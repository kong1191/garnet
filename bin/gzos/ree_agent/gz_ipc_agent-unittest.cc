// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/unique_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/binding.h>

#include "garnet/bin/gzos/ree_agent/gz_ipc_server.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_client.h"
#include "gtest/gtest.h"

#include <gz_ipc_agent_test/cpp/fidl.h>

namespace ree_agent {

class EchoServiceImpl : public gz_ipc_agent_test::EchoService {
 public:
  EchoServiceImpl() : binding_(this) {}

  void Bind(fidl::InterfaceRequest<gz_ipc_agent_test::EchoService> request) {
    binding_.Bind(std::move(request));
  }

  void Unbind() { binding_.Unbind(); }

  auto& binding() { return binding_; }

  void EchoString(fidl::StringPtr value, EchoStringCallback callback) override {
    callback(value);
  }

 private:
  fidl::Binding<gz_ipc_agent_test::EchoService> binding_;
};

class GzIpcAgentTest : public ::testing::Test, public TaServices {
 public:
  GzIpcAgentTest()
      : loop_(&kAsyncLoopConfigMakeDefault),
        vfs_(async_get_default()),
        root_(fbl::AdoptRef(new fs::PseudoDir)) {}

  void ConnectToService(zx::channel request,
                        const std::string& service_name) override {
    fdio_service_connect_at(root_handle_.get(), service_name.c_str(),
                            request.release());
  }

  virtual void SetUp() override {
    ASSERT_EQ(zx::event::create(0, &event_), ZX_OK);

    ASSERT_EQ(loop_.StartThread(), ZX_OK);

    zx::channel ch0, ch1;
    ASSERT_EQ(zx::channel::create(0, &ch0, &ch1), ZX_OK);

    // Create server IPC Agent
    size_t max_message_size = PAGE_SIZE;
    server_ = fbl::make_unique<GzIpcServer>(0, std::move(ch0), max_message_size,
                                            *this);
    ASSERT_TRUE(server_ != nullptr);
    ASSERT_EQ(server_->Start(), ZX_OK);

    // Create client IPC Agent
    client_ = fbl::make_unique<GzIpcClient>(0, std::move(ch1), max_message_size);
    ASSERT_TRUE(client_ != nullptr);
    ASSERT_EQ(client_->Start(), ZX_OK);

    ASSERT_EQ(zx::channel::create(0, &root_handle_, &ch0), ZX_OK);
    ASSERT_EQ(vfs_.ServeDirectory(root_, std::move(ch0)), ZX_OK);

    // Register Echo service
    auto child = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
      fidl::InterfaceRequest<gz_ipc_agent_test::EchoService> request(
          std::move(channel));
      echo_service_impl_.Bind(std::move(request));
      return ZX_OK;
    }));
    root_->AddEntry(gz_ipc_agent_test::EchoService::Name_, std::move(child));
  }

 protected:
  void SignalEvent() { event_.signal(0, ZX_USER_SIGNAL_0); }

  zx_status_t WaitEvent() {
    return event_.wait_one(ZX_USER_SIGNAL_0, zx::deadline_after(zx::msec(1)),
                           nullptr);
  }

  fbl::unique_ptr<GzIpcClient> client_;
  fbl::unique_ptr<GzIpcAgent> server_;

  EchoServiceImpl echo_service_impl_;

 private:
  zx::channel root_handle_;
  async::Loop loop_;
  zx::event event_;

  fs::SynchronousVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> root_;
};

TEST_F(GzIpcAgentTest, AsyncEchoRequest) {
  gz_ipc_agent_test::EchoServicePtr echo;
  ASSERT_EQ(client_->Connect(gz_ipc_agent_test::EchoService::Name_,
                             echo.NewRequest().TakeChannel()),
            ZX_OK);

  async::PostTask(async_get_default(), [this, &echo] {
    std::string test_string = "async_echo_test";
    echo->EchoString(test_string, [this, test_string](std::string response) {
      EXPECT_EQ(test_string, response);
      SignalEvent();
    });
  });

  EXPECT_EQ(WaitEvent(), ZX_OK);
}

TEST_F(GzIpcAgentTest, AsyncBadService) {
  gz_ipc_agent_test::EchoServicePtr echo;

  bool error_handler_triggered = false;
  echo.set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  ASSERT_EQ(client_->Connect("none-existence service",
                             echo.NewRequest().TakeChannel()),
            ZX_OK);

  EXPECT_EQ(WaitEvent(), ZX_ERR_TIMED_OUT);
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, SyncEchoRequest) {
  gz_ipc_agent_test::EchoServiceSyncPtr echo;
  ASSERT_EQ(client_->Connect(gz_ipc_agent_test::EchoService::Name_,
                             echo.NewRequest().TakeChannel()),
            ZX_OK);

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_TRUE(echo->EchoString(test_string, &response));
  EXPECT_EQ(test_string, response);
}

TEST_F(GzIpcAgentTest, SyncBadService) {
  gz_ipc_agent_test::EchoServiceSyncPtr echo;
  ASSERT_EQ(client_->Connect("none-existence service",
                             echo.NewRequest().TakeChannel()),
            ZX_OK);

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_FALSE(echo->EchoString(test_string, &response));
}

TEST_F(GzIpcAgentTest, ClientUnbind) {
  bool error_handler_triggered = false;
  echo_service_impl_.binding().set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  gz_ipc_agent_test::EchoServiceSyncPtr echo;
  ASSERT_EQ(client_->Connect(gz_ipc_agent_test::EchoService::Name_,
                             echo.NewRequest().TakeChannel()),
            ZX_OK);
  echo.Unbind();

  EXPECT_EQ(WaitEvent(), ZX_ERR_TIMED_OUT);
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, AsyncServerUnbind) {
  gz_ipc_agent_test::EchoServicePtr echo;

  bool error_handler_triggered = false;
  echo.set_error_handler(
      [&error_handler_triggered] { error_handler_triggered = true; });

  ASSERT_EQ(client_->Connect(gz_ipc_agent_test::EchoService::Name_,
                             echo.NewRequest().TakeChannel()),
            ZX_OK);
  echo_service_impl_.Unbind();

  EXPECT_EQ(WaitEvent(), ZX_ERR_TIMED_OUT);
  EXPECT_TRUE(error_handler_triggered);
}

TEST_F(GzIpcAgentTest, SyncServerUnbind) {
  gz_ipc_agent_test::EchoServiceSyncPtr echo;
  ASSERT_EQ(client_->Connect(gz_ipc_agent_test::EchoService::Name_,
                             echo.NewRequest().TakeChannel()),
            ZX_OK);

  echo_service_impl_.Unbind();

  std::string test_string = "sync_echo_test";
  fidl::StringPtr response;
  EXPECT_FALSE(echo->EchoString(test_string, &response));
}

}  // namespace ree_agent
