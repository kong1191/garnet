// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/synchronous-vfs.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/wait.h>
#include <lib/fdio/util.h>
#include <lib/fidl/cpp/binding.h>

#include "garnet/bin/gzos/ree_agent/gz_ipc_agent.h"
#include "garnet/bin/gzos/ree_agent/gz_ipc_endpoint.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"
#include "gtest/gtest.h"

#include <gz_ipc_agent_test/cpp/fidl.h>
#include <map>

namespace ree_agent {

class GzIpcClient : public MessageHandler, public Agent {
 public:
  GzIpcClient(zx::channel message_channel, size_t max_message_size)
      : Agent(this, std::move(message_channel), max_message_size) {
    FXL_CHECK(message_reader_.Start() == ZX_OK);
  }

  zx_status_t SendMessageToTee(uint32_t local, uint32_t remote, void* data,
                               size_t data_len) {
    size_t msg_size = sizeof(gz_ipc_msg_hdr) + data_len;
    fbl::unique_ptr<char> buf(new char[msg_size]);

    if (buf == nullptr)
      return ZX_ERR_NO_MEMORY;

    auto hdr = reinterpret_cast<gz_ipc_msg_hdr*>(buf.get());
    hdr->src = local;
    hdr->dst = remote;
    hdr->reserved = 0;
    hdr->len = data_len;
    hdr->flags = 0;
    memcpy(hdr->data, data, data_len);

    return Write(buf.get(), msg_size);
  }

  zx_status_t Connect(std::string service_name, zx::channel channel) {
    fbl::AutoLock lock(&lock_);

    conn_req_msg req_msg;
    req_msg.hdr.type = CtrlMessageType::CONNECT_REQUEST;
    req_msg.hdr.body_len = sizeof(gz_ipc_conn_req_body);
    strncpy(req_msg.body.name, service_name.c_str(), sizeof(req_msg.body.name));

    uint32_t local_addr;
    zx_status_t status = id_allocator_.Alloc(&local_addr);
    if (status != ZX_OK) {
      return status;
    }
    auto free_local_addr = fbl::MakeAutoCall(
        [this, &local_addr]() { id_allocator_.Free(local_addr); });

    status = SendMessageToTee(local_addr, kCtrlEndpointAddress, &req_msg,
                              sizeof(req_msg));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to write connect req: " << status;
      return status;
    }

    auto endpoint = fbl::make_unique<GzIpcEndpoint>(
        this, local_addr, kInvalidEndpointAddress, std::move(channel));
    if (!endpoint) {
      return ZX_ERR_NO_MEMORY;
    }

    endpoint->reader().set_error_handler(
        [this, local_addr]() { Disconnect(local_addr); });
    endpoint_table_.emplace(local_addr, std::move(endpoint));

    free_local_addr.cancel();
    return ZX_OK;
  }

  void Disconnect(uint32_t local_addr) {
    fbl::AutoLock lock(&lock_);

    auto it = endpoint_table_.find(local_addr);
    if (it != endpoint_table_.end()) {
      auto& endpoint = it->second;
      if (endpoint->remote_addr() != kInvalidEndpointAddress) {
        disc_req_msg disc_req{
            {CtrlMessageType::DISCONNECT_REQUEST, sizeof(gz_ipc_disc_req_body)},
            {endpoint->remote_addr()}};

        FXL_CHECK(SendMessageToTee(local_addr, kCtrlEndpointAddress, &disc_req,
                                   sizeof(disc_req)) == ZX_OK);
      }
      endpoint_table_.erase(it);
    }

    id_allocator_.Free(local_addr);
  }

