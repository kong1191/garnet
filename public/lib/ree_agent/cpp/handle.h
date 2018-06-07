// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fbl/unique_ptr.h>
#include <ree_agent/cpp/fidl.h>

#include "lib/fxl/logging.h"

namespace ree_agent {

class TipcObject;
class TipcHandle {
 public:
  TipcHandle() = delete;

  zx_status_t BeginAsyncWait(zx_handle_t port_handle);
  void CancelAsyncWait(zx_handle_t port_handle);

  TipcObject* object() { return object_.get(); }
  uint32_t id() { return id_; }

 private:
  friend class TipcObjectManager;
  TipcHandle(uint32_t id, fbl::unique_ptr<TipcObject> object);

  uint32_t id_;
  fbl::unique_ptr<TipcObject> object_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcHandle);
};

}  // namespace ree_agent
