// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/port.h"
#include "lib/ree_agent/cpp/channel.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

zx_status_t TipcPortImpl::Publish() {
  zx_status_t status;
  fidl::InterfaceHandle<TipcPort> handle;

  binding_.Bind(handle.NewRequest());

  TipcPortInfo info;
  info.path = path_;
  info.num_items = num_items_;
  info.item_size = item_size_;

  port_mgr_->Publish(std::move(handle), info, &status);
  return status;
}

void TipcPortImpl::Connect(fidl::InterfaceHandle<TipcChannel> peer_handle,
                           ConnectCallback callback) {
  fbl::RefPtr<TipcChannelImpl> channel;
  zx_status_t err = TipcChannelImpl::Create(num_items_, item_size_, &channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel object " << err;
    callback(err, nullptr);
  }

  channel->BindPeerInterfaceHandle(std::move(peer_handle));
  pending_requests_.push_front(channel);

  err = SignalEvent(TipcEvent::READY);
  FXL_DCHECK(err == ZX_OK);

  auto local_handle = channel->GetInterfaceHandle();
  callback(ZX_OK, std::move(local_handle));
}

zx_status_t TipcPortImpl::Accept(fbl::RefPtr<TipcChannelImpl>* channel_out) {
  FXL_DCHECK(channel_out);
  if (pending_requests_.is_empty()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  auto channel = pending_requests_.pop_front();

  // TODO(sy): send the connection response message here
  uint32_t dummy;
  zx_status_t status = channel->SendMessage(&dummy, sizeof(dummy));
  if (status != ZX_OK) {
    return status;
  }

  zx_status_t err = TipcObjectManager::Instance()->AddObject(channel);
  FXL_CHECK(err == ZX_OK);

  *channel_out = fbl::move(channel);
  return ZX_OK;
}

}  // namespace ree_agent
