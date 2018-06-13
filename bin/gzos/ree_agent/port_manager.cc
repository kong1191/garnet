#include "garnet/bin/gzos/ree_agent/port_manager.h"

#include "lib/fidl/cpp/binding.h"
#include "lib/ree_agent/cpp/channel.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

zx_status_t TipcPortTableEntry::Connect(
    fbl::RefPtr<TipcChannelImpl>* channel_out) {
  fbl::RefPtr<TipcChannelImpl> channel;
  zx_status_t err = TipcChannelImpl::Create(num_items_, item_size_, &channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "failed to create tipc channel " << err;
    return err;
  }

  err = TipcObjectManager::Instance()->AddObject(channel);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "failed to add object to object manager " << err;
    return err;
  }

  fidl::InterfaceHandle<TipcChannel> peer_handle;
  auto handle = channel->GetInterfaceHandle();
  port_->Connect(fbl::move(handle), &err, &peer_handle);
  if (err != ZX_OK) {
    return err;
  }
  channel->BindPeerInterfaceHandle(fbl::move(peer_handle));

  *channel_out = fbl::move(channel);
  return ZX_OK;
}

void TipcPortManagerImpl::Publish(fidl::InterfaceHandle<TipcPort> port,
                                  TipcPortInfo info, PublishCallback callback) {
  auto entry = fbl::make_unique<TipcPortTableEntry>(info, fbl::move(port));
  if (!entry) {
    callback(ZX_ERR_NO_MEMORY);
  }
  {
    fbl::AutoLock lock(&port_table_lock_);
    if (port_table_.insert_or_find(fbl::move(entry))) {
      callback(ZX_OK);
    } else {
      callback(ZX_ERR_ALREADY_EXISTS);
    }
  }
}

zx_status_t TipcPortManagerImpl::Find(fbl::String path,
                                      TipcPortTableEntry*& entry_out) {
  fbl::AutoLock lock(&port_table_lock_);
  auto iter = port_table_.find(path);

  if (!iter.IsValid()) {
    entry_out = nullptr;
    return ZX_ERR_NOT_FOUND;
  }

  entry_out = &(*iter);
  return ZX_OK;
}

}  // namespace ree_agent
