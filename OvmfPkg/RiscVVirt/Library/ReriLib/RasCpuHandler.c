/** @file
  MM RAS Error Handler for memory.

  Copyright (c) 2024 Rivos Inc. All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/ReriHdr.h>
#include <Library/RasGatewayDriverMM.h>
#include <Library/DebugLib.h>

typedef enum {
  ProcessorTypeValid      = BIT0,     // Bit 0 - Processor Type Valid
  ProcessorIsaValid       = BIT1,     // Bit 1 - Processor ISA Valid
  ProcessorErrorTypeValid = BIT2,     // Bit 2 - Processor Error Type Valid
  OperationValid          = BIT3,     // Bit 3 - Operation Valid
  FlagsValid              = BIT4,     // Bit 4 - Flags Valid
  LevelValid              = BIT5,     // Bit 5 - Level Valid
  CpuVersionValid         = BIT6,     // Bit 6 - CPU Version Valid
  CpuBrandInfoValid       = BIT7,     // Bit 7 - CPU Brand Info Valid
  CpuIdValid              = BIT8,     // Bit 8 - CPU Id Valid
  TargetAddressValid      = BIT9,     // Bit 9 - Target Address Valid
  RequesterIdValid        = BIT10,    // Bit 10 - Requester Identifier Valid
  ResponderIdValid        = BIT11,    // Bit 11 - Responder Identifier Valid
  InstructionIpValid      = BIT12     // Bit 12 - Instruction IP Valid
} PROCESSOR_ERROR_VALIDATION_BITS;

#define PROCESSOR_TYPE  0x3
#define PROCESSOR_ISA   0x6

typedef enum {
  ProcessorErrorUnknown   = 0x00,    // Unknown
  ProcessorErrorCache     = 0x01,    // Cache Error
  ProcessorErrorTlb       = 0x02,    // TLB Error
  ProcessorErrorBus       = 0x04,    // Bus Error
  ProcessorErrorMicroArch = 0x08     // Micro-Architectural Error
} PROCESSOR_ERROR_TYPE;

typedef enum {
  OperationUnknownOrGeneric = 0,   // Unknown or generic
  OperationDataRead         = 1,   // Data Read
  OperationDataWrite        = 2,   // Data Write
  OperationInstructionExec  = 3    // Instruction Execution
} PROCESSOR_OPERATION_TYPE;

CONST EFI_GUID  gUefiCperGenericCpuSec = {
  0x9876CCAD,
  0x47B4,
  0x4bdb,
  { 0xB6,    0x5E,0x16, 0xF1, 0x93, 0xC4, 0xF3, 0xDB }
};

/**
  Get the CPU operation type based on the RISC-V RERI status.

  @param[in]  Status  The RERI status structure.

  @return  The operation type as defined in PROCESSOR_OPERATION_TYPE.
**/
STATIC
UINT8
GetCpuOpType (
  IN CONST RISC_V_RERI_STATUS  Status
  )
{
  switch (Status.Bits.Tt) {
    case RERI_TT_EXPLICIT_READ:
    case RERI_TT_IMPLICIT_READ:
      return OperationDataRead;

    case RERI_TT_EXPLICIT_WRITE:
    case RERI_TT_IMPLICIT_WRITE:
      return OperationDataWrite;

    default:
      DEBUG ((DEBUG_INFO, "Unknown CPU operation type: 0x%x\n", Status.Bits.Tt));
      return OperationUnknownOrGeneric;
  }
}

/**
  Get the CPU error type based on the RISC-V RERI status.

  @param[in]  Status  The RERI status structure.

  @return  The error type as defined in PROCESSOR_ERROR_TYPE.
**/
STATIC
UINT8
GetCpuErrorType (
  IN CONST RISC_V_RERI_STATUS  Status
  )
{
  switch (Status.Bits.Ec) {
    case RERI_EC_CBA:
    case RERI_EC_CSD:
    case RERI_EC_CAS:
    case RERI_EC_CUE:
      return ProcessorErrorCache;

    case RERI_EC_TPD:
    case RERI_EC_TPA:
    case RERI_EC_TPU:
      return ProcessorErrorTlb;

    case RERI_EC_ITO:
    case RERI_EC_ITD:
    case RERI_EC_IDE:
    case RERI_EC_SBE:
      return ProcessorErrorBus;

    case RERI_EC_SDC:
    case RERI_EC_SUE:
    case RERI_EC_HSE:
    case RERI_EC_IWE:
      return ProcessorErrorMicroArch;

    default:
      DEBUG ((DEBUG_INFO, "Unknown CPU error type: 0x%x\n", Status.Bits.Ec));
      return ProcessorErrorUnknown;
  }
}

