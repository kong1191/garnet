// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <ree_agent/cpp/fidl.h>
#include <zircon/compiler.h>

#include "gtest/gtest.h"
#include "lib/ree_agent/cpp/channel.h"
#include "lib/ree_agent/cpp/port.h"

namespace ree_agent {

class TipcPortTest : public ::testing::Test {
 public:
  TipcPortTest()
      : loop_(&kAsyncLoopConfigMakeDefault), port_(kNumItems, kItemSize) {}

 protected:
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;

  virtual void SetUp() { ASSERT_EQ(loop_.StartThread(), ZX_OK); }

  virtual void TeadDown() {
    loop_.Quit();
    loop_.JoinThreads();
  }

  async::Loop loop_;
  TipcPortImpl port_;
};

TEST_F(TipcPortTest, PortConnect) {
  TipcPortSyncPtr port_client;
  port_.Bind(port_client.NewRequest());

  uint32_t num_items;
  size_t item_size;
  port_client->GetInfo(&num_items, &item_size);

  fbl::RefPtr<TipcChannelImpl> channel;
  ASSERT_EQ(TipcChannelImpl::Create(num_items, item_size, &channel),
            ZX_OK);

  zx_status_t status;
  fidl::InterfaceHandle<TipcChannel> peer_handle;
  port_client->Connect(channel->GetInterfaceHandle(), &status,
                       &peer_handle);
  ASSERT_EQ(status, ZX_OK);

  channel->BindPeerInterfaceHandle(std::move(peer_handle));

  WaitResult result;
  EXPECT_EQ(port_.Wait(&result, zx::time::infinite()), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);

  fbl::RefPtr<TipcChannelImpl> accepted_channel;
  EXPECT_EQ(port_.Accept(&accepted_channel), ZX_OK);

  // TODO(sy): verify connection response message here
  EXPECT_EQ(channel->Wait(&result, zx::time::infinite()), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::MSG);
}

}  // namespace ree_agent
