// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <ree_agent/cpp/fidl.h>
#include <zx/event.h>
#include <zx/port.h>

#include "lib/fxl/logging.h"
#include "lib/ree_agent/cpp/id_alloc.h"

namespace ree_agent {

static constexpr uint32_t kMaxObject = 64;

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
  virtual ~TipcObject();

  zx_status_t Init();
  zx_status_t Wait(zx_signals_t* event_out, zx::time deadline);

  zx_status_t SignalEvent(uint32_t set_mask) {
    return event_.signal(0x0, set_mask);
  }
  zx_status_t BeginAsyncWait(zx::port& port);
  void CancelAsyncWait(zx::port& port);

  bool is_initialized() { return initialized_; }
  bool is_port() { return (get_type() == ObjectType::PORT); }
  bool is_channel() { return (get_type() == ObjectType::CHANNEL); }
  uint32_t id() { return id_; }

 protected:
  virtual ObjectType get_type() = 0;

 private:
  // Only TipcObjectManager is allowed to clear event state
  friend class TipcObjectManager;
  zx_status_t ClearEvent(uint32_t clear_mask) {
    return event_.signal(clear_mask, 0x0);
  }

  uint32_t id_;
  zx::event event_;

  fbl::Mutex initialize_lock_;
  bool initialized_ = false;
  static IdAllocator<kMaxObject> id_allocator_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TipcObject);
};

}  // namespace ree_agent
