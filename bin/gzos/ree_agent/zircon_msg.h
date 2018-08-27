// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>

namespace ree_agent {

static constexpr uint32_t kMaxServerNameLength = 256;
static constexpr uint32_t kMaxEndpointNumber = 1024;
static constexpr uint32_t kCtrlEndpointAddress = 4096;

struct zircon_msg_hdr {
  uint32_t src;
  uint32_t dst;
  uint32_t reserved;
  uint16_t len;
  uint16_t flags;
  uint8_t data[0];
} __PACKED;

enum class CtrlMessageType : uint32_t {
  CONNECT_REQUEST,
  CONNECT_RESPONSE,
  DISCONNECT_REQUEST,
};

struct zircon_ctrl_msg_hdr {
  CtrlMessageType type;
  uint32_t body_len;
} __PACKED;

struct zircon_conn_req_body {
  char name[kMaxServerNameLength];
} __PACKED;

struct zircon_conn_rsp_body {
  uint32_t status;
  uint32_t remote_addr;
} __PACKED;

struct zircon_disc_req_body {
  uint32_t target;
} __PACKED;

struct zircon_endpoint_msg_hdr {
  uint32_t reserved;
} __PACKED;


};  // namespace ree_agent
