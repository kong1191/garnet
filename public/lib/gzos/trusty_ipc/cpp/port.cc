// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "lib/fxl/random/uuid.h"
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

  pending_requests_.erase_if(
      [ch](const TipcChannelImpl& ref) { return (ch.get() == &ref); });
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
                           fidl::StringPtr uuid, ConnectCallback callback) {
  fbl::RefPtr<TipcChannelImpl> channel;
  zx_status_t err = TipcChannelImpl::Create(num_items_, item_size_, &channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create channel object " << err;
    callback(err, nullptr);
    return;
  }

  if (uuid.is_null()) {
    // check access for non-secure client
    if (!(flags_ & IPC_PORT_ALLOW_NS_CONNECT)) {
      callback(ZX_ERR_ACCESS_DENIED, nullptr);
      return;
    }
  } else {
    // check access for secure client
    if (!(flags_ & IPC_PORT_ALLOW_TA_CONNECT)) {
      callback(ZX_ERR_ACCESS_DENIED, nullptr);
      return;
    }

    if (!fxl::IsValidUUID(uuid.get())) {
      callback(ZX_ERR_INVALID_ARGS, nullptr);
      return;
    }

    void* cookie = new std::string(uuid.get());
    if (!cookie) {
      callback(ZX_ERR_NO_MEMORY, nullptr);
      return;
    }
    channel->set_cookie(cookie);
  }

  auto handle_hup = [this, channel] {
    channel->Shutdown();
    RemoveFromPendingRequest(channel);
  };

  channel->SetCloseCallback([handle_hup] {
    async::PostTask(async_get_default(), [handle_hup] { handle_hup(); });
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

zx_status_t TipcPortImpl::Accept(std::string* uuid_out,
                                 fbl::RefPtr<TipcChannelImpl>* channel_out) {
  FXL_DCHECK(channel_out);
  auto channel = GetPendingRequest();
  if (channel == nullptr) {
    return ZX_ERR_SHOULD_WAIT;
  }

  if (!HasPendingRequests()) {
    ClearEvent(TipcEvent::READY);
  }

  void* cookie = channel->cookie();
  if (cookie) {
    auto uuid = reinterpret_cast<std::string*>(cookie);
    FXL_DCHECK(fxl::IsValidUUID(*uuid));

    if (uuid_out) {
      uuid_out->assign(uuid->c_str());
    }
    delete uuid;
  }

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

void TipcPortImpl::Shutdown() {
  if (binding_.is_bound()) {
    binding_.Unbind();
  }

  fbl::AutoLock lock(&mutex_);
  pending_requests_.clear();

  TipcObject::Shutdown();
}

}  // namespace trusty_ipc
