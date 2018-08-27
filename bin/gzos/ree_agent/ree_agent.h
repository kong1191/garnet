// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/wait.h>

#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"

#include "garnet/bin/gzos/ree_agent/message_reader.h"

#include <ree_agent/cpp/fidl.h>

namespace ree_agent {

class Agent {
 public:
  Agent() = delete;

  Agent(MessageHandler* handler, zx::channel message_channel,
        size_t max_message_size)
      : message_reader_(handler, message_channel.get(), max_message_size),
        message_channel_(fbl::move(message_channel)),
        max_message_size_(max_message_size) {}

  zx_status_t Write(void* buf, size_t size) {
    if (buf == nullptr)
      return ZX_ERR_INVALID_ARGS;

    if (size > max_message_size_) {
      FXL_LOG(ERROR) << "message size over max_message_size, size=0x"
                     << std::hex << size;
      return ZX_ERR_BUFFER_TOO_SMALL;
    }

    return message_channel_.write(0, buf, size, NULL, 0);
  }

  size_t max_message_size() { return max_message_size_; }
  zx_handle_t message_channel() { return message_channel_.get(); }

 protected:
  MessageReader message_reader_;

 private:
  zx::channel message_channel_;
  size_t max_message_size_;
};

class ReeAgent : public Agent, public MessageHandler {
 public:
  enum State { START, STOP };

  ReeAgent(uint32_t id, zx::channel ch, size_t max_msg_size);
  virtual ~ReeAgent();

  virtual zx_status_t Start();
  virtual zx_status_t Stop();

  uint32_t message_channel_id() { return message_channel_id_; }

 protected:
  virtual zx_status_t HandleMessage(void* buf, size_t size) = 0;

 private:
  zx_status_t OnMessage(Message message) override;

  uint32_t message_channel_id_;

  fbl::Mutex lock_;
  State state FXL_GUARDED_BY(lock_);

  FXL_DISALLOW_COPY_AND_ASSIGN(ReeAgent);
};

}  // namespace ree_agent
