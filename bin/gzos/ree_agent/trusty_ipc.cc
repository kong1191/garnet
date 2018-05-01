#include "garnet/bin/gzos/ree_agent/trusty_ipc.h"

namespace ree_agent {

void TipcPortManagerImpl::Publish(
    fidl::StringPtr port_path,
    fidl::InterfaceHandle<TipcPortListener> listener,
	PublishCallback callback) {
  // TODO(sy): how to handle error here?
  auto entry = fbl::make_unique<TipcPortTableEntry>(port_path.get(),
                                                    fbl::move(listener));
  if (!entry) {
    callback(Status::NO_MEMORY);
  }
  port_table_.insert(fbl::move(entry));
}

zx_status_t TipcPortManagerImpl::Find(fbl::String path,
                                      TipcPortTableEntry*& entry) {
  auto iter = port_table_.find(path);

  if (!iter.IsValid()) {
    entry = nullptr;
    return ZX_ERR_NOT_FOUND;
  }

  entry = &(*iter);
  return ZX_OK;
}

}  // namespace ree_agent
