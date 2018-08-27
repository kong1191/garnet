// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/zircon_endpoint.h"

namespace ree_agent {

zx_status_t ZirconEndpoint::HandleMessage(void* msg, size_t msg_len) {
  return ZX_OK;
}

}  // namespace ree_agent
