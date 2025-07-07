/*
 * RISC-V RERI Registers Definitions
 *
 * Copyright (c) 2024 Rivos Inc.
 *
 * Author(s):
 * Dhaval Sharma <dhaval@rivosinc.com
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 or
 * (at your option) any later version.
 */

#ifndef __RAS_AGENT_API_H__
#define __RAS_AGENT_API_H__

#include <IndustryStandard/Acpi.h>
#include <Uefi.h>

#define __packed32  __attribute__((packed,aligned(__alignof__(UINT32))))

#define MAX_DESC_SIZE  1024
//
// RAS Agent Services on MPXY/RPMI
//
#define RAS_GET_NUM_ERR_SRCS      0x1
#define RAS_GET_ERR_SRCS_ID_LIST  0x2
#define RAS_GET_ERR_SRC_DESC      0x3
#define RAS_GET_ERR_DATA          0x4

typedef struct __packed32 {
  UINT32    status;
  UINT32    flags;
  UINT32    remaining;
  UINT32    returned;
  UINT32    func_id;
} RasRespHeader;

typedef struct __packed32 {
  RasRespHeader    RespHdr;
  UINT32           NumErrorSources;
} RasMsgNumErrSrc;

typedef struct __packed32 {
  RasRespHeader    RespHdr;
  UINT32           SrcId;
} RasErrBufResp;

typedef struct __packed32 {
  RasRespHeader    RespHdr;
  UINT32           RecAddr;
  UINT32           RecLen;
} RasErrBuf;

/** RPMI Error Types */
enum rpmi_error {
  RPMI_SUCCESS        = 0,
  RPMI_ERR_FAILED     = -1,
  RPMI_ERR_NOTSUPP    = -2,
  RPMI_ERR_INVAL      = -3,
  RPMI_ERR_DENIED     = -4,
  RPMI_ERR_NOTFOUND   = -5,
  RPMI_ERR_OUTOFRANGE = -6,
  RPMI_ERR_OUTOFRES   = -7,
  RPMI_ERR_HWFAULT    = -8,
};

#endif /* __RAS_AGENT_API_H__ */