/**
  Populate the CPU error data structure with information from the RERI error record.

  @param[out] CpuErrorData    The CPU error data structure to populate.
  @param[in]  ReriErrorRecord The RERI error record containing error details.
**/
STATIC
VOID
FillCpuErrorData (
  OUT EFI_PROCESSOR_GENERIC_ERROR_DATA  *CpuErrorData,
  IN  CONST RISC_V_RERI_ERROR_RECORD    *ReriErrorRecord
  )
{
  UINT64  ErrorAddr;

  ASSERT (CpuErrorData != NULL);
  ASSERT (ReriErrorRecord != NULL);

  ErrorAddr                  = RiscvReriDevReadU64 (&ReriErrorRecord->Addr);
  CpuErrorData->Type         = PROCESSOR_TYPE;
  CpuErrorData->ValidFields |= ProcessorTypeValid;
  CpuErrorData->Isa          = PROCESSOR_ISA;
  CpuErrorData->ValidFields |= ProcessorIsaValid;
  CpuErrorData->ErrorType    = GetCpuErrorType (ReriErrorRecord->Status);
  CpuErrorData->ValidFields |= ProcessorErrorTypeValid;
  CpuErrorData->Operation    = GetCpuOpType (ReriErrorRecord->Status);
  CpuErrorData->ValidFields |= OperationValid;

  // Address is valid only if At bits are set
  if (ReriErrorRecord->Status.Bits.At) {
    CpuErrorData->TargetAddr   = ErrorAddr;
    CpuErrorData->ValidFields |= TargetAddressValid;
  }

  // TODO: Introduce SBI call to acquire more specific information like
  // Target Address, Instruction IP, etc.
}

/**
  Record a CPU error using the RISC-V RERI error record and populate the GHES
  error block with the appropriate information.

  @param[in,out] ErrorBlock      The GHES error status structure to populate.
  @param[in]     ReriErrorRecord The RERI error record containing error details.
**/
VOID
EFIAPI
GhesRecordCpuError (
  IN OUT EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *ErrorBlock,
  IN      RISC_V_RERI_ERROR_RECORD                    *ReriErrorRecord,
  IN UINT64                                           RasDataPtr,
  IN UINT32                                           RasDataLen
  )
{
  GENERIC_ERROR_DATA                *DataEntry;
  EFI_PROCESSOR_GENERIC_ERROR_DATA  *ProcessorErrorSection;
  UINT64                            ErrorAddr;
  UINT16                            ErrorDataLength;

  ASSERT (ErrorBlock != NULL);
  ASSERT (ReriErrorRecord != NULL);

  ErrorDataLength = sizeof (EFI_PROCESSOR_GENERIC_ERROR_DATA);
  ErrorAddr       = RiscvReriDevReadU64 (&ReriErrorRecord->Addr);

  // Set the fields in the generic error status block header
  ConvertReriToGhes (ErrorBlock, ReriErrorRecord, ErrorDataLength);

  // Build generic data entry header
  DataEntry = (GENERIC_ERROR_DATA *)((UINT8 *)ErrorBlock +
                                     sizeof (EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE));

  CopyMem (
    &DataEntry->DataEntry.SectionType,
    &gUefiCperGenericCpuSec,
    sizeof (DataEntry->DataEntry.SectionType)
    );

  DataEntry->DataEntry.ErrorSeverity   = ErrorBlock->ErrorSeverity;
  DataEntry->DataEntry.Revision        = GHES_VERSION;
  DataEntry->DataEntry.ValidationBits  = 0; // FRU information not supported yet
  DataEntry->DataEntry.Flags           = 0;
  DataEntry->DataEntry.ErrorDataLength = ErrorDataLength;

  ZeroMem (DataEntry->DataEntry.FruId, sizeof (DataEntry->DataEntry.FruId));
  ZeroMem (DataEntry->DataEntry.FruText, sizeof (DataEntry->DataEntry.FruText));

  // Populate the Processor Error Section
  ProcessorErrorSection = &(DataEntry->CperRecord.CperSections[0].ProcessorSection);
  ZeroMem (ProcessorErrorSection, sizeof (EFI_PROCESSOR_GENERIC_ERROR_DATA));

  FillCpuErrorData (ProcessorErrorSection, ReriErrorRecord);

  DEBUG ((DEBUG_INFO, "CPU error recorded at address 0x%lx\n", ErrorAddr));
}
