// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"

namespace ree_agent {

zx_status_t GzIpcAgent::SendMessageToPeer(uint32_t local, uint32_t remote,
                                          void* data, size_t data_len) {
  size_t msg_size = sizeof(gz_ipc_msg_hdr) + data_len;
  fbl::unique_ptr<char> buf(new char[msg_size]);

  if (buf == nullptr)
    return ZX_ERR_NO_MEMORY;

  auto hdr = reinterpret_cast<gz_ipc_msg_hdr*>(buf.get());
  hdr->src = local;
  hdr->dst = remote;
  hdr->reserved = 0;
  hdr->len = data_len;
  hdr->flags = 0;
  memcpy(hdr->data, data, data_len);

  return Write(buf.get(), msg_size);
}

void GzIpcAgent::FreeEndpoint(uint32_t local_addr) {
  fbl::AutoLock lock(&lock_);

  auto it = endpoint_table_.find(local_addr);
  if (it != endpoint_table_.end()) {
    auto& endpoint = it->second;
    if (endpoint->remote_addr() != kInvalidEndpointAddress) {
      disc_req_msg disc_req{
          {CtrlMessageType::DISCONNECT_REQUEST, sizeof(gz_ipc_disc_req_body)},
          {endpoint->remote_addr()}};

      FXL_CHECK(SendMessageToPeer(local_addr, kCtrlEndpointAddress, &disc_req,
                                  sizeof(disc_req)) == ZX_OK);
    }
    endpoint_table_.erase(it);
  }

  id_allocator_.Free(local_addr);
}

zx_status_t GzIpcAgent::HandleConnectResponse(
    fbl::unique_ptr<GzIpcEndpoint>& endpoint, void* msg, size_t msg_len) {
  if (msg_len != sizeof(conn_rsp_msg)) {
    FXL_LOG(ERROR) << "Invalid conn rsp msg";
    return ZX_ERR_INTERNAL;
  }

  auto conn_rsp = reinterpret_cast<conn_rsp_msg*>(msg);
  if (conn_rsp->hdr.type != CtrlMessageType::CONNECT_RESPONSE) {
    FXL_LOG(ERROR) << "Invalid conn rsp msg";
    return ZX_ERR_INTERNAL;
  }

  if (conn_rsp->hdr.body_len != sizeof(gz_ipc_conn_rsp_body)) {
    FXL_LOG(ERROR) << "Invalid body length";
    return ZX_ERR_INTERNAL;
  }

  zx_status_t status = conn_rsp->body.status;
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Connect request failed, status=" << status;
    return status;
  }

  endpoint->set_remote_addr(conn_rsp->body.target);
  return ZX_OK;
}

zx_status_t GzIpcAgent::HandleEndpointMessage(gz_ipc_msg_hdr* msg_hdr) {
  fbl::AutoLock lock(&lock_);

  auto it = endpoint_table_.find(msg_hdr->dst);
  if (it != endpoint_table_.end()) {
    auto& endpoint = it->second;

    if (endpoint->remote_addr() == kInvalidEndpointAddress) {
      zx_status_t status =
          HandleConnectResponse(endpoint, msg_hdr->data, msg_hdr->len);
      if (status != ZX_OK) {
        endpoint_table_.erase(msg_hdr->dst);
        id_allocator_.Free(msg_hdr->dst);
      }
      return ZX_OK;

    } else if (endpoint->remote_addr() == msg_hdr->src) {
      auto endpoint_msg_hdr =
          reinterpret_cast<gz_ipc_endpoint_msg_hdr*>(msg_hdr->data);
      auto payload = endpoint_msg_hdr + 1;
      size_t payload_size = msg_hdr->len - sizeof(gz_ipc_endpoint_msg_hdr);

      return endpoint->Write(payload, payload_size);
    }
  }

  FXL_LOG(ERROR) << "Endpoint addr " << msg_hdr->src
                 << " not found, msg dropped";
  return ZX_OK;
}

zx_status_t GzIpcAgent::HandleMessage(void* buf, size_t msg_size) {
  if (msg_size < sizeof(gz_ipc_msg_hdr)) {
    FXL_LOG(ERROR) << "Invalid msg";
    return ZX_ERR_INTERNAL;
  }

  auto msg_hdr = reinterpret_cast<gz_ipc_msg_hdr*>(buf);

  if (msg_hdr->dst == kCtrlEndpointAddress) {
    return HandleCtrlMessage(msg_hdr);
  }

  return HandleEndpointMessage(msg_hdr);
}

zx_status_t GzIpcAgent::HandleDisconnectRequest(gz_ipc_ctrl_msg_hdr* ctrl_hdr) {
  FXL_CHECK(ctrl_hdr);

  if (ctrl_hdr->body_len != sizeof(gz_ipc_disc_req_body)) {
    FXL_LOG(ERROR) << "Invalid disc req msg";
    return ZX_ERR_INTERNAL;
  }

  fbl::AutoLock lock(&lock_);
  auto body = reinterpret_cast<gz_ipc_disc_req_body*>(ctrl_hdr + 1);

  endpoint_table_.erase(body->target);
  id_allocator_.Free(body->target);
  return ZX_OK;
}

}  // namespace ree_agent
