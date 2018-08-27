// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_call.h>
#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>
#include <map>

#include "garnet/bin/gzos/ree_agent/gzipc_endpoint.h"
#include "garnet/bin/gzos/ree_agent/gzipc_agent.h"
#include "garnet/bin/gzos/ree_agent/ta_service.h"
#include "gtest/gtest.h"
#include "lib/gzos/ree_agent/cpp/rpc_channel.h"

namespace ree_agent {

class ResponseWaiter {
 public:
  ResponseWaiter() { FXL_CHECK(zx::event::create(0, &event_) == ZX_OK); }

  zx_status_t Wait() {
    return event_.wait_one(ZX_USER_SIGNAL_0, zx::deadline_after(zx::msec(1)),
                           nullptr);
  }

  zx_status_t Signal() { return event_.signal(0, ZX_USER_SIGNAL_0); }
  conn_rsp_msg* result() { return &msg_; }

 private:
  zx::event event_;
  conn_rsp_msg msg_;
};

class GzipcClient : public MessageHandler, public Agent {
 public:
  GzipcClient(zx::channel message_channel, size_t max_message_size)
      : Agent(this, fbl::move(message_channel), max_message_size) {}

  zx_status_t Connect(std::string service_name, zx::channel channel) {
    fbl::AutoLock lock(&lock_);

    conn_req_msg req_msg;
    req_msg.hdr.type = CtrlMessageType::CONNECT_REQUEST;
    req_msg.hdr.body_len = sizeof(gzipc_conn_req_body);
    strncpy(req_msg.body.name, service_name.c_str(), service_name.size());

    zx_status_t status = Write(&req_msg, sizeof(req_msg));
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to write connect req: " << status;
      return status;
    }

    uint32_t local_addr;
    status = id_allocator_.Alloc(&local_addr);
    if (status != ZX_OK) {
      return status;
    }
    auto free_local_addr = fbl::MakeAutoCall([this, &local_addr]() {
      fbl::AutoLock lock(&lock_);
      id_allocator_.Free(local_addr);
    });

    ResponseWaiter waiter;
    waiter_list_.emplace(local_addr, &waiter);
    auto remove_waiter = fbl::MakeAutoCall([this, &local_addr]() {
      fbl::AutoLock lock(&lock_);
      waiter_list_.erase(local_addr);
    });

    status = waiter.Wait();
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to wait for connect response: " << status;
      return status;
    }

    status = waiter.result()->body.status;
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Failed to connect service, response: " << status;
      return status;
    }

    auto endpoint = fbl::make_unique<GzipcEndpoint>(
        this, local_addr, waiter.result()->body.remote, fbl::move(channel));
    if (!endpoint) {
      return ZX_ERR_NO_MEMORY;
    }
    endpoint_table_.emplace(local_addr, fbl::move(endpoint));

    free_local_addr.cancel();
    return ZX_OK;
  }

 private:
  zx_status_t HandleCtrlMessage(void* msg, size_t msg_size) {
    if (msg_size < sizeof(gzipc_ctrl_msg_hdr)) {
      FXL_LOG(ERROR) << "Invalid ctrl msg, drop it";
      return ZX_ERR_INVALID_ARGS;
    }

    auto ctrl_hdr = reinterpret_cast<gzipc_ctrl_msg_hdr*>(msg);

    switch (ctrl_hdr->type) {
      case CtrlMessageType::CONNECT_RESPONSE: {
        if (ctrl_hdr->body_len != sizeof(gzipc_conn_rsp_body)) {
          FXL_LOG(ERROR) << "Invalid conn rsp msg, drop it";
          return ZX_ERR_INVALID_ARGS;
        }

        fbl::AutoLock lock(&lock_);
        auto body = reinterpret_cast<gzipc_conn_rsp_body*>(ctrl_hdr + 1);
        auto it = waiter_list_.find(body->target);
        if (it != waiter_list_.end()) {
          auto waiter = it->second;
          memcpy(waiter->result(), body, sizeof(gzipc_conn_req_body));
          waiter->Signal();
        }
        break;
      }

      default:
        FXL_LOG(ERROR) << "Invalid ctrl msg type, drop it";
        return ZX_ERR_INVALID_ARGS;
    }

    return ZX_OK;
  }

  zx_status_t OnMessage(Message message) override {
    if (message.actual() < sizeof(gzipc_msg_hdr)) {
      FXL_LOG(ERROR) << "Invalid msg, drop it";
      return ZX_ERR_INVALID_ARGS;
    }

    auto msg_hdr = reinterpret_cast<gzipc_msg_hdr*>(message.data());

    if (msg_hdr->dst == kCtrlEndpointAddress) {
      return HandleCtrlMessage(msg_hdr->data, msg_hdr->len);
    }

    fbl::AutoLock lock(&lock_);
    auto it = endpoint_table_.find(msg_hdr->dst);
    if (it != endpoint_table_.end()) {
      auto& endpoint = it->second;
      if (endpoint->remote_addr() == msg_hdr->src) {
        zx_status_t status = endpoint->Write(msg_hdr->data, msg_hdr->len);
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to write msg to endpoint: " << status;
          return status;
        }
      }
    }

    return ZX_OK;
  }

  fbl::Mutex lock_;
  trusty_ipc::IdAllocator<kMaxEndpointNumber> id_allocator_
      FXL_GUARDED_BY(lock_);

  std::map<uint32_t, ResponseWaiter*> waiter_list_ FXL_GUARDED_BY(lock_);

  std::map<uint32_t, fbl::unique_ptr<GzipcEndpoint>> endpoint_table_
      FXL_GUARDED_BY(lock_);
};

class GpipcAgentTest : public ::testing::Test {
 public:
 private:
};

}  // namespace ree_agent
