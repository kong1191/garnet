// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/unique_ptr.h>
#include <map>

#include "garnet/bin/gzos/ree_agent/gz_ipc_endpoint.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_msg.h"
#include "garnet/bin/gzos/ree_agent/ree_agent.h"

#include "lib/gzos/trusty_ipc/cpp/id_alloc.h"

namespace ree_agent {

class GzIpcAgent : public ReeAgent {
 public:
  GzIpcAgent(uint32_t id, zx::channel message_channel, size_t max_message_size)
      : ReeAgent(id, std::move(message_channel), max_message_size) {}

  zx_status_t SendMessageToPeer(uint32_t local, uint32_t remote, void* data,
                                size_t data_len);

 protected:
  void FreeEndpoint(uint32_t local_addr);

  zx_status_t HandleDisconnectRequest(gz_ipc_ctrl_msg_hdr* ctrl_hdr);

  fbl::Mutex lock_;
  trusty_ipc::IdAllocator<kMaxEndpointNumber> id_allocator_;
  std::map<uint32_t, fbl::unique_ptr<GzIpcEndpoint>> endpoint_table_
      FXL_GUARDED_BY(lock_);

 private:
  zx_status_t HandleConnectResponse(fbl::unique_ptr<GzIpcEndpoint>& endpoint,
                                    void* msg, size_t msg_len);

  zx_status_t HandleEndpointMessage(gz_ipc_msg_hdr* msg_hdr);

  virtual zx_status_t HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr) = 0;

  zx_status_t HandleMessage(void* buf, size_t size) override;
};

}  // namespace ree_agent
