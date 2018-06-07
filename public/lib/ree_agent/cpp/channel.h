// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <ree_agent/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/ree_agent/cpp/msg_item.h"
#include "lib/ree_agent/cpp/object.h"

namespace ree_agent {

class TipcChannelImpl : public TipcChannel, public TipcObject {
 public:
  static zx_status_t Create(uint32_t num_items, size_t item_size,
                            fbl::unique_ptr<TipcChannelImpl>* out);

  auto GetInterfaceHandle() {
    fidl::InterfaceHandle<TipcChannel> handle;
    binding_.Bind(handle.NewRequest());
    return handle;
  }

  zx_status_t BindPeerInterfaceHandle(
      fidl::InterfaceHandle<TipcChannel> handle);

  zx_status_t SendMessage(void* msg, size_t msg_size);
  zx_status_t GetMessage(uint32_t* msg_id, size_t* len);
  zx_status_t ReadMessage(uint32_t msg_id, uint32_t offset, void* buf,
                          size_t* buf_size);
  zx_status_t PutMessage(uint32_t msg_id);

  bool is_bound() {
    return binding_.is_bound() && peer_.is_bound() &&
           (peer_shared_items_.size() == num_items_);
  }

 protected:
  ObjectType get_type() override { return ObjectType::CHANNEL; }

  void Close() override;
  void RequestSharedMessageItems(
      RequestSharedMessageItemsCallback callback) override;
  void GetFreeMessageItem(GetFreeMessageItemCallback callback) override;
  void NotifyMessageItemIsFilled(
      uint32_t msg_id, uint64_t msg_size,
      NotifyMessageItemIsFilledCallback callback) override;

 private:
  TipcChannelImpl() : binding_(this) {}

  using MsgList = fbl::DoublyLinkedList<fbl::unique_ptr<MessageItem>>;
  fbl::Mutex msg_list_lock_;
  MsgList free_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList outgoing_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList filled_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList read_list_ FXL_GUARDED_BY(msg_list_lock_);

  fidl::Binding<TipcChannel> binding_;

  TipcChannelSyncPtr peer_;
  std::vector<fbl::unique_ptr<MessageItem>> peer_shared_items_;
  size_t num_items_;
};

}  // namespace ree_agent
