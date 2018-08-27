// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <lib/async/cpp/task.h>

#include "garnet/bin/gzos/ree_agent/zircon_agent.h"

namespace ree_agent {

struct conn_rsp_msg {
  struct zircon_ctrl_msg_hdr ctrl_msg;
  struct zircon_conn_rsp_body body;
};

struct disc_req_msg {
  struct zircon_ctrl_msg_hdr ctrl_msg;
  struct zircon_disc_req_body body;
};

zx_status_t ZirconEndpointTable::AllocateSlot(uint32_t remote_addr,
                                              uint32_t* slot_out) {
  FXL_DCHECK(slot_out);

  uint32_t slot;
  zx_status_t err = id_allocator_.Alloc(&slot);
  if (err != ZX_OK) {
    return err;
  }

  auto endpoint = fbl::make_unique<ZirconEndpoint>(remote_addr);
  if (!endpoint) {
    return ZX_ERR_NO_MEMORY;
  }
  table_[slot] = fbl::move(endpoint);

  *slot_out = slot;
  return ZX_OK;
}

ZirconEndpoint* ZirconEndpointTable::Lookup(uint32_t slot) {
  if (slot < kMaxEndpointNumber && id_allocator_.InUse(slot)) {
    return table_[slot].get();
  }
  return nullptr;
}

void ZirconEndpointTable::FreeSlot(uint32_t slot) {
  if (slot < kMaxEndpointNumber && id_allocator_.InUse(slot)) {
    id_allocator_.Free(slot);
    table_[slot].reset();
  }
}

ZirconAgent::ZirconAgent(uint32_t id, zx::channel ch, size_t max_msg_size,
                         ZirconEndpointTable* ep_table)
    : ReeAgent(id, std::move(ch), max_msg_size), ep_table_(ep_table) {}

zx_status_t ZirconAgent::SendMessageToRee(uint32_t local, uint32_t remote,
                                          void* data, size_t data_len) {
  size_t msg_size = sizeof(zircon_msg_hdr) + data_len;
  fbl::unique_ptr<char> buf(new char[msg_size]);

  if (buf == nullptr)
    return ZX_ERR_NO_MEMORY;

  auto hdr = reinterpret_cast<zircon_msg_hdr*>(buf.get());
  hdr->src = local;
  hdr->dst = remote;
  hdr->reserved = 0;
  hdr->len = data_len;
  hdr->flags = 0;
  memcpy(hdr->data, data, data_len);

  return WriteMessage(buf.get(), msg_size);
}

zx_status_t ZirconAgent::Start() { return ReeAgent::Start(); }

zx_status_t ZirconAgent::Stop() {
  zx_status_t status = ReeAgent::Stop();

  if (status == ZX_ERR_BAD_STATE) {
    // ignore error if already stopped.
    return ZX_OK;
  }

  return status;
}

zx_status_t ZirconAgent::HandleMessage(void* buf, size_t size) {
  FXL_DCHECK(buf);

  if (size < sizeof(zircon_msg_hdr)) {
    FXL_LOG(ERROR) << "message buffer size too small, size=" << size;
    return ZX_ERR_INVALID_ARGS;
  }

  auto hdr = reinterpret_cast<zircon_msg_hdr*>(buf);

  if (size != (sizeof(zircon_msg_hdr) + hdr->len)) {
    FXL_LOG(ERROR) << "invalid message length, length=" << size;
    return ZX_ERR_INVALID_ARGS;
  }

  return (hdr->dst == kCtrlEndpointAddress) ? HandleCtrlMessage(hdr)
                                            : HandleEndpointMessage(hdr);
}

zx_status_t ZirconAgent::HandleCtrlMessage(zircon_msg_hdr* hdr) {
  FXL_DCHECK(hdr);

  auto ctrl_msg = reinterpret_cast<zircon_ctrl_msg_hdr*>(hdr->data);
  auto msg_body = reinterpret_cast<void*>(ctrl_msg + 1);

  switch (ctrl_msg->type) {
    case CtrlMessageType::CONNECT_REQUEST:
      return HandleConnectRequest(hdr->src);

    case CtrlMessageType::DISCONNECT_REQUEST:
      if (ctrl_msg->body_len != sizeof(zircon_disc_req_body)) {
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

zx_status_t ZirconAgent::HandleConnectRequest(uint32_t remote_addr) {
  zx_status_t status;
  uint32_t local_addr = 0;

  auto send_reply_msg = fbl::MakeAutoCall([this, &status, &local_addr]() {
    uint32_t err = static_cast<uint32_t>(status);
    conn_rsp_msg res{
        {CtrlMessageType::CONNECT_RESPONSE, sizeof(zircon_conn_rsp_body)},
        {err, local_addr}};
    FXL_CHECK(SendMessageToRee(kCtrlEndpointAddress, kCtrlEndpointAddress, &res,
                               sizeof(res)) == ZX_OK);
  });

  fbl::AutoLock lock(&lock_);

  status = ep_table_->AllocateSlot(remote_addr, &local_addr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "failed to allocate endpoint slot, status=" << status;
    return status;
  }

  return ZX_OK;
}

zx_status_t ZirconAgent::HandleDisconnectRequest(uint32_t src_addr, void* req) {
  FXL_DCHECK(req);

  fbl::AutoLock lock(&lock_);

  auto disc_req = reinterpret_cast<zircon_disc_req_body*>(req);
  uint32_t local_addr = disc_req->target;

  ep_table_->FreeSlot(local_addr);

  return ZX_OK;
}

zx_status_t ZirconAgent::HandleEndpointMessage(zircon_msg_hdr* hdr) {
  FXL_DCHECK(hdr);

  fbl::AutoLock lock(&lock_);

  ZirconEndpoint* ep = ep_table_->Lookup(hdr->dst);
  if (ep && (ep->remote_addr() == hdr->src)) {
    void* msg = reinterpret_cast<void*>(hdr->data);

    return ep->HandleMessage(msg, hdr->len);
  }

  return ZX_ERR_NOT_FOUND;
}

}  // namespace ree_agent
