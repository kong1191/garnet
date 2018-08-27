// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>

#include "garnet/bin/gzos/ree_agent/gz_ipc_client.h"

namespace ree_agent {

zx_status_t GzIpcClient::Connect(std::string service_name,
                                 zx::channel channel) {
  fbl::AutoLock lock(&lock_);

  conn_req_msg req_msg;
  req_msg.hdr.type = CtrlMessageType::CONNECT_REQUEST;
  req_msg.hdr.body_len = sizeof(gz_ipc_conn_req_body);
  strncpy(req_msg.body.name, service_name.c_str(), sizeof(req_msg.body.name));

  uint32_t local_addr;
  zx_status_t status = id_allocator_.Alloc(&local_addr);
  if (status != ZX_OK) {
    return status;
  }
  auto free_local_addr = fbl::MakeAutoCall(
      [this, &local_addr]() { id_allocator_.Free(local_addr); });

  status = SendMessageToPeer(local_addr, kCtrlEndpointAddress, &req_msg,
                             sizeof(req_msg));
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to write connect req: " << status;
    return status;
  }

  auto endpoint = fbl::make_unique<GzIpcEndpoint>(
      this, local_addr, kInvalidEndpointAddress, std::move(channel));
  if (!endpoint) {
    return ZX_ERR_NO_MEMORY;
  }

  endpoint->reader().set_error_handler(
      [this, local_addr]() { FreeEndpoint(local_addr); });
  endpoint_table_.emplace(local_addr, std::move(endpoint));

  free_local_addr.cancel();
  return ZX_OK;
}

zx_status_t GzIpcClient::HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr) {
  uint32_t msg_size = msg_hdr->len;

  if (msg_size < sizeof(gz_ipc_ctrl_msg_hdr)) {
    FXL_LOG(ERROR) << "Invalid ctrl msg";
    return ZX_ERR_INTERNAL;
  }

  auto ctrl_hdr = reinterpret_cast<gz_ipc_ctrl_msg_hdr*>(msg_hdr->data);

  switch (ctrl_hdr->type) {
    case CtrlMessageType::DISCONNECT_REQUEST:
      return HandleDisconnectRequest(ctrl_hdr);

    default:
      FXL_LOG(ERROR) << "Invalid ctrl msg type";
      return ZX_ERR_INVALID_ARGS;
  }
}

}  // namespace ree_agent
