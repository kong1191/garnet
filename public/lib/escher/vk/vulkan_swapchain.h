// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_VK_VULKAN_SWAPCHAIN_H_
#define LIB_ESCHER_VK_VULKAN_SWAPCHAIN_H_

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "lib/escher/forward_declarations.h"
#include "lib/escher/vk/image.h"

namespace escher {

// Encapsulate a VkSwapchain along with associated metadata.
struct VulkanSwapchain {
  vk::SwapchainKHR swapchain;
  std::vector<ImagePtr> images;
  uint32_t width;
  uint32_t height;
  vk::Format format;
  vk::ColorSpaceKHR color_space;

  VulkanSwapchain(vk::SwapchainKHR swapchain_in,
                  std::vector<ImagePtr> images_in, uint32_t width_in,
                  uint32_t height_in, vk::Format format_in,
                  vk::ColorSpaceKHR color_space_in);

  VulkanSwapchain();
};

}  // namespace escher

#endif  // LIB_ESCHER_VK_VULKAN_SWAPCHAIN_H_
