// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/object.h"

namespace ree_agent {

IdAllocator<kMaxObject> TipcObject::id_allocator_;

TipcObject::TipcObject() {
  zx_status_t err = zx::event::create(0, &event_);
  FXL_CHECK(err == ZX_OK);
}

zx_status_t TipcObject::Init() {
  fbl::AutoLock lock(&initialize_lock_);
  if (!initialized_) {
    uint32_t new_id;
    zx_status_t err = id_allocator_.Alloc(&new_id);
    if (err != ZX_OK) {
      return err;
    }

    id_ = new_id;
    initialized_ = true;
  }
  return ZX_OK;
}

TipcObject::~TipcObject() {
  if (initialized_) {
    zx_status_t err = id_allocator_.Free(id_);
    FXL_CHECK(err == ZX_OK);
  }
}

zx_status_t TipcObject::BeginAsyncWait(zx::port& port) {
  return event_.wait_async(port, id_, TipcEvent::ALL, ZX_WAIT_ASYNC_REPEATING);
}

void TipcObject::CancelAsyncWait(zx::port& port) {
  zx_status_t status = port.cancel(event_.get(), id_);
  FXL_CHECK(status == ZX_OK);
}

zx_status_t TipcObject::Wait(zx_signals_t* event_out, zx::time deadline) {
  FXL_DCHECK(event_out);

  zx_signals_t observed;
  zx_status_t err = zx_object_wait_one(event_.get(), TipcEvent::ALL,
                                       deadline.get(), &observed);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for event " << err;
    return err;
  }

  err = ClearEvent(observed);
  FXL_DCHECK(err == ZX_OK);
  *event_out = observed;

  return ZX_OK;
}

}  // namespace ree_agent
