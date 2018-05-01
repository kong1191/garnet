// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fuchsia/cpp/ree_agent.h>

#include <fbl/auto_lock.h>
#include <fbl/function.h>
#include <fbl/intrusive_hash_table.h>
#include <fbl/string.h>
#include <fbl/unique_ptr.h>
#include <zircon/misc/fnv1hash.h>

#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/binding_set.h"

namespace ree_agent {

class TipcPortTableEntry
    : public fbl::SinglyLinkedListable<fbl::unique_ptr<TipcPortTableEntry>> {
 public:
  TipcPortTableEntry(const fbl::String path,
                     fidl::InterfaceHandle<TipcPortListener> listener)
      : path_(path) {
    listener_.Bind(fbl::move(listener));
  }

  auto GetKey() const { return path_; }
  static size_t GetHash(fbl::String key) { return fnv1a64str(key.c_str()); }

  auto path() const { return path_; }

 private:
  fbl::String path_;
  fidl::InterfacePtr<TipcPortListener> listener_;
};

class TipcPortManagerImpl : TipcPortManager {
 public:
  TipcPortManagerImpl() {}

  void Bind(fidl::InterfaceRequest<TipcPortManager> request) {
    if (request)
      bindings_.AddBinding(this, std::move(request));
  }

  zx_status_t Find(fbl::String path, TipcPortTableEntry*& entry);

  // TipcPortManager fidl implementation
  void Publish(fidl::StringPtr port_path,
               fidl::InterfaceHandle<TipcPortListener> listener,
               PublishCallback callback) override;

 private:
  using HashTable =
      fbl::HashTable<fbl::String, fbl::unique_ptr<TipcPortTableEntry>>;
  HashTable port_table_;
  fidl::BindingSet<TipcPortManager> bindings_;
};

}  // namespace ree_agent
