/*
 * RISC-V RERI Registers Definitions
 *
 * Copyright (c) 2024 Rivos Inc.
 *
 * Author(s):
 * Dhaval Sharma <dhaval@rivosinc.com
 * Derived from OpenSBI implementation of RERI
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef __RISCV_RERI_REGS_H__
#define __RISCV_RERI_REGS_H__

#include <Uefi.h>
#include <Guid/Cper.h>
#include <IndustryStandard/Acpi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseRiscVSbiLib.h>

#define MAX_ERROR_RECORDS  63
#define MAX_REC_PER_BANK   64                // Define the maximum records per bank
#define RERI_BANKINFO_SZ   64

#define SBI_SSE_EVENT_GLOBAL_RAS 0x00008000

typedef enum {
  RERI_EC_NONE = 0,
  RERI_EC_OUE  = 1,   /* Other unspecified error */
  RERI_EC_CDA  = 2,   /* Corrupted data access */
  RERI_EC_CBA  = 3,   /* Cache block data error */
  RERI_EC_CSD  = 4,   /* Cache scrubbing detected */
  RERI_EC_CAS  = 5,   /* Cache address/state error */
  RERI_EC_CUE  = 6,   /* Cache unspecified error */
  RERI_EC_SDC  = 7,   /* Snoop-filter/directory address/ control state */
  RERI_EC_SUE  = 8,   /* Snoop-filter/directory unspecified error */
  RERI_EC_TPD  = 9,   /* TLB/Page-walk cache data */
  RERI_EC_TPA  = 10,  /* TLB/Page-walk address control state */
  RERI_EC_TPU  = 11,  /* TLB/Page-walk unknown error */
  RERI_EC_HSE  = 12,  /* Hart state error */
  RERI_EC_ICS  = 13,  /* Interrupt controller state */
  RERI_EC_ITD  = 14,  /* Interconnect data error */
  RERI_EC_ITO  = 15,  /* Interconnection other error */
  RERI_EC_IWE  = 16,  /* Internal watchdog error */
  RERI_EC_IDE  = 17,  /* Internal datapath/memory or execution unit error */
  RERI_EC_SBE  = 18,  /* System memory command or address bus error */
  RERI_EC_SMU  = 19,  /* System memory unspecified error */
  RERI_EC_SMD  = 20,  /* System memory data error */
  RERI_EC_SMS  = 21,  /* System memory scrubbing detected error */
  RERI_EC_PIO  = 22,  /* Protocol error illegal IO */
  RERI_EC_PUS  = 23,  /* Protocol error unexpected state */
  RERI_EC_PTO  = 24,  /* Protocol error timeout error */
  RERI_EC_SIC  = 25,  /* System internal controller error */
  RERI_EC_DPU  = 26,  /* Deferred error passthrough not supported */
  RERI_EC_PCX  = 27,  /* PCI/CXL detected error */
  RERI_EC_RES  = 28,  /* Reserved errors start */
  RERI_EC_REE  = 63,  /* Reserved errors end */
  RERI_EC_CES  = 64,  /* Custom error start */
  RERI_EC_CEE  = 255, /* Custom error end */
  RERI_EC_INVALID,
} RISC_V_RERI_ERROR_CODE;

typedef enum {
  RERI_TT_UNSPECIFIED,
  RERI_TT_CUSTOM,
  RERI_TT_RES1,
  RERI_TT_RES2,
  RERI_TT_EXPLICIT_READ,
  RERI_TT_EXPLICIT_WRITE,
  RERI_TT_IMPLICIT_READ,
  RERI_TT_IMPLICIT_WRITE,
  RERI_TT_INVALID,
} RISC_V_RERI_TRANSACTION_TYPE;

typedef union {
  struct {
    UINT16    Ele    : 1;
    UINT16    Cece   : 1;
    UINT16    Ces    : 2;
    UINT16    Ueds   : 2;
    UINT16    Uecs   : 2;
    UINT16    Rsvd   : 8;
    UINT16    Rsvd1  : 16;

    UINT16    Eid;

    UINT16    Sinv   : 1;
    UINT16    Srdp   : 1;
    UINT16    Rsvd2  : 10;
    UINT16    Custom : 4;
  } Bits;
  UINT64    Value;
} RISC_V_RERI_CONTROL;

#define RERI_CTRL_MASK  0xFFFF000001FDull

