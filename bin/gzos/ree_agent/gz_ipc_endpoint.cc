// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/gzos/ree_agent/gz_ipc_endpoint.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_msg.h"

namespace ree_agent {

zx_status_t GzIpcEndpoint::Write(void* msg, size_t msg_len) {
  return zx_channel_write(message_reader_.channel(), 0, msg, msg_len, nullptr,
                          0);
}

zx_status_t GzIpcEndpoint::OnMessage(Message message) {
  auto endpoint_hdr = message.Alloc<gz_ipc_endpoint_msg_hdr>();
  endpoint_hdr->num_handles = 0;

  auto msg_hdr = message.Alloc<gz_ipc_msg_hdr>();
  msg_hdr->src = local_addr_;
  msg_hdr->dst = remote_addr_;
  msg_hdr->reserved = 0;
  msg_hdr->flags = 0;
  msg_hdr->len = message.actual() - sizeof(gz_ipc_msg_hdr);

  return agent_->Write(message.data(), message.actual());
}

}  // namespace ree_agent
