/** @file

  Copyright (c) 2024 Rivos Inc.
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Specification Reference:
    - ACPI Reference Specification 6.5, GHESv2 Structure.
**/

#include <Library/RasGatewayDriverMM.h>
#include <Library/DebugLib.h>
#include <Library/DxeRiscvRasAgentClient.h>
#include <IndustryStandard/Acpi.h>
#include <Guid/Cper.h>
#include <Library/ReriHdr.h>
#include <Library/BaseRiscVSbiLib.h>

//
// HEST table GHESv2 type related structure.
// Helper Macro to initialize the HEST GHESv2 Notification Structure.
// Refer Table 18-394 in ACPI Specification, Version 6.3.
//
#define EFI_ACPI_6_5_HARDWARE_ERROR_NOTIFICATION_STRUCTURE_INIT(Type,         \
                                                                PollInterval, EventId)                                                      \
  {                                                                           \
    Type,                                                                     \
    sizeof (EFI_ACPI_6_5_HARDWARE_ERROR_NOTIFICATION_STRUCTURE),              \
    {0, 0, 0, 0, 0, 0, 0}, /* ConfigurationWriteEnable */                     \
    PollInterval,                                                             \
    EventId,                                                                  \
    0,                    /* Poll Interval Threshold Value  */                \
    0,                    /* Poll Interval Threshold Window */                \
    0,                    /* Error Threshold Value          */                \
    0                     /* Error Threshold Window         */                \
  }

#define SAMPLE_ERR_RECS                               1
#define SAMPLE_SECS_PER_REC                           1
#define EFI_ACPI_6_5_HARDWARE_ERROR_NOTIFICATION_SSE  0x0C

