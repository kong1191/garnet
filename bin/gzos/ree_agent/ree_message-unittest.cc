// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <fs/vfs.h>
#include <lib/async-loop/cpp/loop.h>
#include <ree_agent/cpp/fidl.h>

#include "gtest/gtest.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/svc/cpp/services.h"

#include "garnet/bin/gzos/ree_agent/ree_message_impl.h"
#include "garnet/bin/gzos/ree_agent/tipc_msg.h"

namespace ree_agent {

class ReeMessageTest : public ::testing::Test {
 public:
  ReeMessageTest() : loop_(&kAsyncLoopConfigMakeDefault) {}

 protected:
  void SetUp() override {
    zx::channel ree_agent_cli, ree_agent_svc;
    zx::channel::create(0, &ree_agent_cli, &ree_agent_svc);
    ree_message_.Bind(std::move(ree_agent_cli));
    ree_message_impl_.Bind(std::move(ree_agent_svc));

    buf_ptr_.reset(new char[PAGE_SIZE]);
    ASSERT_TRUE(buf_ptr_ != nullptr);

    zx::channel::create(0, &msg_local_, &msg_remote_);
    loop_.StartThread();
  }

  void TearDown() override {
    loop_.Quit();
    loop_.JoinThreads();
  }

  fbl::unique_ptr<char> buf_ptr_;
  async::Loop loop_;
  ReeMessageSyncPtr ree_message_;
  ReeMessageImpl ree_message_impl_;
  zx::channel msg_local_, msg_remote_;
};

TEST_F(ReeMessageTest, AddMessageChannelOK) {
  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<MessageChannelInfo> infos;
  infos.push_back({MessageType::Tipc, 0, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_OK);
}

TEST_F(ReeMessageTest, AddMessageChannelWithInvalidType) {
  MessageType type = static_cast<MessageType>(-1);
  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<MessageChannelInfo> infos;
  infos.push_back({type, 0, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_ERR_NOT_SUPPORTED);
}

TEST_F(ReeMessageTest, AddMessageChannelWithInvalidId) {
  uint32_t id = kMaxMsgChannels + 1;
  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<MessageChannelInfo> infos;
  infos.push_back({MessageType::Tipc, id, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
}

TEST_F(ReeMessageTest, AddMessageChannelWithSameIds) {
  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<MessageChannelInfo> infos;
  infos.push_back({MessageType::Tipc, 0, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_OK);

  /* add channel again */
  zx::channel::create(0, &msg_local_, &msg_remote_);
  infos.push_back({MessageType::Tipc, 0, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_ERR_ALREADY_EXISTS);
}

TEST_F(ReeMessageTest, StartTipcMessageChannelOK) {
  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<MessageChannelInfo> infos;
  infos.push_back({MessageType::Tipc, 0, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_OK);

  ASSERT_TRUE(ree_message_->Start(nullptr, &status));
  ASSERT_EQ(status, ZX_OK);

  uint32_t expect = sizeof(tipc_hdr) + sizeof(tipc_ctrl_msg_hdr);
  uint32_t actual;

  auto buf = buf_ptr_.get();
  ASSERT_EQ(msg_local_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
            ZX_OK);
  ASSERT_EQ(actual, expect);

  auto hdr = reinterpret_cast<tipc_hdr*>(buf);
  EXPECT_EQ(hdr->src, kTipcCtrlAddress);
  EXPECT_EQ(hdr->dst, kTipcCtrlAddress);
  EXPECT_EQ(hdr->len, sizeof(tipc_ctrl_msg_hdr));

  auto ctrl_hdr = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr + 1);
  EXPECT_EQ(ctrl_hdr->type, CtrlMessage::GO_ONLINE);
  EXPECT_EQ(ctrl_hdr->body_len, 0u);
}

TEST_F(ReeMessageTest, StartInvalidMessageChannel) {
  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<MessageChannelInfo> infos;
  infos.push_back({MessageType::Tipc, 0, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_OK);

  fidl::VectorPtr<uint32_t> ids;
  ids.push_back(1);

  ASSERT_TRUE(ree_message_->Start(fbl::move(ids), &status));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
}

TEST_F(ReeMessageTest, StartMessageChannelTwice) {
  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<MessageChannelInfo> infos;
  infos.push_back({MessageType::Tipc, 0, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_OK);

  ASSERT_TRUE(ree_message_->Start(nullptr, &status));
  ASSERT_EQ(status, ZX_OK);

  ASSERT_TRUE(ree_message_->Start(nullptr, &status));
  ASSERT_EQ(status, ZX_ERR_BAD_STATE);
}

TEST_F(ReeMessageTest, StopMessageChannelBeforeStart) {
  zx_status_t status = ZX_ERR_INTERNAL;
  fidl::VectorPtr<MessageChannelInfo> infos;
  infos.push_back({MessageType::Tipc, 0, PAGE_SIZE, std::move(msg_remote_)});

  ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
  ASSERT_EQ(status, ZX_OK);

  /* if channel is not started, ignore stop action */
  ASSERT_TRUE(ree_message_->Stop(nullptr, &status));
  ASSERT_EQ(status, ZX_OK);
}

component::Services service_provider;
const char port_name[] = "test.svc1";

void ConnectToTaService(zx::channel request, const std::string& service_name) {
  service_provider.ConnectToService(std::move(request), service_name);
}

class TipcAgentTest : public ReeMessageTest {
 public:
  TipcAgentTest() : port_(kNumItems, kItemSize), vfs_(loop_.async()) {}

 protected:
  void SetUp() override {
    ReeMessageTest::SetUp();

    zx::channel::create(0, &ta_req_cli_, &ta_req_srv_);

    fbl::RefPtr<fs::PseudoDir> dir(fbl::AdoptRef(new fs::PseudoDir()));
    dir->AddEntry(
        port_name,
        fbl::AdoptRef(new fs::Service([&](zx::channel channel) {
          port_.Bind(
              fidl::InterfaceRequest<ree_agent::TipcPort>(std::move(channel)));
          return ZX_OK;
        })));

    ASSERT_EQ(vfs_.ServeDirectory(dir, std::move(ta_req_srv_)), ZX_OK);

    service_provider.Bind(std::move(ta_req_cli_));

    StartTipcAgent();
  }

  void TearDown() override {
    ReeMessageTest::TearDown();
  }

  void StartTipcAgent() {
    zx_status_t status = ZX_ERR_INTERNAL;
    fidl::VectorPtr<MessageChannelInfo> infos;
    infos.push_back({MessageType::Tipc, 0, PAGE_SIZE, std::move(msg_remote_)});

    ASSERT_TRUE(ree_message_->AddMessageChannel(std::move(infos), &status));
    ASSERT_EQ(status, ZX_OK);

    ASSERT_TRUE(ree_message_->Start(nullptr, &status));
    ASSERT_EQ(status, ZX_OK);

    uint32_t expect = sizeof(tipc_hdr) + sizeof(tipc_ctrl_msg_hdr);
    uint32_t actual;

    auto buf = buf_ptr_.get();
    ASSERT_EQ(msg_local_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
              ZX_OK);
    ASSERT_EQ(actual, expect);

    auto hdr = reinterpret_cast<tipc_hdr*>(buf);
    EXPECT_EQ(hdr->src, kTipcCtrlAddress);
    EXPECT_EQ(hdr->dst, kTipcCtrlAddress);
    EXPECT_EQ(hdr->len, sizeof(tipc_ctrl_msg_hdr));

    auto ctrl_hdr = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr + 1);
    EXPECT_EQ(ctrl_hdr->type, CtrlMessage::GO_ONLINE);
    EXPECT_EQ(ctrl_hdr->body_len, 0u);
  }

  static constexpr uint32_t kNumItems = 3u;
  static constexpr size_t kItemSize = 1024u;
  zx::channel ta_req_cli_, ta_req_srv_;
  TipcPortImpl port_;
  fs::SynchronousVfs vfs_;
};

TEST_F(TipcAgentTest, PortConnectOk) {
  auto buf = buf_ptr_.get();
  auto hdr = reinterpret_cast<tipc_hdr*>(buf);
  auto ctrl_msg = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr->data);
  auto conn_req = reinterpret_cast<tipc_conn_req_body*>(ctrl_msg + 1);

  uint32_t write_size = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*conn_req);

  uint32_t src_addr = 5;
  hdr->src = src_addr;
  hdr->dst = kTipcCtrlAddress;
  hdr->len = sizeof(*ctrl_msg) + sizeof(*conn_req);
  ctrl_msg->type = CtrlMessage::CONNECT_REQUEST;
  ctrl_msg->body_len = sizeof(tipc_conn_req_body);
  strncpy(conn_req->name, port_name, kTipcMaxServerNameLength);
  conn_req->name[kTipcMaxServerNameLength - 1] = '\0';

  ASSERT_EQ(msg_local_.write(0, buf, write_size, nullptr, 0), ZX_OK);

  loop_.RunUntilIdle();

  WaitResult result;
  EXPECT_EQ(port_.Wait(&result, zx::deadline_after(zx::sec(1))), ZX_OK);
  EXPECT_EQ(result.event, TipcEvent::READY);

  fbl::RefPtr<TipcChannelImpl> remote_channel;
  EXPECT_EQ(port_.Accept(&remote_channel), ZX_OK);

  loop_.RunUntilIdle();

  zx_signals_t observed;
  ASSERT_EQ(msg_local_.wait_one(ZX_CHANNEL_READABLE,
            zx::deadline_after(zx::sec(1)), &observed), ZX_OK);

  uint32_t actual;
  ASSERT_EQ(msg_local_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
            ZX_OK);

  auto conn_res = reinterpret_cast<tipc_conn_rsp_body*>(ctrl_msg + 1);

  uint32_t expect = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*conn_res);
  EXPECT_EQ(actual, expect);

  EXPECT_EQ(ctrl_msg->type, CtrlMessage::CONNECT_RESPONSE);
  EXPECT_EQ(ctrl_msg->body_len, sizeof(*conn_res));

  EXPECT_EQ(conn_res->target, src_addr);
  EXPECT_GE(conn_res->remote, kTipcAddrBase);
  EXPECT_EQ(conn_res->status, static_cast<uint32_t>(ZX_OK));
  EXPECT_EQ(conn_res->max_msg_cnt, static_cast<uint32_t>(1));
  EXPECT_EQ(conn_res->max_msg_size, kTipcChanMaxBufSize);
}

TEST_F(TipcAgentTest, PortConnectWithInvalidName) {
  auto buf = buf_ptr_.get();
  auto hdr = reinterpret_cast<tipc_hdr*>(buf);
  auto ctrl_msg = reinterpret_cast<tipc_ctrl_msg_hdr*>(hdr->data);
  auto conn_req = reinterpret_cast<tipc_conn_req_body*>(ctrl_msg + 1);

  uint32_t write_size = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*conn_req);

  const char invalid_port_name[] = "invalid";
  uint32_t src_addr = 5;
  hdr->src = src_addr;
  hdr->dst = kTipcCtrlAddress;
  hdr->len = sizeof(*ctrl_msg) + sizeof(*conn_req);
  ctrl_msg->type = CtrlMessage::CONNECT_REQUEST;
  ctrl_msg->body_len = sizeof(tipc_conn_req_body);
  strncpy(conn_req->name, invalid_port_name, kTipcMaxServerNameLength);
  conn_req->name[kTipcMaxServerNameLength - 1] = '\0';

  ASSERT_EQ(msg_local_.write(0, buf, write_size, nullptr, 0), ZX_OK);

  loop_.RunUntilIdle();

  zx_signals_t observed;
  ASSERT_EQ(msg_local_.wait_one(ZX_CHANNEL_READABLE,
            zx::deadline_after(zx::sec(1)), &observed), ZX_OK);

  uint32_t actual;
  ASSERT_EQ(msg_local_.read(0, buf, PAGE_SIZE, &actual, nullptr, 0, nullptr),
            ZX_OK);

  auto conn_res = reinterpret_cast<tipc_conn_rsp_body*>(ctrl_msg + 1);

  uint32_t expect = sizeof(*hdr) + sizeof(*ctrl_msg) + sizeof(*conn_res);
  EXPECT_EQ(actual, expect);

  EXPECT_EQ(ctrl_msg->type, CtrlMessage::CONNECT_RESPONSE);
  EXPECT_EQ(ctrl_msg->body_len, sizeof(*conn_res));

  EXPECT_EQ(conn_res->target, src_addr);
  EXPECT_EQ(conn_res->remote, static_cast<uint32_t>(0));
  EXPECT_EQ(conn_res->status, static_cast<uint32_t>(ZX_ERR_NO_RESOURCES));
  EXPECT_EQ(conn_res->max_msg_cnt, static_cast<uint32_t>(0));
  EXPECT_EQ(conn_res->max_msg_size, static_cast<uint32_t>(0));
}

}  // namespace ree_agent
