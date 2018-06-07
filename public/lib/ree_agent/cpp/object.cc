// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/object.h"

namespace ree_agent {

TipcObject::TipcObject() : handle_id_(kInvalidHandle) {
  zx_status_t err = zx::event::create(0, &event_);
  FXL_CHECK(err == ZX_OK);
}

TipcObject::~TipcObject() = default;

zx_status_t TipcObject::BeginAsyncWait(zx::port& port) {
  return event_.wait_async(port, handle_id_, TipcEvent::ALL,
                           ZX_WAIT_ASYNC_REPEATING);
}

void TipcObject::CancelAsyncWait(zx::port& port) {
  zx_status_t status = port.cancel(event_.get(), handle_id_);
  FXL_CHECK(status == ZX_OK);
}

zx_status_t TipcObject::Wait(WaitResult* result, zx::time deadline) {
  FXL_DCHECK(result);

  zx_signals_t observed;
  zx_status_t err = zx_object_wait_one(event_.get(), TipcEvent::ALL,
                                       deadline.get(), &observed);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for event " << err;
    return err;
  }

  err = ClearEvent(observed);
  FXL_DCHECK(err == ZX_OK);
  result->event = observed;
  result->cookie = cookie_;
  result->handle_id = handle_id_;

  return ZX_OK;
}

}  // namespace ree_agent