/**
  Returns Error source information in GHESvs format. As soon at the gMmHestErrorSourceDescProtocolGuid
  is installed, parent driver calls this function to fill up error source structures for all the sources
  identified by the platform. This buffer is then passed back to UPL to create HEST/GHES from it.


  @param[in out]   *MemInfo           Pointer for RAS memory meta data/driver handle etc.
                                  It gets updated with updated mem pointers on out.
  @param[in]  SrcId              HEST error source ID

  @retval  EFI_SUCCESS            Buffer has valid Error Source descriptor
                                  information.
**/
STATIC
EFI_STATUS
EFIAPI
GhesErrorSourceDescInfoGet (
  IN OUT ACPI_GHES_MEM_INFO  *MemInfo,
  IN UINT16                  SrcId
  )
{
  EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE  *ErrSrc;
  EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE                     *StatusBlock;
  UINT64                                                          RarAddr;
  UINT64                                                          *BAddr;

  if (MemInfo == NULL) {
    return RETURN_NOT_FOUND;
  }

  BAddr = (UINT64 *)MemInfo->GhesGasRegAddrCurr;
  ASSERT (BAddr < (UINT64 *)MemInfo->GhesGasRegEndAddr);
  MemInfo->GhesGasRegAddrCurr += sizeof (UINT64);

  StatusBlock               = (VOID *)MemInfo->GhesErrAddrCurr;
  MemInfo->GhesErrAddrCurr += ERROR_BLOCK_RES_SIZE;
  ASSERT (MemInfo->GhesErrAddrCurr < MemInfo->GhesErrEndAddr);

  ErrSrc = (EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE *)&MemInfo->CurrErrSources[SrcId];
  if (ErrSrc == NULL) {
    return RETURN_NOT_FOUND;
  }

  ErrSrc->Type                         = EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_VERSION_2;
  ErrSrc->SourceId                     = SrcId;
  ErrSrc->RelatedSourceId              = 0;
  ErrSrc->Enabled                      = TRUE;
  ErrSrc->NumberOfRecordsToPreAllocate = SAMPLE_ERR_RECS;
  ErrSrc->MaxSectionsPerRecord         = SAMPLE_SECS_PER_REC;

  ErrSrc->ErrorStatusBlockLength = ERROR_BLOCK_RES_SIZE - READ_ACK_SIZE;
  RarAddr                        = (UINT64)StatusBlock + ErrSrc->ErrorStatusBlockLength;
  *BAddr                         = (UINT64)StatusBlock;

  ErrSrc->ErrorStatusAddress.AddressSpaceId    = EFI_ACPI_6_5_SYSTEM_MEMORY;
  ErrSrc->ErrorStatusAddress.RegisterBitWidth  = sizeof (UINT64) * 8;
  ErrSrc->ErrorStatusAddress.RegisterBitOffset = 0;
  ErrSrc->ErrorStatusAddress.AccessSize        = EFI_ACPI_6_5_QWORD;
  ErrSrc->ErrorStatusAddress.Address           = (UINT64)BAddr;

  ErrSrc->ReadAckRegister.AddressSpaceId    = EFI_ACPI_6_5_SYSTEM_MEMORY;
  ErrSrc->ReadAckRegister.RegisterBitWidth  = sizeof (UINT64) * 8;
  ErrSrc->ReadAckRegister.RegisterBitOffset = 0;
  ErrSrc->ReadAckRegister.AccessSize        = EFI_ACPI_6_5_QWORD;
  ErrSrc->ReadAckRegister.Address           = RarAddr;
  // TODO: Need to fix proper bit setting here; IIUC, GHES supports multiple error
  // records and there should be one bit per record. Depending on which error record
  // is consumed by OS, it will clear that particular bit. Since for now we are creating
  // only one this should suffice.
  ErrSrc->ReadAckPreserve = ~(1UL);
  ErrSrc->ReadAckWrite    = (1UL);

  ErrSrc->NotificationStructure.Type   = EFI_ACPI_6_5_HARDWARE_ERROR_NOTIFICATION_SSE;
  ErrSrc->NotificationStructure.Length = sizeof (EFI_ACPI_6_5_HARDWARE_ERROR_NOTIFICATION_STRUCTURE);

  ErrSrc->NotificationStructure.PollInterval = 0;
  ErrSrc->NotificationStructure.Vector       = SBI_SSE_EVENT_GLOBAL_RAS;

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GhesGenericHandler (
  IN  MM_HEST_ERROR_SOURCE_DESC_PROTOCOL  *This,
  IN  UINT8                               FuncId,
  IN  ACPI_GHES_MEM_INFO                  *MemInfo,
  IN  UINT8                               SrcId,
  IN  UINT64                              RasDataPtr,
  IN  UINT32                              RasDataLen
  )
{
  EFI_STATUS                                                      Status = EFI_UNSUPPORTED;
  RISC_V_RERI_ERROR_RECORD                                        *RecAddr;
  UINT8                                                           Etype;
  EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE  *CurrSrc;

  switch (FuncId) {
    case RAS_GET_ERR_SRC_DESC:
      for (UINT16 Idx = 0; Idx < MemInfo->TotalCount; Idx++) {
        if (MemInfo->HdltoSrcArray[Idx].ErrType >= ERROR_TYPE_MAX) {
          continue;
        }

        Status = GhesErrorSourceDescInfoGet (MemInfo, MemInfo->HdltoSrcArray[Idx].SrcId);
        if (EFI_ERROR (Status)) {
          return Status;
        }

        MemInfo->HdltoSrcArray[Idx].Handle = This;
      }

      break;

    case RAS_GET_ERR_DATA:
      RecAddr = (RISC_V_RERI_ERROR_RECORD *)(UINT64)MemInfo->HdltoSrcArray[SrcId].RecAddress;
      CurrSrc = &MemInfo->CurrErrSources[SrcId];
      Etype   = MemInfo->HdltoSrcArray[SrcId].ErrType;
      return HandleRASError (RecAddr, CurrSrc, Etype, RasDataPtr, RasDataLen);
      break;

    default:
      break;
  }

  return Status;
}

STATIC MM_HEST_ERROR_SOURCE_DESC_PROTOCOL  GhesRasErrorHandler = {
  GhesGenericHandler
};

/**
  Install the HEST Error Source Descriptor protocol handler to allow publishing
  of the supported hardware error sources.

  @param[in]  MmSystemTable  Pointer to System table.

  @retval  EFI_SUCCESS            Protocol installation successful.
  @retval  EFI_INVALID_PARAMETER  Invalid system table parameter.
**/
EFI_STATUS
GhesInstallErrorSourceDescProtocol (
  IN EFI_MM_SYSTEM_TABLE  *MmSystemTable
  )
{
  EFI_HANDLE  mGhesHandle = NULL;
  EFI_STATUS  Status;

  // Check if the MmSystemTable is initialized.
  if (MmSystemTable == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  // Install HEST error source descriptor protocol.
  Status = MmSystemTable->MmInstallProtocolInterface (
                            &mGhesHandle,
                            &gMmHestErrorSourceDescProtocolGuid,
                            EFI_NATIVE_INTERFACE,
                            &GhesRasErrorHandler
                            );
  if (EFI_ERROR (Status)) {
    DEBUG (
      (
       DEBUG_ERROR,
       "%a: Failed installing HEST error source protocol, status: %r\n",
       __FUNCTION__,
       Status
      )
      );
  }

  return Status;
}

/**
Initialize function for the driver.

Registers MMI handlers to process fault events on DMC and installs required
protocols to publish the error source descriptors.

@param[in]  ImageHandle  Handle to image.
@param[in]  SystemTable  Pointer to System table.

@retval  EFI_SUCCESS  On successful installation of error event handler for
                      DMC.
@retval  Other        Failure in installing error event handlers for DMC.
**/
EFI_STATUS
EFIAPI
GhesMmDriverInitialize (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_MM_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  ASSERT (SystemTable != NULL);

  // Installs the HEST error source descriptor protocol.
  Status = GhesInstallErrorSourceDescProtocol (SystemTable);
  return Status;
}
