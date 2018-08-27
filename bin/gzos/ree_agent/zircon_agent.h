// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>

#include <ree_agent/cpp/fidl.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "garnet/bin/gzos/ree_agent/zircon_endpoint.h"
#include "garnet/bin/gzos/ree_agent/zircon_msg.h"
#include "lib/gzos/trusty_ipc/cpp/id_alloc.h"

namespace ree_agent {

// ZirconEndpointTable is not thread-safe. User should have a lock
// to guarantee the atomicity of the table operations.
class ZirconEndpointTable {
 public:
  ZirconEndpointTable() = default;

  zx_status_t AllocateSlot(uint32_t remote_addr, uint32_t* slot);
  ZirconEndpoint* Lookup(uint32_t slot);
  void FreeSlot(uint32_t slot);

 private:
  trusty_ipc::IdAllocator<kMaxEndpointNumber> id_allocator_;
  fbl::unique_ptr<ZirconEndpoint> table_[kMaxEndpointNumber];

  FXL_DISALLOW_COPY_AND_ASSIGN(ZirconEndpointTable);
};

class ZirconAgent : public ReeAgent {
 public:
  ZirconAgent(uint32_t id, zx::channel ch, size_t max_msg_size,
              ZirconEndpointTable* ep_table);
  ~ZirconAgent() = default;

  zx_status_t Start() override;
  zx_status_t Stop() override;
  zx_status_t HandleMessage(void* buf, size_t size) override;

 private:
  zx_status_t SendMessageToRee(uint32_t local, uint32_t remote, void* data,
                               size_t data_len);

  zx_status_t HandleCtrlMessage(zircon_msg_hdr* hdr);
  zx_status_t HandleEndpointMessage(zircon_msg_hdr* hdr);

  zx_status_t HandleConnectRequest(uint32_t remote_addr);
  zx_status_t HandleDisconnectRequest(uint32_t remote_addr, void* req);

  fbl::Mutex lock_;
  fbl::unique_ptr<ZirconEndpointTable> ep_table_ FXL_GUARDED_BY(lock_);
};

}  // namespace ree_agent