 private:
  zx_status_t HandleCtrlMessage(void* msg, size_t msg_size) {
    if (msg_size < sizeof(gz_ipc_ctrl_msg_hdr)) {
      FXL_LOG(ERROR) << "Invalid ctrl msg";
      return ZX_ERR_INTERNAL;
    }

    auto ctrl_hdr = reinterpret_cast<gz_ipc_ctrl_msg_hdr*>(msg);

    switch (ctrl_hdr->type) {
      case CtrlMessageType::DISCONNECT_REQUEST: {
        if (ctrl_hdr->body_len != sizeof(gz_ipc_disc_req_body)) {
          FXL_LOG(ERROR) << "Invalid disc req msg";
          return ZX_ERR_INTERNAL;
        }

        fbl::AutoLock lock(&lock_);
        auto body = reinterpret_cast<gz_ipc_disc_req_body*>(ctrl_hdr + 1);

        endpoint_table_.erase(body->target);
        id_allocator_.Free(body->target);
      } break;

      default:
        FXL_LOG(ERROR) << "Invalid ctrl msg type";
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
  }

  zx_status_t HandleConnectResponse(fbl::unique_ptr<GzIpcEndpoint>& endpoint,
                                    void* msg, size_t msg_len) {
    if (msg_len != sizeof(conn_rsp_msg)) {
      FXL_LOG(ERROR) << "Invalid conn rsp msg";
      return ZX_ERR_INTERNAL;
    }

    auto conn_rsp = reinterpret_cast<conn_rsp_msg*>(msg);
    if (conn_rsp->hdr.type != CtrlMessageType::CONNECT_RESPONSE) {
      FXL_LOG(ERROR) << "Invalid conn rsp msg";
      return ZX_ERR_INTERNAL;
    }

    if (conn_rsp->hdr.body_len != sizeof(gz_ipc_conn_rsp_body)) {
      FXL_LOG(ERROR) << "Invalid body length";
      return ZX_ERR_INTERNAL;
    }

    zx_status_t status = conn_rsp->body.status;
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Connect request failed, status=" << status;
      return status;
    }

    endpoint->set_remote_addr(conn_rsp->body.target);
    return ZX_OK;
  }

  zx_status_t HandleEndpointMessage(gz_ipc_msg_hdr* msg_hdr) {
    fbl::AutoLock lock(&lock_);

    auto it = endpoint_table_.find(msg_hdr->dst);
    if (it != endpoint_table_.end()) {
      auto& endpoint = it->second;

      if (endpoint->remote_addr() == kInvalidEndpointAddress) {
        zx_status_t status =
            HandleConnectResponse(endpoint, msg_hdr->data, msg_hdr->len);
        if (status != ZX_OK) {
          endpoint_table_.erase(msg_hdr->dst);
          id_allocator_.Free(msg_hdr->dst);
        }
        return ZX_OK;

      } else if (endpoint->remote_addr() == msg_hdr->src) {
        return endpoint->Write(msg_hdr->data, msg_hdr->len);
      }
    }

    FXL_LOG(ERROR) << "Endpoint addr " << msg_hdr->src
                   << " not found, msg dropped";
    return ZX_OK;
  }

  zx_status_t OnMessage(Message message) override {
    if (message.actual() < sizeof(gz_ipc_msg_hdr)) {
      FXL_LOG(ERROR) << "Invalid msg";
      return ZX_ERR_INTERNAL;
    }

    auto msg_hdr = reinterpret_cast<gz_ipc_msg_hdr*>(message.data());

    if (msg_hdr->dst == kCtrlEndpointAddress) {
      return HandleCtrlMessage(msg_hdr->data, msg_hdr->len);
    }

    return HandleEndpointMessage(msg_hdr);
  }

  fbl::Mutex lock_;
  trusty_ipc::IdAllocator<kMaxEndpointNumber> id_allocator_;

  std::map<uint32_t, fbl::unique_ptr<GzIpcEndpoint>> endpoint_table_
      FXL_GUARDED_BY(lock_);
};

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
    agent_ = fbl::make_unique<GzIpcAgent>(0, std::move(ch0), max_message_size,
                                          *this);
    ASSERT_TRUE(agent_ != nullptr);
    ASSERT_EQ(agent_->Start(), ZX_OK);

    // Create client IPC Agent
    client_ = fbl::make_unique<GzIpcClient>(std::move(ch1), max_message_size);
    ASSERT_TRUE(client_ != nullptr);

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
  fbl::unique_ptr<GzIpcAgent> agent_;

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
