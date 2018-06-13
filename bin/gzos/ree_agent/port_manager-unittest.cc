// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <ree_agent/cpp/fidl.h>
#include <zircon/compiler.h>

#include "garnet/bin/gzos/ree_agent/port_manager.h"
#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "gtest/gtest.h"
#include "lib/ree_agent/cpp/channel.h"
#include "lib/ree_agent/cpp/port.h"

namespace ree_agent {

class TipcPortManagerTest : public ::testing::Test {
 public:
  TipcPortManagerTest()
      : loop_(&kAsyncLoopConfigMakeDefault), ree_agent_(new ReeAgent()) {}

 protected:
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;
  static const TipcPortInfo kPortInfo;

  void SetUp() {
    // We need exactly 2 loop threads for this test case
    ASSERT_EQ(loop_.StartThread(), ZX_OK);
    ASSERT_EQ(loop_.StartThread(), ZX_OK);

    // Create and register TipcPortManager service
    ree_agent_->services()->AddService<TipcPortManager>(
        [this](fidl::InterfaceRequest<TipcPortManager> request) {
          port_manager_.Bind(fbl::move(request));
        });

    // Bind a service object to the service directory provided by ree_agent
    // Client can use the service object connecting to services published
    // by ree_agent
    zx::channel svc_dir = ree_agent_->services()->OpenAsDirectory();
    ASSERT_TRUE(svc_dir != ZX_HANDLE_INVALID);
    services_.Bind(fbl::move(svc_dir));
  }

  void TearDown() {
    loop_.Quit();
    loop_.JoinThreads();
  }

  TipcPortManagerImpl port_manager_;
  component::Services services_;
  async::Loop loop_;
  fbl::unique_ptr<ReeAgent> ree_agent_;
};

const TipcPortInfo TipcPortManagerTest::kPortInfo = {
    .path = "org.opentrust.test",
    .num_items = kNumItems,
    .item_size = kItemSize,
};

TEST_F(TipcPortManagerTest, PublishPort) {
  // Now publish a test port at Trusted Application
  TipcPortImpl test_port(&services_, kPortInfo);
  EXPECT_EQ(test_port.Publish(), ZX_OK);

  // Publish a duplicated path should fail
  TipcPortImpl duplicated_port(&services_, kPortInfo);
  EXPECT_EQ(duplicated_port.Publish(), ZX_ERR_ALREADY_EXISTS);

  // Verify the published port in port table
  TipcPortTableEntry* entry;
  ASSERT_EQ(port_manager_.Find(kPortInfo.path.get(), entry), ZX_OK);
  ASSERT_TRUE(entry != nullptr);
  EXPECT_STREQ(entry->path(), kPortInfo.path->c_str());
}

TEST_F(TipcPortManagerTest, PortConnect) {
  TipcPortImpl test_port(&services_, kPortInfo);
  EXPECT_EQ(test_port.Publish(), ZX_OK);

  TipcPortTableEntry* entry;
  ASSERT_EQ(port_manager_.Find(kPortInfo.path.get(), entry), ZX_OK);
  ASSERT_TRUE(entry != nullptr);

  fbl::RefPtr<TipcChannelImpl> remote_channel;
  async::PostTask(loop_.async(), [&entry, &remote_channel] {
    EXPECT_EQ(entry->Connect(&remote_channel), ZX_OK);
  });

  WaitResult result;
  EXPECT_EQ(test_port.Wait(&result, zx::time::infinite()), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);

  fbl::RefPtr<TipcChannelImpl> local_channel;
  EXPECT_EQ(test_port.Accept(&local_channel), ZX_OK);

  // TODO: verify connection response message here
  EXPECT_EQ(remote_channel->Wait(&result, zx::time::infinite()), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::MSG);
}

}  // namespace ree_agent