typedef union {
  struct {
    UINT16    V     : 1;
    UINT16    Ce    : 1;
    UINT16    De    : 1;
    UINT16    Ue    : 1;
    UINT16    Pri   : 2;
    UINT16    Mo    : 1;
    UINT16    C     : 1;
    UINT16    Tt    : 3;
    UINT16    Iv    : 1;
    UINT16    At    : 4;

    UINT16    Siv   : 1;
    UINT16    Tsv   : 1;
    UINT16    Rsvd0 : 2;
    UINT16    Scrub : 1;
    UINT16    Ceco  : 1;
    UINT16    Rsvd1 : 1;
    UINT16    Rdip  : 1;
    UINT16    Ec    : 8;

    UINT16    Rsvd2;

    UINT16    Cec   : 16;
  } Bits;
  UINT64    Value;
} RISC_V_RERI_STATUS;

#define RERI_STS_MASK  0x7800FF3FFFFEull

typedef struct {
  RISC_V_RERI_CONTROL    Control;
  RISC_V_RERI_STATUS     Status;
  UINT64                 Addr;
  UINT64                 Info;
  UINT64                 SupplInfo;
  UINT64                 Timestamp;
  UINT64                 Reserved;
  UINT64                 Custom;
} RISC_V_RERI_ERROR_RECORD;

typedef union {
  struct {
    UINT16    InstId;
    UINT16    NErrRecs;
    UINT64    Reserved0 : 24;
    UINT8     Version   : 8;
  } Bits;
  UINT64    Value;
} RISC_V_RERI_BANK_INFO;

typedef union {
  struct {
    UINT32    VendorId;
    UINT16    ImpId;
    UINT16    Reserved;
  } Bits;
  UINT64    Value;
} RISC_V_RERI_VENDOR_IMP_ID;

typedef struct {
  RISC_V_RERI_VENDOR_IMP_ID    VendorImpId;
  RISC_V_RERI_BANK_INFO        BankInfo;
  UINT64                       ValidSummary;
  UINT64                       Reserved[2];
  UINT64                       Custom[3];
  RISC_V_RERI_ERROR_RECORD     Records[MAX_ERROR_RECORDS];
} RISC_V_RERI_ERROR_BANK;

typedef struct {
  UINT32    Etype;
  union {
    struct {
      UINT32    ValidationBits;
      UINT32    Severity;
      UINT8     ProcessorType;
      UINT8     ProcessorIsa;
      UINT8     ProcessorErrorType;
      UINT8     Operation;
      UINT8     Flags;
      UINT8     Level;
      UINT64    CpuVersion;
      UINT8     CpuBrandString[128];
      UINT64    CpuId;
      UINT64    TargetAddr;
      UINT64    ReqIdent;
      UINT64    RespIdent;
      UINT64    Ip;
    } GenericProcessorError;

    struct {
      UINT64    PhysicalAddress;
    } DramError;
  } Info;
} ACPI_GHES_ERROR_INFO;

/* Masks for block_status flags */
#define ACPI_GEBS_UNCORRECTABLE        (0x1UL << 0)
#define ACPI_GEBS_CORRECTABLE          (0x1UL << 1)
#define ACPI_GEBS_MULTI_UNCORRECTABLE  (0x1UL << 2)
#define ACPI_GEBS_MULTI_CORRECTABLE    (0x1UL << 3)

enum {
  GPE_PROC_TYPE_VALID_BIT,
  GPE_PROC_ISA_VALID_BIT,
  GPE_PROC_ERR_TYPE_VALID_BIT,
  GPE_OP_VALID_BIT,
  GPE_FLAGS_VALID_BIT,
  GPE_LEVEL_VALID_BIT,
  GPE_CPU_VERSION_VALID_BIT,
  GPE_CPU_BRAND_STRING_VALID_BIT,
  GPE_CPU_ID_VALID_BIT,
  GPE_TARGET_ADDR_VALID_BIT,
  GPE_REQ_IDENT_VALID_BIT,
  GPE_RESP_IDENT_VALID_BIT,
  GPE_IP_VALID_BIT,
  GPE_BIT_RESERVED_BITS,
};

enum {
  ERROR_TYPE_MEM,
  ERROR_TYPE_GENERIC_CPU,
  ERROR_TYPE_MAX,
};

