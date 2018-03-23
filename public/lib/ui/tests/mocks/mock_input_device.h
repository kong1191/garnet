// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_TESTS_MOCKS_MOCK_INPUT_DEVICE_H_
#define LIB_UI_TESTS_MOCKS_MOCK_INPUT_DEVICE_H_

#include <fuchsia/cpp/input.h>
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"

namespace mozart {
namespace test {

using OnReportCallback = std::function<void(input::InputReportPtr report)>;

class MockInputDevice : public input::InputDevice {
 public:
  MockInputDevice(
      uint32_t device_id,
      input::DeviceDescriptorPtr descriptor,
      fidl::InterfaceRequest<input::InputDevice> input_device_request,
      const OnReportCallback& on_report_callback);
  ~MockInputDevice();

  uint32_t id() { return id_; }
  mozart::DeviceDescriptor* descriptor() { return descriptor_.get(); }

  // |InputDevice|
  void DispatchReport(input::InputReportPtr report) override;

 private:
  uint32_t id_;
  input::DeviceDescriptorPtr descriptor_;
  fidl::Binding<input::InputDevice> input_device_binding_;
  OnReportCallback on_report_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MockInputDevice);
};

}  // namespace test
}  // namespace mozart

#endif  // LIB_UI_TESTS_MOCKS_MOCK_INPUT_DEVICE_H_
