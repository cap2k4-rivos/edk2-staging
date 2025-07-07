/** @file
  MM HEST error source gateway driver.
  Copyright (c) 2024 Rivos Inc All rights reserved.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DxeRiscvRasAgentClient.h>
#include <IndustryStandard/Acpi.h>
#include <Library/PcdLib.h>
#include <Guid/Cper.h>
#include <Library/RasGatewayDriverMM.h>
#include <Library/ReriHdr.h>
#include <Library/BaseRiscVSbiLib.h>

STATIC EFI_MM_SYSTEM_TABLE  *mMmst = NULL;
typedef EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE GHESV2;
STATIC GHESV2              *mErrSources;
STATIC ACPI_GHES_MEM_INFO  mGhesMem;

// PcdRasErrBlockAddr is reserved and Sandman needs to make an entry of this in DT.
// Later it will be moved into DT so that no hardcoding is required at 2 places.

/*
                                                             ┌───────────────────────┐xxxxx
                                                             │                       │    x
                                                             │                       │    x
                             GHESV2                          │                       │    x
                                                             │Error Address Registers│    xxxx
                                                             │                       │    x
                          ┌─────────────────┐                │                       │    x GhesGasRegAddr
                          │                 │                │                       │    x
                          │                 │                ┼───────────────────────┤    x
                          │                 │                │   Error Src 1 Addr    │    x
                          │                 │                ┼───────────────────────┤    x
                          │                 │      ┌─────────►   Error Src 0 Addr    │xxxxx
                          │                 │      │         ┼───────────────────────┤ xxxxx
                          │                 │      │         │                       │     x
                          │                 │      │         │                       │     x
                          ┼─────────────────┤      │         │                       │     x
                          │ Error Register  ┼──────┘         │  CPER + Meta Data     │     x
                          │                 │                │                       │     x
                          ┼─────────────────┼                │                       │     xxxx
┌──────────────────┐      │                 │                │                       │     x
│                  │      │                 │                ┼───────────────────────┤     x GhesErrAddrCurr
│  GHESV2 Source 0 ┼──────►                 │                │  Err Src 0 Ack Reg    │     x
├──────────────────┼      │                 │                ┼───────────────────────┤     x
│                  │      │                 │                │                       │     x
│                  │      │                 │                │  Err Src 0 Data       │     x
├──────────────────┤      │                 │                │                       │ xxxxx
│                  │      └─────────────────┘                └───────────────────────┘
│                  │                                                          PcdRasErrBlockAddr
└──────────────────┘
         mErrSources
Primarily 2 different types of structure are created.
1. ErrorSourceInfo(mErrSources): This is GHESv2 compliant structure created in local memory by the driver.
   This local buffer is used to retrieve source info when caller requests. This structure
   eventually ends up being part of HEST ACPI table which provies OS info about error source.
   importantly it contains pointer to an error register which is ACPI GAS type register containing
   pointer to actual error data block. OS uses this trail to get to actual error data when it
   receives SSE for a particular error source.
2. Error Data Block(GhesGasRegAddr): This memory is a shared pre-known buffer (PcdRasErrBlockAddr) between
   MM and OS. It contains CPER and other required error related data structures. As mentioned above its
   address is provided to the OS through GHES table.

RAS buffer is split into 2 blocks. First block contains pre-allocated error data information and 2nd part
contains info about pointers to error records held by first part.

Communication Buffer Management: When data arrives here, it has actual RAS command with data length.
It is a local buffer which contains data copied over from MM buffer (reserved by platform). Depending
on the message id, it gets cast to a specific service struct. On the return this buffer gets copied
over to MM buffer (reserved by platform) by MM driver which is then received by caller.
*/

