// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/object_set.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

zx_status_t TipcObjectSet::AddObject(fbl::RefPtr<TipcObject> object) {
  zx_status_t err = object->AddParent(this);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to add parent object " << err;
    return err;
  }
  return ZX_OK;
}

void TipcObjectSet::RemoveObject(fbl::RefPtr<TipcObject> object) {
  object->RemoveParent(this);
}

zx_status_t TipcObjectSet::AppendToPendingList(TipcObject* obj) {
  FXL_DCHECK(obj);
  fbl::AutoLock lock(&pending_list_lock_);

  // ignore if already in the pending list
  if (pending_list_.Contains(obj->handle_id())) {
    return ZX_OK;
  }

  auto ref = fbl::make_unique<TipcObjectRef>(obj);
  if (!ref) {
    return ZX_ERR_NO_MEMORY;
  }
  pending_list_.push_back(fbl::move(ref));

  return ZX_OK;
}

void TipcObjectSet::RemoveFromPendingList(TipcObject* obj) {
  FXL_DCHECK(obj);
  fbl::AutoLock lock(&pending_list_lock_);

  auto it = pending_list_.Find(obj->handle_id());
  if (it != pending_list_.end()) {
    pending_list_.erase(it);
  }
}

bool TipcObjectSet::PollPendingEvents(WaitResult* result) {
  fbl::AutoLock lock(&pending_list_lock_);

  while (auto ref = pending_list_.pop_front()) {
    zx_signals_t event = (*ref)->event_state();

    if (event) {
      result->handle_id = (*ref)->handle_id();
      result->event = event;
      result->cookie = (*ref)->cookie();

      pending_list_.push_back(fbl::move(ref));
      return true;
    }
  }

  ClearEvent(TipcEvent::READY);

  return false;
}

zx_status_t TipcObjectSet::Wait(WaitResult* result, zx::time deadline) {
  FXL_DCHECK(result);

  while (true) {
    bool found = PollPendingEvents(result);
    if (found) {
      break;
    }

    zx_status_t err = TipcObject::Wait(result, deadline);
    if (err != ZX_OK) {
      return err;
    }
  }

  return ZX_OK;
}

}  // namespace ree_agent
