/** @file
  This module provides communication with the RAS Agent over RPMI or MPXY.
  Depending on platform configuration, it sends commands via SBI MPXY or MM communication.

  Copyright (c) 2024, Ventana Micro Systems, Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Base.h>
#include <Uefi.h>

#include <IndustryStandard/Acpi.h>

#include <Protocol/AcpiTable.h>
#include <Guid/EventGroup.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/SafeIntLib.h>
#include <Library/BaseRiscVSbiLib.h>

#include <Protocol/FdtClient.h>
#include <Protocol/MmCommunication2.h>

#include <Library/DxeRiscvMpxy.h>
#include <Library/DxeRiscvRasAgentClient.h>

//
// Global static buffers for responses
//
STATIC ErrorSourceListResp  gErrorSourceListResp;
STATIC ErrDescResp          gErrDescResp;
STATIC EFI_STATUS (EFIAPI *gSendCommand)(VOID *CommBuffer, UINTN CmdLen, UINTN *RespLen, UINT8 FuncId);

//
// Global MPXY channel ID for RAS Agent communication
//
UINT32 gMpxyChannelId = 0;

///
/// Size of MM communicate header without including the payload.
///
#define MM_COMMUNICATE_HEADER_SIZE  (OFFSET_OF (EFI_MM_COMMUNICATE_HEADER, Data))

///
/// Pointer to MM Communication Protocol
///
STATIC EFI_MM_COMMUNICATION2_PROTOCOL  *mMmCommunication2 = NULL;

/**
  Probes all MPXY channels to find the one associated with RAS services.

  @param[out] ChannelId    The identified RAS MPXY channel ID.

  @retval EFI_SUCCESS           Channel ID found and returned.
  @retval EFI_UNSUPPORTED       No matching channel found.
  @retval EFI_DEVICE_ERROR      Failure in channel attribute read.
**/
STATIC
EFI_STATUS
EFIAPI
ProbeRasAgentMpxyChannelId (
  OUT UINT32  *ChannelId
  )
{
  UINTN       ChannelList[MAX_MPXY_CHANNELS];
  UINTN       Returned, Remaining, StartIndex = 0;
  EFI_STATUS  Status;
  BOOLEAN     Found = FALSE;
  BOOLEAN     ParsingDone = FALSE;
  UINTN       i, Id;
  UINT32      RasSrvGroup;

  while (!ParsingDone) {
    Status = SbiMpxyGetChannelList (
               StartIndex,
               ChannelList,
               &Remaining,
               &Returned
               );

    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Returned == 0) {
      return EFI_UNSUPPORTED;
    }

    for (i = 0; i < Returned; i++) {
      Id = ChannelList[i];
      Status = SbiMpxyReadChannelAttrs (
                 Id,
                 BASE_ATTR_ID,
                 1,
                 &RasSrvGroup
                 );

      if (EFI_ERROR (Status)) {
        continue;
      }

      if (RasSrvGroup == RAS_SERVICE_GROUP) {
        Found       = TRUE;
        ParsingDone = TRUE;
        break;
      }
    }

    if (Remaining) {
      StartIndex += Returned;
    } else {
      ParsingDone = TRUE;
    }
  }

  if (Found) {
    *ChannelId = Id;
    DEBUG ((DEBUG_INFO, "Found RAS MPXY channel: %x\n", Id));
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

/**
  Sends a RAS command to the SMM handler via MM communication.

  @param[in,out] CommBuffer   Command and response buffer.
  @param[in]     CmdLen       Command length.
  @param[out]    RespLen      Length of response received.
  @param[in]     FuncId       Function ID for the RAS operation.

  @retval EFI_SUCCESS         Command sent and response received successfully.
  @retval EFI_OUT_OF_RESOURCES  Allocation failure.
**/
STATIC
EFI_STATUS
EFIAPI
RacSendMMCommand (
  VOID   *CommBuffer,
  UINTN  CmdLen,
  UINTN  *RespLen,
  UINT8  FuncId
  )
{
  EFI_STATUS                 Status;
  UINTN                      CommBufferSize;
  EFI_MM_COMMUNICATE_HEADER  *SmmCommunicateHeader;

  CommBufferSize       = MM_COMMUNICATE_HEADER_SIZE + *RespLen;
  SmmCommunicateHeader = AllocateZeroPool (CommBufferSize);
  if (SmmCommunicateHeader == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyGuid (&SmmCommunicateHeader->HeaderGuid, &gMmHestGetErrorSourceInfoGuid);
  CopyMem (SmmCommunicateHeader->Data, (const void *)CommBuffer, *RespLen);
  SmmCommunicateHeader->MessageLength = *RespLen;

  Status = mMmCommunication2->Communicate (
                                mMmCommunication2,
                                SmmCommunicateHeader,
                                SmmCommunicateHeader,
                                &CommBufferSize
                                );

  *RespLen = CommBufferSize - MM_COMMUNICATE_HEADER_SIZE;
  CopyMem (CommBuffer, SmmCommunicateHeader->Data, *RespLen);
  FreePool (SmmCommunicateHeader);

  return Status;
}

/**
  Sends a RAS command using SBI MPXY messaging.

  @param[in,out] CommBuffer   Buffer for both command and response.
  @param[in]     CmdLen       Length of command data.
  @param[out]    RespLen      Length of response data.
  @param[in]     FuncId       Function ID for the command.

  @retval EFI_SUCCESS         Communication successful.
**/
STATIC
EFI_STATUS
EFIAPI
RacSendPassThroughCommand (
  VOID   *CommBuffer,
  UINTN  CmdLen,
  UINTN  *RespLen,
  UINT8  FuncId
  )
{
  return SbiMpxySendMessage (
           gMpxyChannelId,
           FuncId,
           CommBuffer,
           CmdLen,
           CommBuffer,
           RespLen
           );
}

/**
  Retrieves the RAS Agent MPXY Channel ID.

  @param[out] ChannelId    Pointer to receive the Channel ID.

  @retval EFI_SUCCESS      Channel ID retrieved.
**/
EFI_STATUS
EFIAPI
GetRasAgentMpxyChannelId (
  OUT UINT32  *ChannelId
  )
{
  return ProbeRasAgentMpxyChannelId (ChannelId);
}

/**
  Initializes the RAS Agent communication.

  Selects either MM communication or SBI MPXY messaging based on PCD.

  @retval EFI_SUCCESS       Initialization successful.
  @retval EFI_NOT_READY     Required channel or SBI setup failed.
**/
EFI_STATUS
EFIAPI
RacInit (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!PcdGetBool (PcdMMPassThroughEnable)) {
    Status = gBS->LocateProtocol (
                   &gEfiMmCommunication2ProtocolGuid,
                   NULL,
                   (VOID **)&mMmCommunication2
                   );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    gSendCommand = RacSendMMCommand;
  } else {
    Status = GetRasAgentMpxyChannelId (&gMpxyChannelId);
    if (EFI_ERROR (Status)) {
      return EFI_NOT_READY;
    }

    Status = SbiMpxyInit ();
    if (EFI_ERROR (Status)) {
      return EFI_NOT_READY;
    }

    gSendCommand = RacSendPassThroughCommand;
  }

  return EFI_SUCCESS;
}

/**
  Queries the number of RAS error sources from the agent.

  @param[out] NumErrorSources   Pointer to return the number of error sources.

  @retval EFI_SUCCESS           Successfully queried.
  @retval EFI_DEVICE_ERROR      Agent returned failure.
**/
EFI_STATUS
EFIAPI
RacGetNumberErrorSources (
  UINT32  *NumErrorSources
  )
{
  struct __packed32 {
    RasRpmiRespHeader  RespHdr;
    UINT32             NumErrorSources;
  } RasMsgBuf;

  EFI_STATUS         Status;
  UINTN              RespLen = sizeof (RasMsgBuf);
  RasRpmiRespHeader  *RespHdr = &RasMsgBuf.RespHdr;

  ZeroMem (&RasMsgBuf, sizeof (RasMsgBuf));
  RespHdr->func_id = RAS_GET_NUM_ERR_SRCS;
  Status = gSendCommand (&RasMsgBuf, 0, &RespLen, RAS_GET_NUM_ERR_SRCS);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (RespHdr->status != 0) {
    return EFI_DEVICE_ERROR;
  }

  *NumErrorSources = RasMsgBuf.NumErrorSources;

  return EFI_SUCCESS;
}

/**
  Retrieves the list of RAS error source IDs.

  @param[out] ErrorSourceList   Pointer to the array of source IDs.
  @param[out] NumSources        Number of sources returned.

  @retval EFI_SUCCESS           List successfully retrieved.
  @retval EFI_INVALID_PARAMETER NULL parameter.
**/
EFI_STATUS
EFIAPI
RacGetErrorSourceIDList (
  OUT UINT32  **ErrorSourceList,
  OUT UINT32  *NumSources
  )
{
  UINT32             *RespData = gErrorSourceListResp.ErrSourceList;
  RasRpmiRespHeader  *RespHdr  = &gErrorSourceListResp.RespHdr;
  EFI_STATUS          Status;
  UINTN               RespLen = sizeof (gErrorSourceListResp);

  ZeroMem (&gErrorSourceListResp, sizeof (gErrorSourceListResp));

  if (ErrorSourceList == NULL) {
    return EFI_INVALID_PARAMETER;
  }
  gErrorSourceListResp.RespHdr.func_id = RAS_GET_ERR_SRCS_ID_LIST;
  Status = gSendCommand (&gErrorSourceListResp, 0, &RespLen, RAS_GET_ERR_SRCS_ID_LIST);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (RespHdr->status != 0) {
    return EFI_DEVICE_ERROR;
  }

  *NumSources      = RespHdr->returned;
  *ErrorSourceList = RespData;

  return EFI_SUCCESS;
}

/**
  Retrieves the descriptor for a specific RAS error source.

  @param[in]  SourceID              Error source ID.
  @param[out] DescriptorType        Type of descriptor (ACPI, Firmware, etc.).
  @param[out] ErrorDescriptor       Pointer to the descriptor data.
  @param[out] ErrorDescriptorSize   Size of the descriptor in bytes.

  @retval EFI_SUCCESS               Descriptor retrieved successfully.
  @retval EFI_DEVICE_ERROR          Invalid data or agent reported failure.
**/
EFI_STATUS
EFIAPI
RacGetErrorSourceDescriptor (
  IN UINT32   SourceID,
  OUT UINTN   *DescriptorType,
  OUT VOID    **ErrorDescriptor,
  OUT UINT32  *ErrorDescriptorSize
  )
{
  UINTN              RespLen = sizeof (gErrDescResp);
  EFI_STATUS         Status;
  RasRpmiRespHeader  *RspHdr = &gErrDescResp.RspHdr;
  UINT8              *Desc   = gErrDescResp.desc;

  ZeroMem (&gErrDescResp, sizeof (gErrDescResp));

  *Desc = (UINT8)SourceID;
  gErrDescResp.RspHdr.func_id = RAS_GET_ERR_SRC_DESC;

  Status = gSendCommand (&gErrDescResp, sizeof (SourceID), &RespLen, RAS_GET_ERR_SRC_DESC);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((RspHdr->status != 0) || (RspHdr->remaining != 0)) {
    return EFI_DEVICE_ERROR;
  }

  *DescriptorType     = RspHdr->flags & ERROR_DESCRIPTOR_TYPE_MASK;
  ASSERT (*DescriptorType < MAX_ERROR_DESCRIPTOR_TYPES);
  *ErrorDescriptor     = (VOID *)Desc;
  *ErrorDescriptorSize = RspHdr->returned;

  return EFI_SUCCESS;
}
