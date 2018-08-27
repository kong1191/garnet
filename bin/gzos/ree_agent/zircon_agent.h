// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>

#include <ree_agent/cpp/fidl.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"
#include "garnet/bin/gzos/ree_agent/zircon_endpoint.h"
#include "garnet/bin/gzos/ree_agent/zircon_msg.h"
#include "lib/gzos/trusty_ipc/cpp/id_alloc.h"

namespace ree_agent {

class ZirconAgent : public ReeAgent {
 public:
  ZirconAgent(uint32_t id, zx::channel ch, size_t max_msg_size,
              TaServices& service_provider);
  ~ZirconAgent() = default;

  zx_status_t Start() override;
  zx_status_t Stop() override;
  zx_status_t HandleMessage(void* buf, size_t size) override;

  zx_status_t SendMessageToRee(uint32_t local, uint32_t remote, void* data,
                               size_t data_len);

 private:
  zx_status_t HandleCtrlMessage(zircon_msg_hdr* hdr);
  zx_status_t HandleEndpointMessage(zircon_msg_hdr* hdr);

  zx_status_t HandleConnectRequest(uint32_t remote_addr, void* req);
  zx_status_t HandleDisconnectRequest(uint32_t remote_addr, void* req);

  TaServices& ta_service_provider_;

  zx_status_t AllocateEndpoint(uint32_t remote_addr, zx::channel channel,
                               uint32_t* local_addr_out);
  ZirconEndpoint* LookupEndpoint(uint32_t local_addr);
  void FreeEndpoint(uint32_t local_addr);

  fbl::Mutex lock_;
  trusty_ipc::IdAllocator<kMaxEndpointNumber> id_allocator_
      FXL_GUARDED_BY(lock_);
  fbl::unique_ptr<ZirconEndpoint> ep_table_[kMaxEndpointNumber] FXL_GUARDED_BY(
      lock_);
};

}  // namespace ree_agent
