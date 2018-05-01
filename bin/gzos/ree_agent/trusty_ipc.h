// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/ree_agent.h>

#include <fbl/auto_lock.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <zircon/misc/fnv1hash.h>

#include "lib/fidl/cpp/binding.h"

namespace ree_agent {

class TipcChannelImpl
    : public TipcChannel,
      public fbl::SinglyLinkedListable<fbl::unique_ptr<TipcChannelImpl>> {
 private:
  zx::channel channel_;
};

class TipcPort
    : public fbl::SinglyLinkedListable<fbl::unique_ptr<TipcPortImpl>> {
 public:
  fbl::String GetKey() const { return path_; }

  static size_t GetHash(fbl::String key) { return fnv1a64str(key.c_str()); }

 private:
  fidl::Binding<TipcPort> binding_;
  fbl::String path_;
};

class TipcPortManagerImpl : public TipcPortManager {
 public:
  TipcPortManager(zx::channel loader) : loader_(fbl::move(loader)) {}

  zx_status_t FindPort(fbl::String path, TipcPortImpl*& port) {
    auto iter = port_table_.find(path);

    if (!iter.IsValid()) {
      return ZX_ERR_NOT_FOUND;
    }

    port = &(*iter);
    return ZX_OK;
  }

 private:
  using HashTable = fbl::HashTable<fbl::String, fbl::unique_ptr<TipcPortImpl>>;
  HashTable port_table_;

  zx::channel loader_;
};

}  // namespace ree_agent
