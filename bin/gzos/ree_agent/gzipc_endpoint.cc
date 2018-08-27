// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/gzipc_endpoint.h"
#include "garnet/bin/gzos/ree_agent/gzipc_agent.h"
#include "garnet/bin/gzos/ree_agent/gzipc_msg.h"

namespace ree_agent {

zx_status_t GzipcEndpoint::Write(void* msg, size_t msg_len) {
  return channel_.write(0, msg, msg_len, NULL, 0);
}

zx_status_t GzipcEndpoint::OnMessage(Message message) {
  auto msg_hdr = message.Alloc<gzipc_msg_hdr>();

  msg_hdr->src = local_addr_;
  msg_hdr->dst = remote_addr_;
  msg_hdr->reserved = 0;
  msg_hdr->flags = 0;
  msg_hdr->len = message.actual() - sizeof(gzipc_msg_hdr);

  return agent_->Write(message.data(), message.actual());
}

}  // namespace ree_agent
