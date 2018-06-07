// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/event.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"

namespace ree_agent {

enum TipcEvent : uint32_t {
  READY = ZX_USER_SIGNAL_0,
  ERROR = ZX_USER_SIGNAL_1,
  HUP = ZX_USER_SIGNAL_2,
  MSG = ZX_USER_SIGNAL_3,
  SEND_UNBLOCKED = ZX_USER_SIGNAL_4,

  ALL = READY | ERROR | HUP | MSG | SEND_UNBLOCKED,
};

class TipcObject {
 public:
  enum ObjectType { PORT, CHANNEL };

  TipcObject();
  virtual ~TipcObject() = default;

  zx_status_t SignalEvent(uint32_t set_mask) {
    return event_.signal(0x0, set_mask);
  }

  virtual ObjectType get_type() = 0;
  zx_handle_t event() const { return event_.get(); }

 private:
  friend class TipcObjectManager;

  // Only TipcObjectManager is allowed to clear event state
  zx_status_t ClearEvent(uint32_t clear_mask) {
    return event_.signal(clear_mask, 0x0);
  }
  zx::event event_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObject);
};

}  // namespace ree_agent
