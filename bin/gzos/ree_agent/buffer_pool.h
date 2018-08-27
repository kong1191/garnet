// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <lib/fxl/logging.h>

#include "lib/fxl/synchronization/thread_annotations.h"

namespace ree_agent {

class BufferPool {
 public:
  BufferPool(size_t buf_size) : buf_size_(buf_size) {
    buf_ptr_.reset(new uint8_t[buf_size]);
    FXL_CHECK(buf_ptr_.get());
  }

  uint8_t* get_buffer() FXL_ACQUIRE(mutex) {
    mutex.Acquire();
    return buf_ptr_.get();
  }

  void put_buffer() FXL_RELEASE(mutex) { mutex.Release(); }

  size_t buf_size() { return buf_size_; }

 private:
  fbl::unique_ptr<uint8_t> buf_ptr_;
  fbl::Mutex mutex;
  size_t buf_size_;
};

class AutoBuffer {
 public:
  explicit AutoBuffer(BufferPool* pool) : pool_(pool) {
    data_ = pool_->get_buffer();
  }

  ~AutoBuffer() { pool_->put_buffer(); }

  uint8_t* data() { return data_; }
  size_t size() { return pool_->buf_size(); }

 private:
  BufferPool* pool_;
  uint8_t* data_;
};

}  // namespace ree_agent
