/** @file
  Reri Library functions for RISC-V
  Copyright (c) 2023, Rivos Inc. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/
#include <Library/ReriHdr.h>

STATIC
BOOLEAN
EFIAPI
OspmAckedPrevErr (
  IN EFI_ACPI_6_5_GENERIC_ADDRESS_STRUCTURE  *ReadAckRegister,
  IN UINT64                                  AckPreserve,
  IN UINT64                                  AckWrite
  )
{
  UINT64  Resp;

  // If there is no ack register, assume the previous error has been acknowledged
  if (ReadAckRegister->Address == 0) {
    return TRUE;
  }

  // Read the response from the register address
  Resp = *((volatile UINT64 *)(UINTN)ReadAckRegister->Address);

  // If register contains zero, assume it's acknowledged
  if (Resp == 0) {
    return TRUE;
  }

  // Apply preserve and write masks
  Resp = (Resp & AckPreserve) | AckWrite;

  // Return TRUE if the result is non-zero, indicating acknowledgment
  return (Resp != 0);
}

EFI_STATUS
EFIAPI
HandleRASError (
  IN RISC_V_RERI_ERROR_RECORD                                     *ErrorRec,
  EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE  *ErrSrc,
  UINT8                                                           ErrType,
  UINT64                                                          RasDataPtr,
  UINT32                                                          RasDataLen
  )
{
  RISC_V_RERI_STATUS                           Status;
  EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *StatusBlock;
  UINT64                                       *Gas;

  DEBUG ((DEBUG_INFO, "%a Request for Err Addr %p Err Type %d", __func__, ErrorRec, ErrType));

  Status.Value = RiscvReriDevReadU64 (&ErrorRec->Status.Value);
  if (!OspmAckedPrevErr (
         &ErrSrc->ReadAckRegister,
         ErrSrc->ReadAckPreserve,
         ErrSrc->ReadAckWrite
         ))
  {
    DEBUG ((DEBUG_ERROR, "OSPM hasn't acknowledged the previous error. New error record cannot be created.\n"));
    return EFI_NOT_READY;
  }

  // Obtain the error status block address from the GAS structure
  Gas         = (UINT64 *)(UINTN)ErrSrc->ErrorStatusAddress.Address;
  StatusBlock = (EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE *)(UINTN)(*Gas);

  if (ErrType == ERROR_TYPE_MEM) {
    GhesRecordMemError (StatusBlock, ErrorRec);
  } else if (ErrType == ERROR_TYPE_GENERIC_CPU) {
    // If there is additional data we can create RV CPER out of it
    if (RasDataLen) {
      RiscVGhesRecordCpuError (StatusBlock, ErrorRec, RasDataPtr, RasDataLen);
    } else {
      GhesRecordCpuError (StatusBlock, ErrorRec, RasDataPtr, RasDataLen);
    }
  } else {
    return EFI_UNSUPPORTED;
  }

  return EFI_NOT_READY;
}

/**
  Convert a RISC_V_RERI_ERROR_RECORD into a GHES-style error status record.

  This function populates the EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE fields
  (ErrorBlock) using information from the RISC_V_RERI_ERROR_RECORD (ReriErrorRecord).
  TODO: There is more optimization required but will be influenced by how OS consumes this
  data i.e. "Recoverable" is a subjective state and requires OS consideration.
  @param[in,out] ErrorBlock       Pointer to the GHES error status structure to be updated.
  @param[in]     ReriErrorRecord  Pointer to the RERI error record data which provides error
                                  type and status bits.

  @retval None
**/
VOID
ConvertReriToGhes (
  EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *ErrorBlock,
  const RISC_V_RERI_ERROR_RECORD               *ReriErrorRecord,
  UINT16                                       ErrorDataLength
  )
{
  EFI_ACPI_6_5_ERROR_BLOCK_STATUS  *BlockStatusPtr;

  //
  // Initialize basic fields in the ErrorBlock.
  //
  ErrorBlock->RawDataOffset = 0;
  ErrorBlock->RawDataLength = 0;
  ErrorBlock->DataLength    = sizeof (EFI_ACPI_6_5_GENERIC_ERROR_DATA_ENTRY_STRUCTURE) \
                              + ErrorDataLength;

  //
  // Get a pointer to the BlockStatus bits instead of casting multiple times.
  //
  BlockStatusPtr = (EFI_ACPI_6_5_ERROR_BLOCK_STATUS *)&ErrorBlock->BlockStatus;

  //
  // If the RERI record indicates a correctable error, mark it as such.
  //
  if (ReriErrorRecord->Status.Bits.Ce) {
    BlockStatusPtr->CorrectableErrorValid = 1;
    ErrorBlock->ErrorSeverity             = EFI_ACPI_6_5_ERROR_SEVERITY_CORRECTED;
  }

  //
  // If 'De' is set, treat it as an uncorrectable error and mark severity as recoverable.
  //
  if (ReriErrorRecord->Status.Bits.De) {
    BlockStatusPtr->UncorrectableErrorValid = 1;
    ErrorBlock->ErrorSeverity               = EFI_ACPI_6_5_ERROR_SEVERITY_RECOVERABLE;
  }

  //
  // If 'Ue' is set, treat it as an uncorrectable error and mark severity as fatal.
  //
  if (ReriErrorRecord->Status.Bits.Ue) {
    BlockStatusPtr->UncorrectableErrorValid = 1;
    ErrorBlock->ErrorSeverity               = EFI_ACPI_6_5_ERROR_SEVERITY_FATAL;
  }

  //
  // If multiple-correctable is indicated (Mo bit), update the count
  // of multiple correctable errors only if we already have a correctable error.
  //
  if (ReriErrorRecord->Status.Bits.Mo && BlockStatusPtr->CorrectableErrorValid) {
    BlockStatusPtr->MultipleCorrectableErrors = ReriErrorRecord->Status.Bits.Cec;
  }

  //
  // RERI does not count multiple uncorrectable errors.
  //
  BlockStatusPtr->MultipleUncorrectableErrors = 0;
  BlockStatusPtr->ErrorDataEntryCount         = 1;
}
