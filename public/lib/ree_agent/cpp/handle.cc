// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/fxl/logging.h"
#include "lib/ree_agent/cpp/handle.h"
#include "lib/ree_agent/cpp/object.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

TipcHandle::TipcHandle(uint32_t id, fbl::unique_ptr<TipcObject> object)
    : id_(id), object_(fbl::move(object)) {}

zx_status_t TipcHandle::BeginAsyncWait(zx_handle_t port_handle) {
  zx_handle_t event_handle = object_->event();
  return zx_object_wait_async(event_handle, port_handle, id_, TipcEvent::ALL,
                              ZX_WAIT_ASYNC_REPEATING);
}

void TipcHandle::CancelAsyncWait(zx_handle_t port_handle) {
  zx_handle_t event_handle = object_->event();
  zx_status_t status = zx_port_cancel(port_handle, event_handle, id_);
  FXL_CHECK(status == ZX_OK);
}

}  // namespace ree_agent
