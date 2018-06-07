// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ree_agent/cpp/object.h"
#include "lib/ree_agent/cpp/object_set.h"

namespace ree_agent {

TipcObject::TipcObject() : handle_id_(kInvalidHandle) {
  zx_status_t err = zx::event::create(0, &event_);
  FXL_CHECK(err == ZX_OK);
}

TipcObject::~TipcObject() = default;

zx_status_t TipcObject::AddParent(TipcObject* obj) {
  FXL_DCHECK(obj);
  FXL_DCHECK(obj->is_object_set());
  fbl::AutoLock lock(&mutex_);

  if (parent_list_.Contains(obj->handle_id())) {
    return ZX_ERR_ALREADY_EXISTS;
  }

  auto ref = fbl::make_unique<TipcObjectRef>(obj);
  if (!ref) {
    return ZX_ERR_NO_MEMORY;
  }

  parent_list_.push_back(fbl::move(ref));
  return ZX_OK;
}

void TipcObject::RemoveParent(TipcObject* obj) {
  fbl::AutoLock lock(&mutex_);

  auto it = parent_list_.Find(obj->handle_id());
  if (it != parent_list_.end()) {
    auto parent = static_cast<TipcObjectSet*>(it->get());
    parent->RemoveFromPendingList(this);

    parent_list_.erase(it);
  }
}

void TipcObject::RemoveAllParents() {
  fbl::AutoLock lock(&mutex_);

  while (const auto& ref = parent_list_.pop_front()) {
    auto parent = static_cast<TipcObjectSet*>(ref->get());
    parent->RemoveFromPendingList(this);
  }
}

zx_status_t TipcObject::SignalEvent(uint32_t set_mask) {
  fbl::AutoLock lock(&mutex_);

  for (const auto& ref : parent_list_) {
    auto parent = static_cast<TipcObjectSet*>(ref.get());

    zx_status_t err = parent->AppendToPendingList(this);
    if (err != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to add to parent pending list: " << err;
      return err;
    }

    parent->SignalEvent(TipcEvent::READY);
  }

  event_state_ |= set_mask;
  if (event_state_) {
    return event_.signal(0x0, NEED_ATTENSION);
  }

  return ZX_OK;
}

zx_status_t TipcObject::ClearEvent(uint32_t clear_mask) {
  fbl::AutoLock lock(&mutex_);

  event_state_ &= ~clear_mask;
  if (!event_state_) {
    return event_.signal(NEED_ATTENSION, 0x0);
  }

  return ZX_OK;
}

zx_signals_t TipcObject::event_state() {
  fbl::AutoLock lock(&mutex_);
  return event_state_;
}

zx_status_t TipcObject::Wait(WaitResult* result, zx::time deadline) {
  FXL_DCHECK(result);

  zx_signals_t observed;
  zx_status_t err = zx_object_wait_one(event_.get(), NEED_ATTENSION,
                                       deadline.get(), &observed);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for event " << err;
    return err;
  }

  result->event = event_state();
  result->cookie = cookie_;
  result->handle_id = handle_id_;

  return ZX_OK;
}

}  // namespace ree_agent
