/** @file
  Copyright (c) 2024, Rivos Inc
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef RAS_GATEWAY_DRIVER_MM_H_
#define RAS_GATEWAY_DRIVER_MM_H_

#include <IndustryStandard/Acpi.h>
#include <Guid/Cper.h>
#include <Library/RasAgentApi.h>
#include <Library/ReriHdr.h>

#define ROUNDUP_2_64B(sz)  (((sz) + 0x3F) & ~0x3F)
#define HEST_ERROR_SOURCE_DESC_INFO_SIZE \
  (OFFSET_OF (HEST_ERROR_SOURCE_DESC_INFO, ErrSourceDescList))
#define GHES_VERSION  (UINT16)0x300

typedef UINT64 read_ack_reg_t;
#define READ_ACK_SIZE         sizeof (read_ack_reg_t)
#define ERROR_BLOCK_RES_SIZE  (UINT64)(sizeof(EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE) \
                       + sizeof(EFI_ACPI_6_5_GENERIC_ERROR_DATA_ENTRY_STRUCTURE) \
                       + sizeof(CPER_RECORD) \
                       + READ_ACK_SIZE)

#define MAX_ERR_SRCS      (UINT64)128
#define MAX_ERR_RECS      32
#define MAX_SECS_PER_REC  1

// Binds all the information related to an error source at one place.
typedef struct {
  UINT16        SrcId;      // SrcID assigned to this error source
  EFI_HANDLE    Handle;     // Handle of the driver who signed up to handle it
  UINT32        RecAddress; // RAS MMIO address that describes the error
  UINT8         ErrType;    // Type of the error. Memory/CPU etc.
} RasSrcInfo;

typedef struct {
  UINT64                                                            GhesGasRegMemSz;
  UINT64                                                            GhesGasRegAddr;
  UINT64                                                            GhesGasRegAddrCurr;
  UINT64                                                            GhesGasRegEndAddr;
  UINT64                                                            GhesErrAddr;
  UINT64                                                            GhesErrMemSz;
  UINT64                                                            GhesErrAddrCurr;
  UINT64                                                            GhesErrEndAddr;
  RasSrcInfo                                                        HdltoSrcArray[MAX_ERR_SRCS];
  EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE    *CurrErrSources;
  UINT16                                                            TotalCount;
} ACPI_GHES_MEM_INFO;

typedef union {
  EFI_PROCESSOR_GENERIC_ERROR_DATA    ProcessorSection;
  EFI_PLATFORM_MEMORY_ERROR_DATA      MemorySection;
  RISCV_CPER_REC                      RiscvProcSection;
} CPER_SECTION_UNION;

typedef struct {
  CPER_SECTION_UNION    CperSections[MAX_SECS_PER_REC];
} CPER_RECORD;

typedef struct {
  EFI_ACPI_6_5_GENERIC_ERROR_DATA_ENTRY_STRUCTURE    DataEntry;
  CPER_RECORD                                        CperRecord;
} GENERIC_ERROR_DATA;

//
// Data Structure to communicate the error source descriptor information from
// Standalone MM.
//
typedef struct __packed {
  //
  // Total count of error source descriptors.
  //
  UINTN    ErrSourceDescCount;
  //
  // Total size of all the error source descriptors.
  //
  UINTN    ErrSourceDescSize;
  //
  // Array of error source descriptors that is ErrSourceDescSize in size.
  //
  UINT8    ErrSourceDescList[1];
} HEST_ERROR_SOURCE_DESC_INFO;

#define MM_HEST_ERROR_SOURCE_DESC_PROTOCOL_GUID \
  { \
    0x560bf236, 0xa4a8, 0x4d69, { 0xbc, 0xf6, 0xc2, 0x97, 0x24, 0x10, 0x9d, 0x91 } \
  }

typedef struct MmHestErrorSourceDescProtocol
  MM_HEST_ERROR_SOURCE_DESC_PROTOCOL;

typedef
  EFI_STATUS
(EFIAPI *MM_HEST_GET_ERROR_SOURCE_DESCRIPTORS)(
  IN  MM_HEST_ERROR_SOURCE_DESC_PROTOCOL *This,
  IN  UINT8                              FuncId,
  IN  ACPI_GHES_MEM_INFO                 *MemInfo,
  IN  UINT8                              SrcId,
  IN  UINT64                             RasAddr,
  IN  UINT32                             DataLen
  );

struct MmHestErrorSourceDescProtocol {
  MM_HEST_GET_ERROR_SOURCE_DESCRIPTORS    GhesGenericHandler;
};

EFI_STATUS
EFIAPI
FillRasRecs (
  OUT RasSrcInfo  *RInfo,
  IN OUT UINT16   *MaxRecs
  );

#endif // RAS_GATEWAY_DRIVER_MM_H_
