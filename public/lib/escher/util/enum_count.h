// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstddef>

namespace escher {

// Return the number of elements in an enum, which must properly define
// kEnumCount.  For example, see escher/vk/shader_stage.h
template <typename E>
constexpr size_t EnumCount() {
  return static_cast<size_t>(E::kEnumCount);
}

}  // namespace escher
