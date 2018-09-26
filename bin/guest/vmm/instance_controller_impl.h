// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_INSTANCE_CONTROLLER_IMPL_H_
#define GARNET_BIN_GUEST_VMM_INSTANCE_CONTROLLER_IMPL_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>

#include "garnet/lib/machina/input_dispatcher_impl.h"

// Provides an implementation of the |fuchsia::guest::InstanceController|
// interface. This exposes some guest services over FIDL.
class InstanceControllerImpl : public fuchsia::guest::InstanceController {
 public:
  InstanceControllerImpl();

  zx_status_t AddPublicService(component::StartupContext* context);

  void SetViewProvider(fuchsia::ui::viewsv1::ViewProvider* view_provider) {
    view_provider_ = view_provider;
  }
  void SetInputDispatcher(machina::InputDispatcherImpl* input_dispatcher) {
    input_dispatcher_ = input_dispatcher;
  }

  // Extracts the socket handle to be used for the host end of serial
  // communication. The other end of this socket will be provided to clients
  // via |GetSerial|.
  zx::socket TakeSocket() { return std::move(socket_); }

  // |fuchsia::guest::InstanceController|
  void GetSerial(GetSerialCallback callback) override;
  void GetViewProvider(
      fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewProvider> request)
      override;
  void GetInputDispatcher(
      fidl::InterfaceRequest<fuchsia::ui::input::InputDispatcher> request)
      override;

 private:
  fidl::BindingSet<fuchsia::guest::InstanceController> bindings_;
  fidl::BindingSet<fuchsia::ui::viewsv1::ViewProvider> view_provider_bindings_;
  fidl::BindingSet<fuchsia::ui::input::InputDispatcher>
      input_dispatcher_bindings_;

  zx::socket socket_;
  zx::socket remote_socket_;
  fuchsia::ui::viewsv1::ViewProvider* view_provider_;
  machina::InputDispatcherImpl* input_dispatcher_;
};

#endif  // GARNET_BIN_GUEST_VMM_INSTANCE_CONTROLLER_IMPL_H_
