// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/cpp/ree_agent.h>
#include <zircon/compiler.h>

#include "garnet/bin/gzos/ree_agent/ree_agent.h"
#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/ree_agent/cpp/channel.h"
#include "lib/ree_agent/cpp/port.h"

namespace ree_agent {

TEST(TipcAgentTest, PublishPort) {
  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  ReeAgent ree_agent;

  // Create and register TipcPortManager service
  TipcPortManagerImpl port_manager;
  ree_agent.services()->AddService<TipcPortManager>(
      [&port_manager](fidl::InterfaceRequest<TipcPortManager> request) {
        port_manager.Bind(fbl::move(request));
      });

  // Grab service directory, this is used by Trusted Application
  // for getting services published by ree_agent
  zx::channel svc_dir = ree_agent.services()->OpenAsDirectory();
  ASSERT_TRUE(svc_dir != ZX_HANDLE_INVALID);

  // Create a Services object for Trusted Application to connect
  // TipcPortManager service
  component::Services services;
  services.Bind(fbl::move(svc_dir));

  // Now publish a test port at Trusted Application
  fbl::String port_path("org.opentrust.test");
  TipcPort test_port(&services, port_path, [](void) {
    // Connection Request callback, leave it empty
  });
  test_port.Publish([](Status status) { EXPECT_EQ(status, Status::OK); });

  loop.RunUntilIdle();

  // Publish a duplicated path should fail
  TipcPort duplicated_port(&services, port_path, [](void) {
    // Connection Request callback, leave it empty
  });
  duplicated_port.Publish(
      [](Status status) { EXPECT_EQ(status, Status::ALREADY_EXISTS); });

  loop.RunUntilIdle();

  // Verify the published port in port talbe
  TipcPortTableEntry* entry;
  ASSERT_EQ(port_manager.Find(port_path, entry), ZX_OK);
  ASSERT_TRUE(entry != nullptr);
  EXPECT_STREQ(entry->path().c_str(), port_path.c_str());
}

class TipcChannelTest : public ::testing::Test {
 public:
  TipcChannelTest() : loop_(&kAsyncLoopConfigMakeDefault) {}

 protected:
  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;

  struct TestMessage {
    TestMessage(uint32_t seq_num) {
      fxl::StringAppendf(&string_, "Test message %u", seq_num);
    }
    auto data() { return const_cast<char*>(string_.c_str()); }
    auto size() { return string_.length() + 1; }

   private:
    std::string string_;
  };

  virtual void SetUp() {
    loop_.StartThread();

    // Create pair of channel
    ASSERT_EQ(TipcChannelImpl::Create(kNumItems, kItemSize, &local_channel_),
              ZX_OK);
    ASSERT_EQ(TipcChannelImpl::Create(kNumItems, kItemSize, &remote_channel_),
              ZX_OK);

    // Bind the channel with each other
    auto handle = remote_channel_->GetInterfaceHandle();
    ASSERT_EQ(local_channel_->BindPeerInterfaceHandle(std::move(handle)),
              ZX_OK);
    handle = local_channel_->GetInterfaceHandle();
    ASSERT_EQ(remote_channel_->BindPeerInterfaceHandle(std::move(handle)),
              ZX_OK);
  }

  virtual void TeadDown() {
    loop_.Quit();
    loop_.JoinThreads();
  }

  void TestSendAndReceive(TipcChannelImpl* sender, TipcChannelImpl* receiver) {
    ASSERT_TRUE(sender->initialized());
    ASSERT_TRUE(receiver->initialized());

    // Send test messages from sender
    for (uint32_t i = 0; i < kNumItems; i++) {
      TestMessage test_msg(i);
      EXPECT_EQ(sender->SendMessage(test_msg.data(), test_msg.size()),
                Status::OK);
    }

    // We ran out of free buffer, should fail
    uint32_t dummy;
    EXPECT_EQ(sender->SendMessage(&dummy, sizeof(dummy)), Status::NO_MEMORY);

    // Read test messages from receiver and verify it
    for (uint32_t i = 0; i < kNumItems; i++) {
      TestMessage expected_msg(i);

      // Get a message from the filled list
      uint32_t msg_id;
      size_t msg_len;
      EXPECT_EQ(receiver->GetMessage(&msg_id, &msg_len), Status::OK);
      EXPECT_EQ(msg_len, expected_msg.size());

      // Read the message and verify it
      char msg_buf[msg_len];
      EXPECT_EQ(receiver->ReadMessage(msg_id, 0, msg_buf, &msg_len),
                Status::OK);
      EXPECT_STREQ(msg_buf, expected_msg.data());
      EXPECT_EQ(msg_len, expected_msg.size());

      // Put the message back to free list
      EXPECT_EQ(receiver->PutMessage(msg_id), Status::OK);
    }
  }

  async::Loop loop_;
  fbl::unique_ptr<TipcChannelImpl> local_channel_;
  fbl::unique_ptr<TipcChannelImpl> remote_channel_;
};

TEST_F(TipcChannelTest, ExchangeMessage) {
  TestSendAndReceive(local_channel_.get(), remote_channel_.get());
  TestSendAndReceive(remote_channel_.get(), local_channel_.get());
}

}  // namespace ree_agent
