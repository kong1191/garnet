#include "garnet/bin/gzos/ree_agent/trusty_ipc.h"

namespace ree_agent {

void TipcPortManagerImpl::Publish(
    fidl::StringPtr port_path,
    fidl::InterfaceHandle<TipcPortListener> listener,
    PublishCallback callback) {
  auto entry = fbl::make_unique<TipcPortTableEntry>(port_path.get(),
                                                    fbl::move(listener));
  if (!entry) {
    callback(Status::NO_MEMORY);
  }
  {
    fbl::AutoLock lock(&port_table_lock_);
    port_table_.insert(fbl::move(entry));
  }

  callback(Status::OK);
}

zx_status_t TipcPortManagerImpl::Find(fbl::String path,
                                      TipcPortTableEntry*& entry) {
  fbl::AutoLock lock(&port_table_lock_);
  auto iter = port_table_.find(path);

  if (!iter.IsValid()) {
    entry = nullptr;
    return ZX_ERR_NOT_FOUND;
  }

  entry = &(*iter);
  return ZX_OK;
}

}  // namespace ree_agent
