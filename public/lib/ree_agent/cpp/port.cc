// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/port.h"
#include "lib/ree_agent/cpp/channel.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

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

void TipcPortImpl::GetInfo(GetInfoCallback callback) {
  callback(num_items_, item_size_);
}

zx_status_t TipcPortImpl::Accept(fbl::RefPtr<TipcChannelImpl>* channel_out) {
  FXL_DCHECK(channel_out);
  if (pending_requests_.is_empty()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  auto channel = pending_requests_.pop_front();

  zx_status_t err = TipcObjectManager::Instance()->InstallObject(channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to install channel object: " << err;
    return err;
  }
  channel->NotifyReady();

  *channel_out = fbl::move(channel);
  return ZX_OK;
}

}  // namespace ree_agent
