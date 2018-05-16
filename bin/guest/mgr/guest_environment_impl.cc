// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/guest_environment_impl.h"

#include "lib/fxl/logging.h"

namespace guestmgr {

GuestEnvironmentImpl::GuestEnvironmentImpl(
    uint32_t id, const std::string& label,
    component::ApplicationContext* context,
    fidl::InterfaceRequest<guest::GuestEnvironment> request)
    : id_(id),
      label_(label),
      context_(context),
      host_socket_endpoint_(guest::kHostCid) {
  CreateApplicationEnvironment(label);
  AddBinding(std::move(request));
  FXL_CHECK(socket_server_.AddEndpoint(&host_socket_endpoint_) == ZX_OK);
}

GuestEnvironmentImpl::~GuestEnvironmentImpl() = default;

void GuestEnvironmentImpl::AddBinding(
    fidl::InterfaceRequest<GuestEnvironment> request) {
  bindings_.AddBinding(this, std::move(request));
}

void GuestEnvironmentImpl::set_unbound_handler(std::function<void()> handler) {
  bindings_.set_empty_set_handler(std::move(handler));
}

void GuestEnvironmentImpl::LaunchGuest(
    guest::GuestLaunchInfo launch_info,
    fidl::InterfaceRequest<guest::GuestController> controller,
    LaunchGuestCallback callback) {
  component::Services guest_services;
  component::ApplicationControllerPtr guest_app_controller;
  component::ApplicationLaunchInfo guest_launch_info;
  guest_launch_info.url = launch_info.url;
  guest_launch_info.arguments = std::move(launch_info.vmm_args);
  guest_launch_info.directory_request = guest_services.NewRequest();
  guest_launch_info.flat_namespace = std::move(launch_info.flat_namespace);
  app_launcher_->CreateApplication(std::move(guest_launch_info),
                                   guest_app_controller.NewRequest());

  // Setup Socket Endpoint
  uint32_t cid = next_guest_cid_++;
  auto vsock_endpoint = std::make_unique<RemoteVsockEndpoint>(cid);
  zx_status_t status = socket_server_.AddEndpoint(vsock_endpoint.get());
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "Failed to allocate socked endpoint on CID " << cid
                   << ": " << status;
    callback(guest::GuestInfo());
    return;
  }
  guest::SocketEndpointPtr remote_endpoint;
  guest_services.ConnectToService(remote_endpoint.NewRequest());
  vsock_endpoint->BindSocketEndpoint(std::move(remote_endpoint));

  guest_app_controller.set_error_handler([this, cid] { guests_.erase(cid); });
  auto& label = launch_info.label ? launch_info.label : launch_info.url;
  auto holder = std::make_unique<GuestHolder>(
      cid, label, std::move(vsock_endpoint), std::move(guest_services),
      std::move(guest_app_controller));
  holder->AddBinding(std::move(controller));
  guests_.insert({cid, std::move(holder)});

  guest::GuestInfo info;
  info.cid = cid;
  info.label = label;
  callback(std::move(info));
}

void GuestEnvironmentImpl::GetHostSocketEndpoint(
    fidl::InterfaceRequest<guest::ManagedSocketEndpoint> request) {
  host_socket_endpoint_.AddBinding(std::move(request));
}

fidl::VectorPtr<guest::GuestInfo> GuestEnvironmentImpl::ListGuests() {
  fidl::VectorPtr<guest::GuestInfo> guest_infos =
      fidl::VectorPtr<guest::GuestInfo>::New(0);
  for (const auto& it : guests_) {
    guest::GuestInfo guest_info;
    guest_info.cid = it.first;
    guest_info.label = it.second->label();
    guest_infos.push_back(std::move(guest_info));
  }
  return guest_infos;
}

void GuestEnvironmentImpl::ListGuests(ListGuestsCallback callback) {
  callback(ListGuests());
}

void GuestEnvironmentImpl::ConnectToGuest(
    uint32_t id, fidl::InterfaceRequest<guest::GuestController> request) {
  const auto& it = guests_.find(id);
  if (it == guests_.end()) {
    return;
  }
  it->second->AddBinding(std::move(request));
}

void GuestEnvironmentImpl::CreateApplicationEnvironment(
    const std::string& label) {
  context_->environment()->CreateNestedEnvironment(
      service_provider_bridge_.OpenAsDirectory(), env_.NewRequest(),
      env_controller_.NewRequest(), label);
  env_->GetApplicationLauncher(app_launcher_.NewRequest());
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0) {
    return;
  }
  context_->environment()->GetDirectory(std::move(h1));
  service_provider_bridge_.set_backing_dir(std::move(h2));
}

}  // namespace guestmgr
