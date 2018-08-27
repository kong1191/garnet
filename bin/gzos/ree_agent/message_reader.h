// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/auto_lock.h>
#include <fbl/mutex.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fxl/logging.h>
#include <zircon/syscalls.h>

namespace ree_agent {

static constexpr uint32_t kDefaultReservedSize = 128;

class Message {
 public:
  Message(uint8_t* buf_ptr, size_t capacity, size_t reserved)
      : reserved_(reserved),
        capacity_(capacity),
        actual_(0u),
        data_(buf_ptr + reserved_) {}

  Message(const Message& other) = delete;
  Message& operator=(const Message& other) = delete;

  Message(Message&& other)
      : reserved_(other.reserved_),
        capacity_(other.capacity_),
        actual_(other.actual_),
        data_(other.data_) {
    other.data_ = nullptr;
    other.capacity_ = 0u;
    other.actual_ = 0u;
    other.reserved_ = 0u;
  }

  zx_status_t Read(zx_handle_t channel, uint32_t flags) {
    uint32_t actual_bytes = 0u;
    zx_status_t status = zx_channel_read(channel, flags, data_, nullptr,
                                         capacity_, 0, &actual_bytes, nullptr);
    if (status == ZX_OK) {
      actual_ = actual_bytes;
    }
    return status;
  }

  zx_status_t Write(zx_handle_t channel, uint32_t flags) {
    zx_status_t status =
        zx_channel_write(channel, flags, data_, actual_, nullptr, 0);
    return status;
  }

  template<typename T>
  T* Alloc() {
    FXL_CHECK(sizeof(T) <= reserved_);

    reserved_ -= sizeof(T);
    data_ -= sizeof(T);
	actual_ += sizeof(T);

    return reinterpret_cast<T*>(data_);
  }

  uint8_t* data() { return data_; }
  size_t actual() { return actual_; }
  size_t capacity() { return capacity_; }

 private:
  size_t reserved_;
  size_t capacity_;
  size_t actual_;
  uint8_t* data_;
};

class MessageBuffer {
 public:
  MessageBuffer(size_t capacity = PAGE_SIZE,
                size_t reserved = kDefaultReservedSize)
      : buf_ptr_(new uint8_t[capacity]),
        reserved_(reserved),
        capacity_(capacity) {
    FXL_CHECK(buf_ptr_.get());
  }

  Message CreateEmptyMessage() {
    return Message(buf_ptr_.get(), capacity_, reserved_);
  }

 private:
  fbl::unique_ptr<uint8_t> buf_ptr_;
  size_t reserved_;
  size_t capacity_;
};

class MessageHandler {
 public:
  virtual ~MessageHandler() = default;

  virtual zx_status_t OnMessage(Message message) = 0;
};

class MessageReader {
 public:
  MessageReader(MessageHandler* message_handler, zx_handle_t channel,
                size_t max_message_size, async_t* async = async_get_default())
      : message_handler_(message_handler),
        channel_(channel),
        max_message_size_(max_message_size),
        async_(async) {}
  ~MessageReader() { Stop(); }
  zx_status_t Start() { return WaitOnChannel(); }

  void Stop() { wait_.Cancel(); }

 private:
  zx_status_t WaitOnChannel() {
    wait_.set_object(channel_);
    wait_.set_trigger(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED);
    return wait_.Begin(async_);
  }

  void OnMessage(async_t* async, async::WaitBase* wait, zx_status_t status,
                 const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Error occurs during async wait: " << status;
      Stop();
      return;
    }

    if (signal->observed & ZX_CHANNEL_READABLE) {
      MessageBuffer buffer(max_message_size_);

      for (uint64_t i = 0; i < signal->count; i++) {
        auto message = buffer.CreateEmptyMessage();
        status = message.Read(channel_, 0);
        if (status == ZX_ERR_SHOULD_WAIT) {
          break;
        }
        if (status != ZX_OK) {
          Stop();
          return;
        }

        status = message_handler_->OnMessage(std::move(message));
        if (status != ZX_OK) {
          FXL_LOG(ERROR) << "Failed to handle message: " << status;
          Stop();
          return;
        }
      }

      status = WaitOnChannel();
      if (status != ZX_OK) {
        FXL_LOG(ERROR) << "Failed to begin async wait: " << status;
        Stop();
        return;
      }
    }

    FXL_CHECK(signal->observed & ZX_CHANNEL_PEER_CLOSED);
    Stop();
  }

  MessageHandler* message_handler_;
  zx_handle_t channel_;
  size_t max_message_size_;
  async::WaitMethod<MessageReader, &MessageReader::OnMessage> wait_{this};
  async_t* const async_;
};

}  // namespace ree_agent