/**
  AcpiGhesInit - Initialize the GHES error reporting structure.

  @param[in] Addr  The base address of the GHES error reporting region.
  @param[in] Size  The size of the GHES error reporting region.

  @retval EFI_SUCCESS           Initialization completed successfully.
  @retval EFI_INVALID_PARAMETER Invalid parameters were passed.
  @retval EFI_OUT_OF_RESOURCES  Failed to allocate memory for error sources.
**/
STATIC
EFI_STATUS
EFIAPI
AcpiGhesInit (
  IN UINT64  Addr,
  IN UINT64  Size
  )
{
  if ((Size == 0) || (Addr == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  mErrSources = AllocateZeroPool (sizeof (EFI_ACPI_6_5_GENERIC_HARDWARE_ERROR_SOURCE_VERSION_2_STRUCTURE) * MAX_ERR_SRCS);
  if (mErrSources == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((DEBUG_INFO, "%a\n", __func__));
  ZeroMem (&mGhesMem, sizeof (mGhesMem));
  mGhesMem.GhesErrAddrCurr = mGhesMem.GhesErrAddr = Addr;
  mGhesMem.GhesErrMemSz    = Size;
  mGhesMem.GhesErrEndAddr  = Addr + MAX_ERR_SRCS * ERROR_BLOCK_RES_SIZE;
  ASSERT (mGhesMem.GhesErrEndAddr < Addr + Size);

  mGhesMem.GhesGasRegMemSz    = sizeof (UINT64) * MAX_ERR_SRCS;
  mGhesMem.GhesGasRegAddr     = ROUNDUP_2_64B (mGhesMem.GhesErrEndAddr);
  mGhesMem.GhesGasRegAddrCurr = mGhesMem.GhesGasRegAddr;
  mGhesMem.GhesGasRegEndAddr  = mGhesMem.GhesGasRegAddr + mGhesMem.GhesGasRegMemSz;
  ASSERT (mGhesMem.GhesGasRegEndAddr < Addr + Size);
  mGhesMem.CurrErrSources = mErrSources;
  mGhesMem.TotalCount     = MAX_ERR_SRCS;
  FillRasRecs (mGhesMem.HdltoSrcArray, &mGhesMem.TotalCount);

  return EFI_SUCCESS;
}

/**
  acpi_ghes_get_num_err_srcs - Get the total number of GHES error sources.

  @retval The total number of GHES error sources.
**/
UINT32
acpi_ghes_get_num_err_srcs (
  VOID
  )
{
  return mGhesMem.TotalCount;
}

/**
  acpi_ghes_get_err_src_desc - Get the descriptor of the specified error source.

  @param[in] SrcId  The ID of the error source.

  @retval Pointer to the error source descriptor if found.
  @retval NULL if the error source descriptor is not found.
**/
VOID *
acpi_ghes_get_err_src_desc (
  UINT32  SrcId
  )
{
  for (UINT16 Idx = 0; Idx < mGhesMem.TotalCount; Idx++) {
    if (mErrSources != NULL) {
      if (mErrSources[Idx].SourceId == SrcId) {
        return (VOID *)(mErrSources+Idx);
      }
    }
  }

  return NULL;
}

/**
  acpi_ghes_get_err_data - Retrieve error data from the specified record address.

  @param[in] RecAddr  The record address from which to retrieve error data.
  @param[out] SseVec   The retrieved sse vector for the error record.

  @retval EFI_SUCCESS       Error data retrieved successfully.
  @retval EFI_UNSUPPORTED   No error data found for the specified record address.
  @retval Other             An error occurred during error data retrieval.
**/
STATIC
EFI_STATUS
acpi_ghes_get_err_data (
  IN   UINT32  RecAddr,
  IN   UINT64  RasDataPtr,
  IN   UINT32  DataLen,
  OUT  UINT16  *SseVec
  )
{
  EFI_STATUS                          Status = EFI_UNSUPPORTED;
  MM_HEST_ERROR_SOURCE_DESC_PROTOCOL  *GhesRasErrorHandler;

  for (UINT16 Idx = 0; Idx < mGhesMem.TotalCount; Idx++) {
    if (mGhesMem.HdltoSrcArray[Idx].RecAddress == RecAddr) {
      *SseVec = mGhesMem.CurrErrSources[Idx].NotificationStructure.Vector;
      ASSERT (*SseVec == SBI_SSE_EVENT_GLOBAL_RAS); // Later add HP and LP SSE events as per spec udpate.
      GhesRasErrorHandler = (MM_HEST_ERROR_SOURCE_DESC_PROTOCOL *)mGhesMem.HdltoSrcArray[Idx].Handle;
      Status              = GhesRasErrorHandler->GhesGenericHandler (
                                                   GhesRasErrorHandler,
                                                   RAS_GET_ERR_DATA,
                                                   &mGhesMem,
                                                   mGhesMem.HdltoSrcArray[Idx].SrcId,
                                                   RasDataPtr,
                                                   DataLen
                                                   );
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  return Status;
}

/**
  Err_Source_Init - Initialize the Error Source.

  @param[in] Protocol   The GUID of the protocol.
  @param[in] Interface  The pointer to the protocol interface. Unused.
  @param[in] Handle     The handle of the protocol.

  @retval EFI_SUCCESS   The error source was successfully initialized.
  @retval Other         An error occurred when initializing the error source.
**/
STATIC
EFI_STATUS
Err_Source_Init (
  IN      CONST EFI_GUID  *Protocol,
  IN      VOID            *Interface,
  IN      EFI_HANDLE      Handle
  )
{
  EFI_STATUS                          Status = EFI_SUCCESS;
  MM_HEST_ERROR_SOURCE_DESC_PROTOCOL  *GhesRasErrorHandler;

  Status = mMmst->MmHandleProtocol (
                    Handle,
                    (EFI_GUID *)Protocol,
                    (VOID **)&GhesRasErrorHandler
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = GhesRasErrorHandler->GhesGenericHandler (
                                  GhesRasErrorHandler,
                                  RAS_GET_ERR_SRC_DESC,
                                  &mGhesMem,
                                  0,
                                  0,
                                  0
                                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

/**
  Mmi handler handles various commands coming in from the RAS client.

  @param[in]       DispatchHandle  The unique handle assigned to this handler by
                                   MmiHandlerRegister().
  @param[in]       Context         Points to an optional handler context that
                                   is specified when the handler was registered.
  @param[in, out]  CommBuffer      Buffer used for communication of HEST error
                                   source descriptors.
  @param[in, out]  CommBufferSize  The size of the CommBuffer.

  @retval  EFI_SUCCESS            CommBuffer has valid data.
  @retval  EFI_BAD_BUFFER_SIZE    CommBufferSize not adequate.
  @retval  EFI_OUT_OF_RESOURCES   System out of memory resources.
  @retval  EFI_INVALID_PARAMETER  Invalid CommBufferSize recieved.
  @retval  Other                  For any other error.
**/
STATIC
EFI_STATUS
EFIAPI
HestErrorSourcesInfoMmiHandler (
  IN     EFI_HANDLE  DispatchHandle,
  IN     CONST VOID  *Context,
  IN OUT VOID        *CommBuffer,
  IN     UINTN       *CommBufferSize
  )
{
  RasRespHeader  *CommandIn;
  RasErrBuf      *ErrBuf;
  RasErrBufResp  *ErrRespBuf;
  UINT32         NumErrSrc;
  UINT16         SrcIdResp;
  ErrDescResp    *desc;

  if ((*CommBufferSize < HEST_ERROR_SOURCE_DESC_INFO_SIZE) || (CommBuffer == NULL)) {
    //
    // Ensures that the communication buffer has enough space
    //
    DEBUG (
      (
       DEBUG_ERROR,
       "%a: Invalid CommBufferSize parameter here\n",
       __FUNCTION__
      )
      );
    return EFI_INVALID_PARAMETER;
  }

  CommandIn = (RasRespHeader *)CommBuffer;
  ErrBuf    = (RasErrBuf *)CommBuffer;

  switch (CommandIn->func_id) {
    case RAS_GET_NUM_ERR_SRCS:
      DEBUG ((DEBUG_INFO, "%a: Incoming command %d\n", __FUNCTION__, CommandIn->func_id));
      NumErrSrc = acpi_ghes_get_num_err_srcs ();
      ZeroMem (CommandIn, sizeof (RasMsgNumErrSrc));
      CommandIn->flags     = 0;
      CommandIn->status    = RPMI_SUCCESS;
      CommandIn->remaining = 0;
      CommandIn->returned  = 0;

      ((RasMsgNumErrSrc *)CommBuffer)->NumErrorSources = NumErrSrc;
      *CommBufferSize                                  = sizeof (RasMsgNumErrSrc);
      break;

    case RAS_GET_ERR_SRCS_ID_LIST:
      DEBUG ((DEBUG_INFO, "%a: Incoming command %d\n", __FUNCTION__, CommandIn->func_id));
      ZeroMem (CommandIn, sizeof (ErrorSourceListResp));
      CommandIn->flags     = 0;
      CommandIn->status    = RPMI_SUCCESS;
      CommandIn->remaining = 0;
      CommandIn->returned  = mGhesMem.TotalCount;

      for (UINT16 Idx = 0; Idx < mGhesMem.TotalCount; Idx++) {
        ((ErrorSourceListResp *)CommBuffer)->ErrSourceList[Idx] = mGhesMem.HdltoSrcArray[Idx].SrcId;
      }

      *CommBufferSize = sizeof (ErrorSourceListResp);
      break;

    case RAS_GET_ERR_SRC_DESC:
      DEBUG ((DEBUG_INFO, "%a: Incoming command %d\n", __FUNCTION__, CommandIn->func_id));
      desc = (ErrDescResp *)CommBuffer;
      UINT16  SrcId = desc->desc[0];
      ZeroMem (CommBuffer, sizeof (ErrDescResp));
      CommandIn->flags     = 0;
      CommandIn->status    = RPMI_SUCCESS;
      CommandIn->remaining = 0;
      CommandIn->returned  = sizeof (GHESV2);
      CONST VOID  *ErrSrcBuff = acpi_ghes_get_err_src_desc (SrcId);
      if (ErrSrcBuff == NULL) {
        return EFI_UNSUPPORTED;
      }

      CopyMem (desc->desc, (const void *)ErrSrcBuff, sizeof (GHESV2));
      *CommBufferSize = sizeof (ErrDescResp);
      break;

    case RAS_GET_ERR_DATA:
      acpi_ghes_get_err_data (ErrBuf->RecAddr, (UINT64)(ErrBuf + 1), ErrBuf->RecLen, &SrcIdResp);
      ErrRespBuf        = (RasErrBufResp *)CommBuffer;
      ErrRespBuf->SrcId = SBI_SSE_EVENT_GLOBAL_RAS;
      *CommBufferSize   = sizeof (RasErrBufResp);
      break;

    default:
      DEBUG ((DEBUG_INFO, "%a: Invalid command %d\n", __FUNCTION__, CommandIn->func_id));
      break;
  }

  return EFI_SUCCESS;
}

/**
  Entry point for this Standalone MM driver.

  Registers an Mmi handler that retrieves the error source descriptors from all
  the MM drivers implementing the MM_HEST_ERROR_SOURCE_DESC_PROTOCOL.

  @param[in]  ImageHandle  The firmware allocated handle for the EFI image.
  @param[in]  SystemTable  A pointer to the EFI System Table.

  @retval  EFI_SUCCESS  The entry point registered handler successfully.
  @retval  Other        Some error occurred when executing this entry point.
**/
EFI_STATUS
EFIAPI
StandaloneRasGatewayInitialize (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_MM_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_HANDLE  DispatchHandle;
  EFI_STATUS  Status;
  UINT64      ErrBlockAddr;
  UINT64      ErrBlockSize;
  VOID        *Registration; // This is unused as we do not need to de-register

  ASSERT (SystemTable != NULL);
  mMmst = SystemTable;

  DEBUG ((DEBUG_INFO, "%a\n", __func__));

  Status = mMmst->MmiHandlerRegister (
                    HestErrorSourcesInfoMmiHandler,
                    &gMmHestGetErrorSourceInfoGuid,
                    &DispatchHandle
                    );
  if (EFI_ERROR (Status)) {
    DEBUG (
      (
       DEBUG_ERROR,
       "%a: Mmi handler registration failed with status : %r\n",
       __FUNCTION__,
       Status
      )
      );
    return Status;
  }

  ErrBlockAddr = PcdGet64 (PcdRasErrBlockAddr);
  ErrBlockSize = PcdGet64 (PcdRasErrBlockSize);

  ZeroMem ((VOID *)ErrBlockAddr, ErrBlockSize);
  AcpiGhesInit (ErrBlockAddr, ErrBlockSize);
  Status = mMmst->MmRegisterProtocolNotify (
                    &gMmHestErrorSourceDescProtocolGuid,
                    Err_Source_Init,
                    &Registration
                    );
  DEBUG ((DEBUG_INFO, "StandaloneMmHestErrorSourceInitialize!\n"));
  return Status;
}
