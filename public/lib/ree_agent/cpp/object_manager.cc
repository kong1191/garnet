// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>

#include "lib/ree_agent/cpp/id_alloc.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

static IdAllocator<kMaxHandle> id_allocator_;

TipcObjectManager* TipcObjectManager::Instance() {
  static TipcObjectManager* instance;
  static fbl::Mutex instance_lock;
  fbl::AutoLock lock(&instance_lock);

  if (!instance) {
    instance = new TipcObjectManager();
  }

  return instance;
}

zx_status_t TipcObjectManager::AddObject(fbl::RefPtr<TipcObject> object) {
  fbl::AutoLock lock(&object_table_lock_);
  FXL_DCHECK(object->handle_id() == TipcObject::kInvalidHandle);

  uint32_t new_id;
  zx_status_t err = id_allocator_.Alloc(&new_id);
  if (err != ZX_OK) {
    return err;
  }
  object->handle_id_ = new_id;

  FXL_DCHECK(object_table_[new_id] == nullptr);
  object_table_[new_id] = object;

  return ZX_OK;
}

void TipcObjectManager::RemoveObject(uint32_t handle_id) {
  fbl::AutoLock lock(&object_table_lock_);

  FXL_DCHECK(handle_id < kMaxHandle);
  auto obj = fbl::move(object_table_[handle_id]);
  if (obj) {
    // also remove the object from object set,
    // since we are going to release handle id
    obj_set_.RemoveObject(obj);

    zx_status_t err = id_allocator_.Free(handle_id);
    FXL_CHECK(err == ZX_OK);

    obj->handle_id_ = TipcObject::kInvalidHandle;
  }
}

zx_status_t TipcObjectManager::GetObject(uint32_t handle_id,
                                         fbl::RefPtr<TipcObject>* obj_out) {
  fbl::AutoLock lock(&object_table_lock_);

  FXL_DCHECK(handle_id < kMaxHandle);
  if (!object_table_[handle_id]) {
    return ZX_ERR_BAD_HANDLE;
  }

  *obj_out = object_table_[handle_id];
  return ZX_OK;
}

}  // namespace ree_agent
