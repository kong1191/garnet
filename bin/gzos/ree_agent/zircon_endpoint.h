// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

namespace ree_agent {

class ZirconEndpoint {
 public:
  ZirconEndpoint() = delete;

  ZirconEndpoint(uint32_t remote_addr) : remote_addr_(remote_addr) {}

  zx_status_t HandleMessage(void* data, size_t date_len);

  auto remote_addr() { return remote_addr_; }

 private:
  uint32_t remote_addr_;
};

};  // namespace ree_agent
