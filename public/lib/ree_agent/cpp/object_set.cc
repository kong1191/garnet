// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/object_set.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

zx_status_t TipcObjectSet::AddObject(fbl::RefPtr<TipcObject> obj) {
  FXL_DCHECK(obj);

  zx_status_t err = obj->AddParent(this);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to create object reference: " << err;
    return err;
  }

  return ZX_OK;
}

void TipcObjectSet::RemoveObject(fbl::RefPtr<TipcObject> obj) {
  FXL_DCHECK(obj);
  obj->RemoveParent(this);
}

void TipcObjectSet::OnChildrenAdded(TipcObjectRef& ref) {
  if (ref.obj->tipc_event_state()) {
    AppendToPendingList(ref);
  }

  fbl::AutoLock lock(&mutex_);
  children_count_++;
}

void TipcObjectSet::OnChildrenRemoved(TipcObjectRef& ref) {
  RemoveFromPendingList(ref);

  fbl::AutoLock lock(&mutex_);
  children_count_--;
}

void TipcObjectSet::SignalEvent(uint32_t set_mask, TipcObjectRef& notifier) {
  FXL_DCHECK(set_mask == TipcEvent::READY);

  AppendToPendingList(notifier);
  TipcObject::SignalEvent(set_mask);
}

void TipcObjectSet::AppendToPendingList(TipcObjectRef& ref) {
  fbl::AutoLock lock(&mutex_);

  // ignore if already in the pending list
  for (const auto& r : pending_list_) {
    if (r.obj == ref.obj) {
      return;
    }
  }

  pending_list_.push_back(&ref);
}

void TipcObjectSet::RemoveFromPendingList(TipcObjectRef& ref) {
  fbl::AutoLock lock(&mutex_);

  pending_list_.erase_if([&ref](const TipcObjectRef& r) {
    return r.obj == ref.obj;
  });
}

bool TipcObjectSet::PollPendingEvents(WaitResult* result) {
  fbl::AutoLock lock(&mutex_);

  while (auto ref = pending_list_.pop_front()) {
    auto obj = ref->obj;
    zx_signals_t event = obj->tipc_event_state();

    if (event) {
      result->handle_id = obj->handle_id();
      result->event = event;
      result->cookie = obj->cookie();

      pending_list_.push_back(fbl::move(ref));
      return true;
    }
  }

  ClearEvent(TipcEvent::READY);

  return false;
}

uint32_t TipcObjectSet::children_count() {
  fbl::AutoLock lock(&mutex_);
  return children_count_;
}

zx_status_t TipcObjectSet::Wait(WaitResult* result, zx::time deadline) {
  FXL_DCHECK(result);

  if (children_count() == 0) {
    return ZX_ERR_NOT_FOUND;
  }

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
