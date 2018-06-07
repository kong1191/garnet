// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>

#include "lib/ree_agent/cpp/handle.h"
#include "lib/ree_agent/cpp/id_alloc.h"
#include "lib/ree_agent/cpp/object.h"
#include "lib/ree_agent/cpp/object_manager.h"

namespace ree_agent {

static IdAllocator<kMaxHandle> id_allocator;

Status TipcObjectManager::CreateHandle(fbl::unique_ptr<TipcObject> object,
                                       uint32_t* id_out) {
  FXL_DCHECK(id_out);
  uint32_t new_id;
  zx_status_t err = id_allocator.Alloc(&new_id);
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to allocate hanlde id: " << err;
    return Status::NO_RESOURCES;
  }

  fbl::unique_ptr<TipcHandle> handle(new TipcHandle(new_id, fbl::move(object)));
  if (!handle) {
    return Status::NO_MEMORY;
  }

  Status status = AddHandle(fbl::move(handle));
  if (status != Status::OK) {
    return status;
  }

  *id_out = new_id;
  return Status::OK;
}

TipcHandle* TipcObjectManager::GetHandle(uint32_t id) {
  fbl::AutoLock lock(&handle_table_lock_);
  return GetHandleLocked(id);
}

TipcHandle* TipcObjectManager::GetHandleLocked(uint32_t id) {
  FXL_DCHECK(id < kMaxHandle);
  return handle_table_[id].get();
}

Status TipcObjectManager::AddHandle(fbl::unique_ptr<TipcHandle> handle) {
  FXL_DCHECK(handle);

  zx_status_t err = handle->BeginAsyncWait(port_.get());
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to async wait on port " << err;
    return Status::GENERIC;
  }

  fbl::AutoLock lock(&handle_table_lock_);
  uint32_t id = handle->id();

  FXL_DCHECK(GetHandleLocked(id) == nullptr);
  handle_table_[id] = fbl::move(handle);

  return Status::OK;
}

Status TipcObjectManager::RemoveHandle(uint32_t id) {
  fbl::AutoLock lock(&handle_table_lock_);

  TipcHandle* handle = GetHandleLocked(id);
  if (!handle) {
    return Status::BAD_HANDLE;
  }

  handle->CancelAsyncWait(port_.get());
  handle_table_[id].reset();

  zx_status_t err = id_allocator.Free(id);
  FXL_CHECK(err == ZX_OK);

  return Status::OK;
}

Status TipcObjectManager::WaitOne(uint32_t id, zx_signals_t* event_out,
                                  zx::time deadline) {
  FXL_DCHECK(event_out);
  TipcHandle* handle = GetHandle(id);
  if (!handle) {
    return Status::NOT_FOUND;
  }

  // Cancel async wait to prevent further events to be observed on zx::port,
  // it also remove the pending event already queued on zx::port, if any.
  // Remove pending event will not cause event lost, since the state of
  // zx::event in interested object is not changed, thus the event will
  // be observed by the following zx_object_wait_one().
  handle->CancelAsyncWait(port_.get());

  zx_handle_t event_handle = handle->object()->event();
  zx_signals_t observed;
  zx_status_t err = zx_object_wait_one(event_handle, TipcEvent::ALL,
                                       deadline.get(), &observed);
  if (err == ZX_ERR_TIMED_OUT) {
    return Status::TIMED_OUT;
  }

  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait for event " << err;
    return Status::GENERIC;
  }

  err = handle->object()->ClearEvent(observed);
  FXL_DCHECK(err == ZX_OK);

  err = handle->BeginAsyncWait(port_.get());
  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to async wait on zx::port " << err;
    return Status::GENERIC;
  }

  *event_out = observed;

  return Status::OK;
}

Status TipcObjectManager::WaitAny(uint32_t* id_out, zx_signals_t* event_out,
                                  zx::time deadline) {
  FXL_DCHECK(id_out);
  FXL_DCHECK(event_out);
  zx_port_packet_t packet;

  zx_status_t err = port_.wait(fbl::move(deadline), &packet);
  if (err == ZX_ERR_TIMED_OUT) {
    return Status::TIMED_OUT;
  }

  if (err != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to wait on port " << err;
    return Status::GENERIC;
  }

  uint32_t id = packet.key;
  zx_signals_t observed = packet.signal.observed;

  auto handle = GetHandle(id);
  FXL_DCHECK(handle);

  err = handle->object()->ClearEvent(observed);
  FXL_DCHECK(err == ZX_OK);

  *id_out = id;
  *event_out = observed;
  return Status::OK;
}

}  // namespace ree_agent