#define CPU_VERSION_INFO_VALID       (1 << 0)
#define CPU_VENDOR_INFO_VALID        (1 << 1)
#define CPU_ARCHITECTURE_INFO_VALID  (1 << 2)
#define PROCESSOR_CONTEXT_VALID      (1 << 3)
#define HARTID_VALID                 (1 << 4)
#define ERROR_HEADER_VALID           (1 << 5)
#define ERROR_RECORDS_VALID          (1 << 6)

// TODO: As such for this driver this could be pass through info.
// may not require MM driver to access all this info. In which case
// then we can avoid keeping all these structs in sync with m-mode.
// currently CPER_RECORD is used to estimate initial space. I think
// we could estimate it through some other means instead of relying
// on CPER structs which anyways can change too in size depending on the error
#pragma pack(1)

/*
 * RISC-V processor context structure
 */
typedef struct {
  UINT16    version;
  UINT16    ctx_type;
  UINT16    ctx_size;
  UINT16    reserved;
} ErrContextHdr;

typedef struct {
  UINTN    ra;
  UINTN    sp;
  UINTN    gp;
  UINTN    tp;
  UINTN    t0;
  UINTN    t1;
  UINTN    t2;
  UINTN    s0;
  UINTN    s1;
  UINTN    a0;
  UINTN    a1;
  UINTN    a2;
  UINTN    a3;
  UINTN    a4;
  UINTN    a5;
  UINTN    a6;
  UINTN    a7;
  UINTN    s2;
  UINTN    s3;
  UINTN    s4;
  UINTN    s5;
  UINTN    s6;
  UINTN    s7;
  UINTN    s8;
  UINTN    s9;
  UINTN    s10;
  UINTN    s11;
  UINTN    t3;
  UINTN    t4;
  UINTN    t5;
  UINTN    t6;
} GPR;

typedef struct {
  UINT16    ValidationBits;
  UINT16    ContextSize;
  UINT8     Reserved[4];
  UINT64    CpuVersionInfo;
  UINT64    CpuVendorInfo;
  UINT64    CpuArchitectureInfo;
  UINT64    HartId;
  UINT8     NumContextInfo;
  UINT8     NumErrRecords;
  UINT8     Reserved2[6];
} RISC_V_PROCESSOR_ERROR_SECTION;

typedef struct {
  RISC_V_PROCESSOR_ERROR_SECTION    Pes;
  ErrContextHdr                     Ehdr;
  GPR                               Pctx;
  UINT64                            EBank[8];
  UINT64                            RRecord[8];
} RISCV_CPER_REC;

struct ReriAddr {
  UINT32    Base;
  UINT32    Size;
};

struct RasRecArray {
  UINT64             NumElements;
  struct ReriAddr    ReriAddr[];
};

#pragma pack()

EFI_STATUS
EFIAPI
HandleRASError (
  IN RISC_V_RERI_ERROR_RECORD                                     *ErrorRec,
  EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE  *ErrSrc,
  UINT8                                                           ErrType,
  UINT64                                                          RasDataPtr,
  UINT32                                                          RasDataLen
  );

#define RiscvReriDevReadU64(DevAddr)  (*((volatile UINT64 *)(DevAddr)))

#define RiscvReriDevWriteU64(DevAddr, Value)  (*((volatile UINT64 *)(DevAddr)) = (Value))

VOID
EFIAPI
GhesRecordMemError (
  IN EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *ErrorBlock,
  const RISC_V_RERI_ERROR_RECORD                  *ReriErrorRecord
  );

VOID
EFIAPI
GhesRecordCpuError (
  IN EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *ErrorBlock,
  RISC_V_RERI_ERROR_RECORD                        *ReriErrorRecord,
  UINT64                                          RasDataPtr,
  UINT32                                          RasDataLen
  );

VOID
EFIAPI
RiscVGhesRecordCpuError (
  IN EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *ErrorBlock,
  RISC_V_RERI_ERROR_RECORD                        *ReriErrorRecord,
  UINT64                                          RasDataPtr,
  UINT32                                          RasDataLen
  );

VOID
ConvertReriToGhes (
  EFI_ACPI_6_5_GENERIC_ERROR_STATUS_STRUCTURE  *ErrorBlock,
  const RISC_V_RERI_ERROR_RECORD               *ReriErrorRecord,
  UINT16                                       ErrorDataLength
  );

#endif /* __RISCV_RERI_REGS_H__ */
