// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/intrusive_single_list.h>
#include <fbl/mutex.h>
#include <fbl/string.h>
#include <ree_agent/cpp/fidl.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/ree_agent/cpp/msg_item.h"
#include "lib/ree_agent/cpp/object.h"

namespace ree_agent {

class TipcChannelImpl
    : public TipcChannel,
      public TipcObject,
      public fbl::SinglyLinkedListable<fbl::RefPtr<TipcChannelImpl>> {
 public:
  static zx_status_t Create(uint32_t num_items, size_t item_size,
                            fbl::RefPtr<TipcChannelImpl>* out);

  auto GetInterfaceHandle() {
    fidl::InterfaceHandle<TipcChannel> handle;
    binding_.Bind(handle.NewRequest());
    return handle;
  }

  void BindPeerInterfaceHandle(fidl::InterfaceHandle<TipcChannel> handle) {
    peer_.Bind(std::move(handle));
  }

  zx_status_t SendMessage(void* msg, size_t msg_size);
  zx_status_t GetMessage(uint32_t* msg_id, size_t* len);
  zx_status_t ReadMessage(uint32_t msg_id, uint32_t offset, void* buf,
                          size_t* buf_size);
  zx_status_t PutMessage(uint32_t msg_id);

  bool is_bound() { return peer_.is_bound(); }

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
  TipcChannelImpl() : binding_(this), peer_shared_items_ready_(false) {}

  using MsgList = fbl::DoublyLinkedList<fbl::unique_ptr<MessageItem>>;
  fbl::Mutex msg_list_lock_;
  MsgList free_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList outgoing_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList filled_list_ FXL_GUARDED_BY(msg_list_lock_);
  MsgList read_list_ FXL_GUARDED_BY(msg_list_lock_);

  fidl::Binding<TipcChannel> binding_;
  size_t num_items_;

  TipcChannelSyncPtr peer_;
  std::vector<fbl::unique_ptr<MessageItem>> peer_shared_items_;

  fbl::Mutex request_shared_items_lock_;
  bool peer_shared_items_ready_ FXL_GUARDED_BY(request_shared_items_lock_);
  zx_status_t PopulatePeerSharedItemsLocked()
      FXL_EXCLUSIVE_LOCKS_REQUIRED(request_shared_items_lock_);
};

}  // namespace ree_agent
