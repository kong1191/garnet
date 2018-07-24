// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>
#include <vector>

#include <fbl/intrusive_double_list.h>
#include <fbl/mutex.h>
#include <fbl/unique_ptr.h>
#include <fs/synchronous-vfs.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <sysmgr/cpp/fidl.h>

#include "lib/app/cpp/service_provider_impl.h"
#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_annotations.h"

namespace sysmgr {

// The dynamic service model is created just for trusty application only, since
// without dirty hacks, it is not easy to implement trusty api using fuchsia's
// standard service model. Thus we create additional fidl interface for
// turst app only. This may be dropped in the future.
//
// DO NOT create additional customized service model for other protocols. We
// should follow fuchsia's standard.
class TrustyApp;
class TrustyServiceProvider : public ServiceProvider {
 public:
  TrustyServiceProvider(fbl::RefPtr<fs::PseudoDir> root, TrustyApp* app);
  TrustyServiceProvider() = delete;

  void BindProvider(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
    service_provider_.AddBinding(std::move(request));
  }
  void Bind(fidl::InterfaceRequest<ServiceProvider> request) {
    binding_.Bind(std::move(request));
  }
  void Bind(zx::channel channel) { binding_.Bind(std::move(channel)); }
  void RemoveService(std::string service_name);
  void RemoveAllServices();

  auto& controller() { return controller_; }

 protected:
  // Overridden from |sysmgr::ServiceProvider|
  void LookupService(fidl::StringPtr service_name,
                     LookupServiceCallback callback) override;
  void AddService(fidl::StringPtr service_name,
                  AddServiceCallback callback) override;
  void ConnectToService(fidl::StringPtr service_name,
                        zx::channel channel) override;
  void WaitOnService(fidl::StringPtr service_name,
                     WaitOnServiceCallback callback) override;
  void RequestService(fidl::StringPtr service_name) override;

 private:
  fidl::Binding<ServiceProvider> binding_;
  fbl::RefPtr<fs::PseudoDir> root_;

  TrustyApp* app_;
  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::sys::ServiceProviderImpl service_provider_;

  std::vector<std::string> registered_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TrustyServiceProvider);
};

class TrustyApp {
 public:
  explicit TrustyApp();
  ~TrustyApp() = default;

  void RequestService(fidl::StringPtr service_name);
  void WaitOnService(fidl::StringPtr service_name,
                     ServiceProvider::WaitOnServiceCallback callback);
  void NotifyServiceAdded(std::string service_name);

 private:
  zx::channel OpenAsDirectory();
  void ScanPublicServices();

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;

  // map of service name and app name
  std::map<std::string, std::string> public_services_;

  // map of app name and it's service provider
  std::map<std::string, std::unique_ptr<TrustyServiceProvider>> launched_apps_;

  // Nested environment within which the apps started by sysmgr will run.
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr env_launcher_;

  fbl::RefPtr<fs::PseudoDir> root_;
  fs::SynchronousVfs vfs_;

  struct ServiceWaiter
      : public fbl::DoublyLinkedListable<fbl::unique_ptr<ServiceWaiter>> {
    std::string service_name;
    ServiceProvider::WaitOnServiceCallback callback;
  };

  fbl::Mutex mutex_;
  fbl::DoublyLinkedList<fbl::unique_ptr<ServiceWaiter>> waiters_
      FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_AND_ASSIGN(TrustyApp);
};

}  // namespace sysmgr
