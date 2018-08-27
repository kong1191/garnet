// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/type_support.h>
#include <zircon/types.h>
#include <lib/zx/channel.h>

namespace ree_agent {

class ZirconAgent;
class ZirconEndpoint {
 public:
  ZirconEndpoint() = delete;

  ZirconEndpoint(ZirconAgent* agent, uint32_t local_addr, uint32_t remote_addr,
                 zx::channel channel)
      : agent_(agent),
        local_addr_(local_addr),
        remote_addr_(remote_addr),
        channel_(fbl::move(channel)) {}

  zx_status_t ReceiveMessage(void* msg, size_t msg_len);

  zx_status_t SendMessage(void* msg, size_t msg_len);

  auto remote_addr() { return remote_addr_; }

 private:
  ZirconAgent* agent_;
  uint32_t local_addr_;
  uint32_t remote_addr_;
  zx::channel channel_;
};

};  // namespace ree_agent
