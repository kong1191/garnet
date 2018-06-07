// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/object_set.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

bool TipcObjectSet::in_object_list(fbl::RefPtr<TipcObject> obj) {
  fbl::AutoLock lock(&object_list_lock_);
  auto it = object_list_.find_if([&obj](const TipcObject& o) {
    return (obj->handle_id() == o.handle_id());
  });
  return (it != object_list_.end());
}

zx_status_t TipcObjectSet::AddObject(fbl::RefPtr<TipcObject> object) {
  if (in_object_list(object)) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  zx_status_t err = object->BeginAsyncWait(port_);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to async wait on port " << err;
    return err;
  }

  fbl::AutoLock lock(&object_list_lock_);
  object_list_.push_back(object);

  return ZX_OK;
}

void TipcObjectSet::RemoveObject(fbl::RefPtr<TipcObject> object) {
  if (in_object_list(object)) {
    object->CancelAsyncWait(port_);

    fbl::AutoLock lock(&object_list_lock_);
    object_list_.erase(*object);
  }
}

zx_status_t TipcObjectSet::Wait(WaitResult* result, zx::time deadline) {
  FXL_DCHECK(result);
  zx_port_packet_t packet;

  zx_status_t err = port_.wait(fbl::move(deadline), &packet);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on port " << err;
    return err;
  }

  uint32_t handle_id = packet.key;
  uint32_t observed = packet.signal.observed;

  fbl::RefPtr<TipcObject> obj;
  err = TipcObjectManager::Instance()->GetObject(handle_id, &obj);
  FXL_CHECK(err == ZX_OK);

  err = obj->ClearEvent(observed);
  FXL_CHECK(err == ZX_OK);

  result->handle_id = handle_id;
  result->event = observed;
  result->cookie = obj->cookie();

  return ZX_OK;
}

}  // namespace ree_agent
