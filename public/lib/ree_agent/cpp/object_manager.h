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
#include "lib/ree_agent/cpp/object.h"

namespace ree_agent {

class TipcObjectManager {
 public:
  static TipcObjectManager* Instance();

  zx_status_t AddObject(fbl::unique_ptr<TipcObject> obj);
  zx_status_t RemoveObject(uint32_t id, fbl::unique_ptr<TipcObject>* obj_out);

  zx_status_t Wait(uint32_t* id, zx_signals_t* event_out, zx::time deadline);

 private:
  TipcObjectManager() {
    zx_status_t status = zx::port::create(0, &port_);
    FXL_CHECK(status == ZX_OK);
  }

  TipcObject* GetObject(uint32_t id);
  TipcObject* GetObjectLocked(uint32_t id)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(object_table_lock_);

  fbl::unique_ptr<TipcObject> object_table_[kMaxObject] FXL_GUARDED_BY(
      object_table_lock_);
  fbl::Mutex object_table_lock_;

  zx::port port_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObjectManager);
};

}  // namespace ree_agent
