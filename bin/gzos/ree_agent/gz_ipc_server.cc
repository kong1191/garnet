// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

#include "garnet/bin/gzos/ree_agent/gz_ipc_server.h"

namespace ree_agent {

zx_status_t GzIpcServer::HandleConnectRequest(uint32_t remote_addr,
                                              gz_ipc_ctrl_msg_hdr* ctrl_hdr) {
  FXL_CHECK(ctrl_hdr);

  if (ctrl_hdr->body_len != sizeof(gz_ipc_conn_req_body)) {
    FXL_LOG(ERROR) << "Invalid disc req msg";
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status;
  uint32_t local_addr = 0;

  auto send_reply_msg =
      fbl::MakeAutoCall([this, &status, &remote_addr, &local_addr]() {
        uint32_t err = static_cast<uint32_t>(status);
        conn_rsp_msg res{
            {CtrlMessageType::CONNECT_RESPONSE, sizeof(gz_ipc_conn_rsp_body)},
            {remote_addr, err, local_addr}};
        FXL_CHECK(SendMessageToPeer(kCtrlEndpointAddress, remote_addr, &res,
                                    sizeof(res)) == ZX_OK);
      });

  zx::channel h1, h2;
  status = zx::channel::create(0, &h1, &h2);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create channel pair, status=" << status;
    return status;
  }

  status = id_allocator_.Alloc(&local_addr);
  if (status != ZX_OK) {
    return status;
  }
  auto free_local_addr = fbl::MakeAutoCall(
      [this, &local_addr]() { id_allocator_.Free(local_addr); });

  auto endpoint = fbl::make_unique<GzIpcEndpoint>(this, local_addr, remote_addr,
                                                  std::move(h1));
  if (!endpoint) {
    FXL_LOG(ERROR) << "failed to allocate endpoint, status=" << status;
    return ZX_ERR_NO_MEMORY;
  }

  fbl::AutoLock lock(&lock_);
  endpoint->reader().set_error_handler(
      [this, local_addr] { FreeEndpoint(local_addr); });
  endpoint_table_.emplace(local_addr, std::move(endpoint));

  auto conn_req = reinterpret_cast<gz_ipc_conn_req_body*>(ctrl_hdr + 1);
  ta_service_provider_.ConnectToService(std::move(h2), conn_req->name);

  free_local_addr.cancel();
  return ZX_OK;
}

zx_status_t GzIpcServer::HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr) {
  uint32_t remote = msg_hdr->src;
  uint32_t msg_size = msg_hdr->len;

  if (msg_size < sizeof(gz_ipc_ctrl_msg_hdr)) {
    FXL_LOG(ERROR) << "Invalid ctrl msg";
    return ZX_ERR_INTERNAL;
  }

  auto ctrl_hdr = reinterpret_cast<gz_ipc_ctrl_msg_hdr*>(msg_hdr->data);

  switch (ctrl_hdr->type) {
    case CtrlMessageType::CONNECT_REQUEST:
      return HandleConnectRequest(remote, ctrl_hdr);

    case CtrlMessageType::DISCONNECT_REQUEST:
      return HandleDisconnectRequest(ctrl_hdr);

    default:
      FXL_LOG(ERROR) << "Invalid ctrl msg type";
      return ZX_ERR_INVALID_ARGS;
  }
}

}  // namespace ree_agent
