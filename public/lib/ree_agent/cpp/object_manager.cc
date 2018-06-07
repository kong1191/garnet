// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>

#include "lib/ree_agent/cpp/id_alloc.h"
#include "lib/ree_agent/cpp/object.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

TipcObjectManager* TipcObjectManager::Instance() {
  static TipcObjectManager* instance;
  static fbl::Mutex instance_lock;
  fbl::AutoLock lock(&instance_lock);

  if (!instance) {
    instance = new TipcObjectManager();
  }

  return instance;
}

zx_status_t TipcObjectManager::AddObject(fbl::unique_ptr<TipcObject> object) {
  FXL_CHECK(object->is_initialized());

  zx_status_t err = object->BeginAsyncWait(port_);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to async wait on port " << err;
    return err;
  }

  uint32_t id = object->id();

  fbl::AutoLock lock(&object_table_lock_);
  FXL_DCHECK(GetObjectLocked(id) == nullptr);
  object_table_[id] = fbl::move(object);

  return ZX_OK;
}

zx_status_t TipcObjectManager::RemoveObject(
    uint32_t id, fbl::unique_ptr<TipcObject>* obj_out) {
  FXL_DCHECK(obj_out);

  fbl::AutoLock lock(&object_table_lock_);
  auto obj = GetObjectLocked(id);
  if (!obj) {
    return ZX_ERR_BAD_HANDLE;
  }
  obj->CancelAsyncWait(port_);

  *obj_out = fbl::move(object_table_[id]);
  return ZX_OK;
}

TipcObject* TipcObjectManager::GetObjectLocked(uint32_t id) {
  FXL_DCHECK(id < kMaxObject);
  return object_table_[id].get();
}

TipcObject* TipcObjectManager::GetObject(uint32_t id) {
  fbl::AutoLock lock(&object_table_lock_);
  return GetObjectLocked(id);
}

zx_status_t TipcObjectManager::Wait(uint32_t* id_out, zx_signals_t* event_out,
                                    zx::time deadline) {
  FXL_DCHECK(id_out);
  FXL_DCHECK(event_out);
  zx_port_packet_t packet;

  zx_status_t err = port_.wait(fbl::move(deadline), &packet);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on port " << err;
    return err;
  }

  uint32_t id = packet.key;
  zx_signals_t observed = packet.signal.observed;

  auto obj = GetObject(id);
  FXL_DCHECK(obj);

  err = obj->ClearEvent(observed);
  FXL_DCHECK(err == ZX_OK);

  *id_out = id;
  *event_out = observed;
  return ZX_OK;
}

}  // namespace ree_agent
