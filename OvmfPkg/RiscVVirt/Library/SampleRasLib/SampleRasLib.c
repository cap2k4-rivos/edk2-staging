#include <Library/ReriHdr.h>
#include <Library/RasGatewayDriverMM.h>
#include <Library/MemoryAllocationLib.h>

/**
  Determines the error type based on the implementation ID.

  @param[in]  ImpId   The implementation ID to determine the error type.

  @retval UINT8   The error type.
**/

#define MEM_IMP_ID     0x0501
#define CPU_IMP_ID     0x0301

UINT8
GetErrorType (
  UINT64  VImpId
  )
{
  RISC_V_RERI_VENDOR_IMP_ID  CopyImpId;

  CopyImpId.Value = VImpId;
  UINT16  ImpId = CopyImpId.Bits.ImpId;

  switch (ImpId) {
    case MEM_IMP_ID:
      return ERROR_TYPE_MEM;
    case CPU_IMP_ID:
      return ERROR_TYPE_GENERIC_CPU;
    default:
      return ERROR_TYPE_MAX;
  }
}

STATIC
VOID
BuildReriErrorBank(UINT32 Addr, UINT16 val)
{
  RISC_V_RERI_ERROR_RECORD SampleErrorRecord;

  RISC_V_RERI_CONTROL SampleControl = {.Value=1};
  RISC_V_RERI_STATUS SampleStatus = {.Value=1};
  
  SampleErrorRecord.Control = SampleControl;
  SampleErrorRecord.Status = SampleStatus;
  
  RISC_V_RERI_VENDOR_IMP_ID SampleVendorImpl = {.Value = 0x050100000000ULL};
  RISC_V_RERI_BANK_INFO SampleBankInfo = {.Value=0x10000};

  RISC_V_RERI_ERROR_BANK SampleErrorBank;
  SampleErrorBank.VendorImpId = SampleVendorImpl;
  SampleErrorBank.BankInfo = SampleBankInfo;
  SampleErrorBank.Records[0] = SampleErrorRecord;

  CopyMem((void *)(UINT64)Addr, &SampleErrorBank, sizeof(RISC_V_RERI_ERROR_BANK));
}

/**
  Fills the RAS records into the provided RAS source information structure.

  @param[out]  RInfo     Pointer to the RAS source information structure to be filled.
  @param[in, out]  MaxRecs   Pointer to the maximum number of records possible, updated to the number of actual records filled.

  @retval EFI_SUCCESS           The operation was successful.
  @retval EFI_DEVICE_ERROR      There was an SBI error.
**/
EFI_STATUS
EFIAPI
FillRasRecs (
  OUT RasSrcInfo  *RInfo,
  IN OUT UINT16   *MaxRecs
  )
{
  struct RasRecArray      *ReriArray;    // Pointer to RERI array structure
  RISC_V_RERI_ERROR_BANK  *ErrorBank;    // Pointer to RERI error bank
  UINT16                  TotalRec;      // Counter for total actual records
  UINT32                  RecAddr;       // Address of the record
  RISC_V_RERI_BANK_INFO   EBankInfoCopy; // Copy of RERI error bank
  UINT64                  NumElements = 3;

  TotalRec = 0;
  RecAddr  = 0;
  
  struct ReriAddr SourceData[3] = {
        {0x80060000, 0x20000},
        {0x80080000, 0x20000},
        {0x800A0000, 0x20000}
    };

  UINT64 total_size = sizeof(struct RasRecArray) + (NumElements * sizeof(struct ReriAddr));
  ReriArray = AllocateZeroPool(total_size);
  if (ReriArray == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  ReriArray->NumElements = NumElements;
  CopyMem (ReriArray->ReriAddr, SourceData, (NumElements * sizeof(struct ReriAddr)));

  // Loop through each element in the RERI array. We first get the pointer to
  // all error banks and num banks. Each bank has info on num records it
  // populates, so loop through that and extract all error sources to build
  // the desired rec array

  for (
       UINT16 Idx = 0;
       Idx < ReriArray->NumElements;
       Idx++)
  {
    BuildReriErrorBank(ReriArray->ReriAddr[Idx].Base, Idx);
    // Get the error bank from the base address.
    ErrorBank = (RISC_V_RERI_ERROR_BANK *)(UINT64)(ReriArray->ReriAddr[Idx].Base);

    EBankInfoCopy.Value = ErrorBank->BankInfo.Value;

    // Initialize the record address within the bank.
    RecAddr = ReriArray->ReriAddr[Idx].Base + RERI_BANKINFO_SZ;

    // Skip if the number of records exceeds the maximum or if the error type is invalid.
    if (GetErrorType (ErrorBank->VendorImpId.Value) == ERROR_TYPE_MAX) {
      continue;
    }

    ASSERT (EBankInfoCopy.Bits.NErrRecs < MAX_REC_PER_BANK);

    // Loop through each error record in the bank.
    for (
         UINT16 Count = 0;
         Count < EBankInfoCopy.Bits.NErrRecs;
         Count++)
    {
      RInfo->ErrType = GetErrorType (ErrorBank->VendorImpId.Value);
      RInfo->SrcId   = TotalRec++;

      // Ensure the total records do not exceed the maximum.
      ASSERT (TotalRec <= *MaxRecs);

      RInfo->RecAddress = RecAddr;

      // Print debug information about the RAS record.
      DEBUG ((DEBUG_INFO, "RAS Src Idx %d Base Addr %p ErrorType \n", RInfo->SrcId, RecAddr, RInfo->ErrType));

      RInfo++;
      RecAddr += sizeof (RISC_V_RERI_ERROR_RECORD);
    }
  }

  // Update the maximum records with the total count.
  *MaxRecs = TotalRec;

  return EFI_SUCCESS;
}
