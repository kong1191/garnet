// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zx/eventpair.h>

#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"

#include "lib/fidl/cpp/interface_request.h"
#include "magma_util/simple_allocator.h"

namespace ree_agent {

class GzIpcClient : public GzIpcAgent {
 public:
  GzIpcClient(zx::channel message_channel, size_t max_message_size)
      : GzIpcAgent(std::move(message_channel), max_message_size),
        alloc_(magma::SimpleAllocator::Create(shm_info_.base_phys,
                                              shm_info_.size)) {
    FXL_CHECK(alloc_);
  }

  template <typename INTERFACE>
  zx_status_t Connect(fidl::InterfaceRequest<INTERFACE> request) {
    std::string service_name = INTERFACE::Name_;
    return Connect(service_name, request.TakeChannel());
  }

  zx_status_t Connect(std::string service_name, zx::channel channel);

  zx_status_t AllocSharedMemory(size_t size, zx::vmo* vmo_out);

 private:
  zx_status_t HandleFreeVmo(gz_ipc_ctrl_msg_hdr* ctrl_hdr);

  zx_status_t HandleCtrlMessage(gz_ipc_msg_hdr* msg_hdr) override;
  std::unique_ptr<magma::SimpleAllocator> alloc_;
};

}  // namespace ree_agent
