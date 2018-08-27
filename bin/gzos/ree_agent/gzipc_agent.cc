// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

#include "garnet/bin/gzos/ree_agent/gzipc_agent.h"

namespace ree_agent {

zx_status_t GzipcAgent::AllocateEndpoint(uint32_t remote_addr,
                                         zx::channel channel,
                                         uint32_t* local_addr_out) {
  FXL_DCHECK(local_addr_out);

  fbl::AutoLock lock(&lock_);
  uint32_t local_addr;
  zx_status_t err = id_allocator_.Alloc(&local_addr);
  if (err != ZX_OK) {
    return err;
  }

  auto endpoint = fbl::make_unique<GzipcEndpoint>(this, local_addr, remote_addr,
                                                  fbl::move(channel));
  if (!endpoint) {
    return ZX_ERR_NO_MEMORY;
  }
  ep_table_[local_addr] = fbl::move(endpoint);

  *local_addr_out = local_addr;
  return ZX_OK;
}

GzipcEndpoint* GzipcAgent::LookupEndpoint(uint32_t local_addr) {
  fbl::AutoLock lock(&lock_);

  if (local_addr < kMaxEndpointNumber && id_allocator_.InUse(local_addr)) {
    return ep_table_[local_addr].get();
  }
  return nullptr;
}

void GzipcAgent::FreeEndpoint(uint32_t local_addr) {
  fbl::AutoLock lock(&lock_);

  if (local_addr < kMaxEndpointNumber && id_allocator_.InUse(local_addr)) {
    id_allocator_.Free(local_addr);
    ep_table_[local_addr].reset();
  }
}

GzipcAgent::GzipcAgent(uint32_t id, zx::channel ch, size_t max_msg_size,
                       TaServices& service_provider)
    : ReeAgent(id, std::move(ch), max_msg_size),
      ta_service_provider_(service_provider) {}

zx_status_t GzipcAgent::SendMessageToRee(uint32_t local, uint32_t remote,
                                         void* data, size_t data_len) {
  size_t msg_size = sizeof(gzipc_msg_hdr) + data_len;
  fbl::unique_ptr<char> buf(new char[msg_size]);

  if (buf == nullptr)
    return ZX_ERR_NO_MEMORY;

  auto hdr = reinterpret_cast<gzipc_msg_hdr*>(buf.get());
  hdr->src = local;
  hdr->dst = remote;
  hdr->reserved = 0;
  hdr->len = data_len;
  hdr->flags = 0;
  memcpy(hdr->data, data, data_len);

  return Write(buf.get(), msg_size);
}

zx_status_t GzipcAgent::Start() { return ReeAgent::Start(); }

zx_status_t GzipcAgent::Stop() {
  zx_status_t status = ReeAgent::Stop();

  if (status == ZX_ERR_BAD_STATE) {
    // ignore error if already stopped.
    return ZX_OK;
  }

  return status;
}

zx_status_t GzipcAgent::HandleMessage(void* buf, size_t size) {
  FXL_DCHECK(buf);

  if (size < sizeof(gzipc_msg_hdr)) {
    FXL_LOG(ERROR) << "message buffer size too small, size=" << size;
    return ZX_ERR_INVALID_ARGS;
  }

  auto hdr = reinterpret_cast<gzipc_msg_hdr*>(buf);

  if (size != (sizeof(gzipc_msg_hdr) + hdr->len)) {
    FXL_LOG(ERROR) << "invalid message length, length=" << size;
    return ZX_ERR_INVALID_ARGS;
  }

  return (hdr->dst == kCtrlEndpointAddress) ? HandleCtrlMessage(hdr)
                                            : HandleEndpointMessage(hdr);
}

zx_status_t GzipcAgent::HandleCtrlMessage(gzipc_msg_hdr* hdr) {
  FXL_DCHECK(hdr);

  auto ctrl_msg = reinterpret_cast<gzipc_ctrl_msg_hdr*>(hdr->data);
  auto msg_body = reinterpret_cast<void*>(ctrl_msg + 1);

  switch (ctrl_msg->type) {
    case CtrlMessageType::CONNECT_REQUEST:
      if (ctrl_msg->body_len != sizeof(gzipc_conn_req_body)) {
        break;
      }
      return HandleConnectRequest(hdr->src, msg_body);

    case CtrlMessageType::DISCONNECT_REQUEST:
      if (ctrl_msg->body_len != sizeof(gzipc_disc_req_body)) {
        break;
      }
      return HandleDisconnectRequest(hdr->src, msg_body);

    default:
      break;
  }

  FXL_LOG(ERROR) << "invalid ctrl message, type="
                 << static_cast<uint32_t>(ctrl_msg->type)
                 << ", body_len=" << ctrl_msg->body_len;
  return ZX_ERR_INVALID_ARGS;
}

zx_status_t GzipcAgent::HandleConnectRequest(uint32_t remote_addr, void* req) {
  zx_status_t status;
  uint32_t local_addr = 0;

  auto send_reply_msg =
      fbl::MakeAutoCall([this, &status, &remote_addr, &local_addr]() {
        uint32_t err = static_cast<uint32_t>(status);
        conn_rsp_msg res{
            {CtrlMessageType::CONNECT_RESPONSE, sizeof(gzipc_conn_rsp_body)},
            {remote_addr, err, local_addr}};
        FXL_CHECK(SendMessageToRee(kCtrlEndpointAddress, kCtrlEndpointAddress,
                                   &res, sizeof(res)) == ZX_OK);
      });

  zx::channel h1, h2;
  status = zx::channel::create(0, &h1, &h2);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create channel pair, status=" << status;
    return status;
  }

  auto conn_req = reinterpret_cast<gzipc_conn_req_body*>(req);
  ta_service_provider_.ConnectToService(std::move(h1), conn_req->name);

  status = AllocateEndpoint(remote_addr, fbl::move(h2), &local_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to allocate endpoint slot, status=" << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t GzipcAgent::HandleDisconnectRequest(uint32_t src_addr, void* req) {
  FXL_DCHECK(req);

  auto disc_req = reinterpret_cast<gzipc_disc_req_body*>(req);
  uint32_t local_addr = disc_req->target;

  FreeEndpoint(local_addr);

  return ZX_OK;
}

zx_status_t GzipcAgent::HandleEndpointMessage(gzipc_msg_hdr* hdr) {
  FXL_DCHECK(hdr);

  GzipcEndpoint* ep = LookupEndpoint(hdr->dst);
  if (ep && (ep->remote_addr() == hdr->src)) {
    void* msg = reinterpret_cast<void*>(hdr->data);

    return ep->Write(msg, hdr->len);
  }

  return ZX_ERR_NOT_FOUND;
}

}  // namespace ree_agent
