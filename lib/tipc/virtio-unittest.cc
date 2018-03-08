// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/virtio.h"

#include "garnet/lib/machina/phys_mem_fake.h"
#include "garnet/lib/machina/virtio_queue_fake.h"
#include "gtest/gtest.h"

#define QUEUE_SIZE 16
#define VIRTIO_TEST_ID 30

namespace machina {
namespace {

class TestDevice : public VirtioDevice {
 public:
  TestDevice()
      : VirtioDevice(VIRTIO_TEST_ID, nullptr, 0, &queue_, 1, phys_mem_),
        queue_fake_(&queue_) {}

  zx_status_t Init() { return queue_fake_.Init(QUEUE_SIZE); }

  virtio_queue_t& queue() { return queue_; }
  VirtioQueueFake& queue_fake() { return queue_fake_; }

 private:
  virtio_queue_t queue_;
  PhysMemFake phys_mem_;
  VirtioQueueFake queue_fake_;
};

TEST(VirtioQueueTest, HandleOverflow) {
  TestDevice device;
  ASSERT_EQ(device.Init(), ZX_OK);
  virtio_queue_t& queue = device.queue();
  VirtioQueueFake& queue_fake = device.queue_fake();

  // Setup queue pointers so that the next descriptor will wrap avail->idx
  // to 0.
  queue.avail->idx = UINT16_MAX;
  queue.index = UINT16_MAX;

  uint16_t expected_desc;
  uint32_t data = 0x12345678;
  ASSERT_EQ(queue_fake.BuildDescriptor()
                .AppendReadable(&data, sizeof(data))
                .Build(&expected_desc),
            ZX_OK);

  uint16_t desc;
  ASSERT_EQ(virtio_queue_next_avail(&queue, &desc), ZX_OK);
  ASSERT_EQ(desc, expected_desc);
  ASSERT_EQ(queue.avail->idx, 0);
  ASSERT_EQ(queue.index, 0);
}

}  // namespace
}  // namespace machina
