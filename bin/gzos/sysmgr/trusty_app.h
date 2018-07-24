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

// The dynamic service model is created just for trusty application only, since
// without dirty hacks, it is not easy to implement trusty api using fuchsia's
// standard service model. Thus we create additional fidl interface for
// turst app only. This may be dropped in the future.
//
// DO NOT create additional customized service model for other protocols. We
// should follow fuchsia's standard.

namespace sysmgr {

class TrustyApp;
class TrustyAppInstance : public ServiceRegistry {
 public:
  TrustyAppInstance(fbl::RefPtr<fs::PseudoDir> root, TrustyApp* app,
                    std::string app_name);
  TrustyAppInstance() = delete;

  void BindServiceProvider(
      fidl::InterfaceRequest<fuchsia::sys::ServiceProvider>);
  void RemoveService(std::string service_name);
  void RemoveAllServices();

  auto& controller() { return controller_; }
  auto& service() { return service_; }

 protected:
  // Overridden from |sysmgr::ServiceRegistry|
  void LookupService(fidl::StringPtr service_name,
                     LookupServiceCallback callback) override;
  void AddService(fidl::StringPtr service_name) override;
  void WaitOnService(fidl::StringPtr service_name,
                     WaitOnServiceCallback callback) override;

 private:
  fidl::Binding<ServiceRegistry> binding_;
  fbl::RefPtr<fs::PseudoDir> root_;

  TrustyApp* app_;
  std::string app_name_;

  fuchsia::sys::ComponentControllerPtr controller_;
  fuchsia::sys::Services service_;
  fuchsia::sys::ServiceProviderImpl service_provider_;

  std::vector<std::string> registered_services_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TrustyAppInstance);
};

class TrustyApp {
 public:
  explicit TrustyApp();
  ~TrustyApp() = default;

  void LookupService(fidl::StringPtr service_name,
                     ServiceRegistry::LookupServiceCallback callback);
  void WaitOnService(fidl::StringPtr service_name,
                     ServiceRegistry::WaitOnServiceCallback callback);

  void NotifyServiceAdded(std::string app_name, std::string service_name);

 private:
  zx::channel OpenAsDirectory();
  void ScanPublicServices();
  void RegisterService(std::string service_name);

  std::unique_ptr<fuchsia::sys::StartupContext> startup_context_;

  // map of service name and app name
  std::map<std::string, std::string> services_;
  std::map<std::string, std::string> startup_services_;

  // map of app name and it's service provider
  std::map<std::string, std::unique_ptr<TrustyAppInstance>> launched_apps_;

  // Nested environment within which the apps started by sysmgr will run.
  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr env_launcher_;

  fbl::RefPtr<fs::PseudoDir> root_;
  fs::SynchronousVfs vfs_;

  struct ServiceWaiter
      : public fbl::DoublyLinkedListable<fbl::unique_ptr<ServiceWaiter>> {
    std::string service_name;
    ServiceRegistry::WaitOnServiceCallback callback;
  };

  fbl::Mutex mutex_;
  fbl::DoublyLinkedList<fbl::unique_ptr<ServiceWaiter>> waiters_
      FXL_GUARDED_BY(mutex_);

  FXL_DISALLOW_COPY_AND_ASSIGN(TrustyApp);
};

}  // namespace sysmgr
