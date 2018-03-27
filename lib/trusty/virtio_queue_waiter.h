// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/function.h>
#include <lib/async/cpp/wait.h>
#include <zircon/types.h>

#include "garnet/lib/trusty/virtio_queue.h"

namespace trusty {

// Helper for performing async waits against a virtio queue.
class VirtioQueueWaiter {
 public:
  VirtioQueueWaiter(async_t* async);
  using Callback = fbl::Function<void(zx_status_t, uint16_t index)>;

  zx_status_t Wait(VirtioQueue* queue, Callback callback);

  void Cancel();

 private:
  async_wait_result_t Handler(async_t* async,
                              zx_status_t status,
                              const zx_packet_signal_t* signal);

  async_t* const async_;
  async::Wait wait_;
  VirtioQueue* queue_;
  Callback callback_;
};

}  // namespace trusty
