/** @file
  MM RAS Error Handler for memory.

  Copyright (c) 2025 Rivos Inc. All rights reserved.
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

CONST EFI_GUID  gUefiCperRiscvCpuSec = {
  0xDB9A32B0,
  0x6B7F,
  0x40E2,
  { 0xAC,    0xA1,0xB0, 0x1D, 0x40, 0x43, 0x3D, 0x48 }
};

/**
  Record a CPU error using the RISC-V RERI error record and populate the GHES
  error block with the appropriate information.

  @param[in,out] ErrorBlock      The GHES error status structure to populate.
  @param[in]     ReriErrorRecord The RERI error record containing error details.
**/
VOID
EFIAPI
RiscVGhesRecordCpuError (
  IN OUT EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *ErrorBlock,
  IN      RISC_V_RERI_ERROR_RECORD                    *ReriErrorRecord,
  IN UINT64                                           RasDataPtr,
  IN UINT32                                           RasDataLen
  )
{
  GENERIC_ERROR_DATA  *DataEntry;
  RISCV_CPER_REC      *RvProcessorErrorSection;
  UINT64              ErrorAddr;

  ASSERT (ErrorBlock != NULL);
  ASSERT (ReriErrorRecord != NULL);

  ErrorAddr = RiscvReriDevReadU64 (&ReriErrorRecord->Addr);

  // Set the fields in the generic error status block header
  ConvertReriToGhes (ErrorBlock, ReriErrorRecord, RasDataLen);

  // Build generic data entry header
  DataEntry = (GENERIC_ERROR_DATA *)((UINT8 *)ErrorBlock +
                                     sizeof (EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE));

  CopyMem (
    &DataEntry->DataEntry.SectionType,
    &gUefiCperRiscvCpuSec,
    sizeof (DataEntry->DataEntry.SectionType)
    );

  DataEntry->DataEntry.ErrorSeverity   = ErrorBlock->ErrorSeverity;
  DataEntry->DataEntry.Revision        = GHES_VERSION;
  DataEntry->DataEntry.ValidationBits  = 0; // FRU information not supported yet
  DataEntry->DataEntry.Flags           = 0;
  DataEntry->DataEntry.ErrorDataLength = RasDataLen;

  ZeroMem (DataEntry->DataEntry.FruId, sizeof (DataEntry->DataEntry.FruId));
  ZeroMem (DataEntry->DataEntry.FruText, sizeof (DataEntry->DataEntry.FruText));

  // Populate the Processor Error Section
  RvProcessorErrorSection = &(DataEntry->CperRecord.CperSections[0].RiscvProcSection);
  ZeroMem (RvProcessorErrorSection, RasDataLen);

  CopyMem (RvProcessorErrorSection, (CONST VOID *)RasDataPtr, RasDataLen);

  DEBUG ((DEBUG_VERBOSE, "CPU error recorded at address 0x%lx\n", ErrorAddr));
}
