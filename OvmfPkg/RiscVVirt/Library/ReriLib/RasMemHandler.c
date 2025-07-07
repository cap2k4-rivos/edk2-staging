/** @file
  MM RAS Error Handler for memory
  Copyright (c) 2024 Rivos Inc All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

// TODO: Find an elegant way to add header through build include path
#include <Library/ReriHdr.h>
#include <Library/RasGatewayDriverMM.h>

#define GHES_ERROR_STATUS_VALID      (1U << 0)  // Bit 0 - Error Status Valid
#define GHES_PHYSICAL_ADDRESS_VALID  (1U << 1)  // Bit 1 - Physical Address Valid

VOID
EFIAPI
GhesRecordMemError (
  IN EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *ErrorBlock,
  const RISC_V_RERI_ERROR_RECORD                        *ReriErrorRecord
  )
{
  GENERIC_ERROR_DATA              *Dentry;
  EFI_PLATFORM_MEMORY_ERROR_DATA  *Msec;
  UINT64                          ErrorAddr;
  UINT16                          ErrorDataLength;
  // UUID for Memory Error Section Type
  CONST EFI_GUID  UefiCperMemSec = {
    0xA5BC1114, 0x6F64, 0x4EDE, { 0xB8, 0x63, 0x3E, 0x83, 0xED, 0x7C, 0x83, 0xB1 }
  };

  ErrorDataLength = sizeof (EFI_PLATFORM_MEMORY_ERROR_DATA);
  ErrorAddr       = RiscvReriDevReadU64 (&ReriErrorRecord->Addr);

  // Set the fields in the generic error status block header
  ConvertReriToGhes (ErrorBlock, ReriErrorRecord, ErrorDataLength);

  // Build generic data entry header
  Dentry = (GENERIC_ERROR_DATA *)((UINT8 *)ErrorBlock + sizeof (EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE));
  CopyMem (&Dentry->DataEntry.SectionType, &UefiCperMemSec, sizeof (Dentry->DataEntry.SectionType));
  Dentry->DataEntry.ErrorSeverity   = ErrorBlock->ErrorSeverity;
  Dentry->DataEntry.Revision        = GHES_VERSION;
  Dentry->DataEntry.ValidationBits  = 0; // We do not support FRU information yet.
  Dentry->DataEntry.Flags           = 0;
  Dentry->DataEntry.ErrorDataLength = ErrorDataLength;

  ZeroMem (Dentry->DataEntry.FruId, sizeof (Dentry->DataEntry.FruId));
  ZeroMem (Dentry->DataEntry.FruText, sizeof (Dentry->DataEntry.FruText));

  // Populate the Memory Error Section; Right now we are using only first section but later
  // we might be able to use other sections if previous record has not been consumed.
  Msec = &(Dentry->CperRecord.CperSections[0].MemorySection);
  ZeroMem (Msec, sizeof (EFI_PLATFORM_MEMORY_ERROR_DATA));
  Msec->ValidFields         = GHES_ERROR_STATUS_VALID;
  Msec->ErrorStatus.Type    = 1;
  Msec->PhysicalAddressMask = (UINT64)-1;

  // Address is valid only if At bits are set
  if (ReriErrorRecord->Status.Bits.At) {
    Msec->PhysicalAddress = ErrorAddr;
    Msec->ValidFields    |= GHES_PHYSICAL_ADDRESS_VALID;
  }

  DEBUG ((DEBUG_VERBOSE, "Memory error recorded at physical address: 0x%016lx\n", ErrorAddr));
}
