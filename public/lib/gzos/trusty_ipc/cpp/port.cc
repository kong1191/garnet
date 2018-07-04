// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/gzos/trusty_ipc/cpp/channel.h"
#include "lib/gzos/trusty_ipc/cpp/object_manager.h"
#include "lib/gzos/trusty_ipc/cpp/port.h"

namespace trusty_ipc {

void TipcPortImpl::AddPendingRequest(fbl::RefPtr<TipcChannelImpl> channel) {
  fbl::AutoLock lock(&mutex_);
  pending_requests_.push_back(fbl::move(channel));
}

void TipcPortImpl::RemoveFromPendingRequest(fbl::RefPtr<TipcChannelImpl> ch) {
  fbl::AutoLock lock(&mutex_);

  pending_requests_.erase_if([ch](const TipcChannelImpl& ref) {
    return ref.cookie() == ch->cookie();
  });
}

fbl::RefPtr<TipcChannelImpl> TipcPortImpl::GetPendingRequest() {
  fbl::AutoLock lock(&mutex_);
  return pending_requests_.pop_front();
};

bool TipcPortImpl::HasPendingRequests() {
  fbl::AutoLock lock(&mutex_);
  return !pending_requests_.is_empty();
}

void TipcPortImpl::Connect(fidl::InterfaceHandle<TipcChannel> peer_handle,
                           ConnectCallback callback) {
  fbl::RefPtr<TipcChannelImpl> channel;
  zx_status_t err = TipcChannelImpl::Create(num_items_, item_size_, &channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel object " << err;
    callback(err, nullptr);
  }

  // tipc channel object should be destroyed when client try to close channel
  // and the channel is still not accepted by service program
  void* cookie = reinterpret_cast<void*>(channel.get());
  channel->set_cookie(cookie);

  auto handle_hup = [this, channel] {
    channel->Shutdown();
    RemoveFromPendingRequest(channel);
  };

  channel->SetCloseCallback([handle_hup] {
    async::PostTask(async_get_default(), [handle_hup] {
      handle_hup();
    });
  });

  channel->BindPeerInterfaceHandle(std::move(peer_handle));
  auto local_handle = channel->GetInterfaceHandle();

  AddPendingRequest(fbl::move(channel));

  SignalEvent(TipcEvent::READY);

  callback(ZX_OK, std::move(local_handle));
}

void TipcPortImpl::GetInfo(GetInfoCallback callback) {
  callback(num_items_, item_size_);
}

zx_status_t TipcPortImpl::Accept(fbl::RefPtr<TipcChannelImpl>* channel_out) {
  FXL_DCHECK(channel_out);
  if (!HasPendingRequests()) {
    return ZX_ERR_SHOULD_WAIT;
  }
  auto channel = GetPendingRequest();

  // remove channel close callback and user should take care of
  // channel HUP event by itself
  channel->set_cookie(nullptr);
  channel->SetCloseCallback(nullptr);

  zx_status_t err = TipcObjectManager::Instance()->InstallObject(channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to install channel object: " << err;
    return err;
  }
  channel->NotifyReady();

  *channel_out = fbl::move(channel);
  return ZX_OK;
}

}  // namespace trusty_ipc
