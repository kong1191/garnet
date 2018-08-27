// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/zircon_agent.h"
#include "garnet/bin/gzos/ree_agent/zircon_endpoint.h"

namespace ree_agent {

zx_status_t ZirconEndpoint::ReceiveMessage(void* msg, size_t msg_len) {
  auto hdr = reinterpret_cast<zircon_endpoint_msg_hdr*>(msg);
  auto payload = hdr + 1;
  auto payload_len = msg_len - sizeof(zircon_endpoint_msg_hdr);

  return channel_.write(0, payload, payload_len, NULL, 0);
}

zx_status_t ZirconEndpoint::SendMessage(void* msg, size_t msg_len) {
  return agent_->SendMessageToRee(remote_addr_, local_addr_, msg, msg_len);
}

}  // namespace ree_agent
