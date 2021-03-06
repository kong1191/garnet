// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_VIRTIO_BALLOON_H_
#define GARNET_LIB_MACHINA_VIRTIO_BALLOON_H_

#include <fuchsia/guest/device/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <virtio/balloon.h>
#include <virtio/virtio_ids.h>

#include "garnet/lib/machina/virtio_device.h"

namespace machina {

static constexpr uint16_t kVirtioBalloonNumQueues = 3;

class VirtioBalloon
    : public VirtioComponentDevice<VIRTIO_ID_BALLOON, kVirtioBalloonNumQueues,
                                   virtio_balloon_config_t>,
      public fuchsia::guest::BalloonController {
 public:
  explicit VirtioBalloon(const PhysMem& phys_mem);

  zx_status_t AddPublicService(component::StartupContext* context);

  // If |demand_page| is true, then deflate requests will be treated as a no-op.
  // Memory will instead be provided via demand paging.
  zx_status_t Start(const zx::guest& guest, bool demand_page,
                    fuchsia::sys::Launcher* launcher,
                    async_dispatcher_t* dispatcher);

 private:
  fidl::BindingSet<fuchsia::guest::BalloonController> bindings_;
  fuchsia::sys::ComponentControllerPtr controller_;
  // Use a sync pointer for consistency of virtual machine execution.
  fuchsia::guest::device::VirtioBalloonSyncPtr balloon_;
  fuchsia::guest::device::VirtioBalloonPtr stats_;

  zx_status_t ConfigureQueue(uint16_t queue, uint16_t size, zx_gpaddr_t desc,
                             zx_gpaddr_t avail, zx_gpaddr_t used);
  zx_status_t Ready(uint32_t negotiated_features);

  // |fuchsia::guest::BalloonController|
  void GetNumPages(GetNumPagesCallback callback) override;
  void RequestNumPages(uint32_t num_pages) override;
  void GetMemStats(GetMemStatsCallback callback) override;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_VIRTIO_BALLOON_H_
