// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace ree_agent {

static constexpr uint32_t kMaxHandle = 64;

class TipcHandle;
class TipcObjectManager {
 public:
  TipcObjectManager() {
    zx_status_t status = zx::port::create(0, &port_);
    FXL_CHECK(status == ZX_OK);
  }

  Status CreateHandle(fbl::unique_ptr<TipcObject> object, uint32_t* id_out);
  Status RemoveHandle(uint32_t id);
  TipcHandle* GetHandle(uint32_t id);

  Status WaitOne(uint32_t id, zx_signals_t* event_out, zx::time deadline);
  Status WaitAny(uint32_t* id, zx_signals_t* event_out, zx::time deadline);

 private:
  fbl::unique_ptr<TipcHandle> handle_table_[kMaxHandle] FXL_GUARDED_BY(
      handle_table_lock_);
  fbl::Mutex handle_table_lock_;

  Status AddHandle(fbl::unique_ptr<TipcHandle> handle);
  TipcHandle* GetHandleLocked(uint32_t id)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(handle_table_lock_);

  zx::port port_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObjectManager);
};

}  // namespace ree_agent
