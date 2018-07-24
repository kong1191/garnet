// Copyright 2018 Open Trust Group
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/auto_lock.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <lib/async/default.h>
#include <lib/fdio/util.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/files/unique_fd.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_view.h"

#include "garnet/bin/gzos/sysmgr/manifest_parser.h"
#include "garnet/bin/gzos/sysmgr/trusty_app.h"

constexpr char kDefaultLabel[] = "trusty";

namespace sysmgr {

TrustyApp::TrustyApp()
    : startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
      root_(fbl::AdoptRef(new fs::PseudoDir)),
      vfs_(async_get_default()) {
  FXL_DCHECK(startup_context_);

  // Set up environment for the programs we will run.
  startup_context_->environment()->CreateNestedEnvironment(
      OpenAsDirectory(), env_.NewRequest(), env_controller_.NewRequest(),
      kDefaultLabel);
  env_->GetLauncher(env_launcher_.NewRequest());

  // Create default loader
  auto child = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
    startup_context_->ConnectToEnvironmentService(fuchsia::sys::Loader::Name_,
                                                  std::move(channel));
    return ZX_OK;
  }));
  root_->AddEntry(fuchsia::sys::Loader::Name_, std::move(child));

  ScanPublicServices();
}

void TrustyApp::ScanPublicServices() {
  ForEachManifest([this](const std::string& app_name,
                         const std::string& manifest_path,
                         const rapidjson::Document& document) {
    ParsePublicServices(document, [this, &app_name,
                                   &manifest_path](const std::string& service) {
      fbl::RefPtr<fs::Vnode> dummy;
      if (root_->Lookup(&dummy, service) == ZX_OK) {
        FXL_LOG(WARNING) << "Ignore duplicated service '" << service
                         << "' which comes from " << manifest_path;
        return;
      }

      startup_services_.emplace(service, app_name);

      RegisterService(service);
    });
  });
}

zx::channel TrustyApp::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) != ZX_OK)
    return zx::channel();
  if (vfs_.ServeDirectory(root_, std::move(h1)) != ZX_OK)
    return zx::channel();
  return h2;
}

void TrustyApp::RegisterService(std::string service_name) {
  auto child = fbl::AdoptRef(new fs::Service([this, service_name](
                                                 zx::channel client_handle) {
    auto it = startup_services_.find(service_name);
    if (it != startup_services_.end()) {
      std::string app_name(it->second);

      auto it = launched_apps_.find(app_name);
      if (it == launched_apps_.end()) {
        auto app = std::make_unique<TrustyAppInstance>(root_, this, app_name);
        FXL_CHECK(app);

        auto additional_services = fuchsia::sys::ServiceList::New();
        FXL_CHECK(additional_services);
        app->BindServiceProvider(additional_services->provider.NewRequest());
        additional_services->names->push_back(sysmgr::ServiceRegistry::Name_);

        fuchsia::sys::LaunchInfo launch_info;
        launch_info.url = app_name;
        launch_info.directory_request = app->service().NewRequest();
        launch_info.additional_services = std::move(additional_services);

        MountPackageData(launch_info);

        auto& controller = app->controller();
        env_launcher_->CreateComponent(std::move(launch_info),
                                       controller.NewRequest());
        controller.set_error_handler([this, app_name] {
          FXL_LOG(ERROR) << "Application " << app_name << " died";

          auto it = launched_apps_.find(app_name);
          FXL_CHECK(it != launched_apps_.end());

          auto& app = it->second;
          app->controller().Unbind();  // kills the singleton application
          app->RemoveAllServices();

          launched_apps_.erase(it);
        });

        std::tie(it, std::ignore) =
            launched_apps_.emplace(app_name, std::move(app));
      }

      it->second->service().ConnectToService(std::move(client_handle),
                                             service_name);
    }
    return ZX_OK;
  }));
  root_->AddEntry(service_name, std::move(child));
}

void TrustyApp::WaitOnService(fidl::StringPtr service_name,
                              ServiceRegistry::WaitOnServiceCallback callback) {
  auto waiter = fbl::make_unique<ServiceWaiter>();
  FXL_DCHECK(waiter);

  waiter->service_name = service_name.get();
  waiter->callback = std::move(callback);

  fbl::AutoLock lock(&mutex_);
  waiters_.push_back(std::move(waiter));
}

void TrustyApp::LookupService(fidl::StringPtr service_name,
                              ServiceRegistry::LookupServiceCallback callback) {
  auto it = services_.find(service_name);
  if (it == services_.end()) {
    callback(false);
  } else {
    callback(true);
  }
}

void TrustyApp::NotifyServiceAdded(std::string app_name,
                                   std::string service_name) {
  services_.emplace(service_name, app_name);

  auto child = fbl::AdoptRef(new fs::Service(
      [this, app_name, service_name](zx::channel client_handle) {
        auto it = launched_apps_.find(app_name);
        FXL_CHECK(it != launched_apps_.end());

        it->second->service().ConnectToService(std::move(client_handle),
                                               service_name);
        return ZX_OK;
      }));
  root_->AddEntry(service_name, std::move(child));

  fbl::AutoLock lock(&mutex_);
  for (auto& waiter : waiters_) {
    if (waiter.service_name == service_name) {
      waiter.callback();
      waiters_.erase(waiter);

      if (waiters_.is_empty()) {
        break;
      }
    }
  }
}

TrustyAppInstance::TrustyAppInstance(fbl::RefPtr<fs::PseudoDir> root,
                                     TrustyApp* app, std::string app_name)
    : binding_(this), root_(root), app_(app), app_name_(app_name) {
  service_provider_.AddServiceForName(
      [this](zx::channel channel) { binding_.Bind(std::move(channel)); },
      sysmgr::ServiceRegistry::Name_);
}

void TrustyAppInstance::BindServiceProvider(
    fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request) {
  service_provider_.AddBinding(std::move(request));
}

void TrustyAppInstance::LookupService(
    fidl::StringPtr service_name,
    ServiceRegistry::LookupServiceCallback callback) {
  app_->LookupService(service_name, std::move(callback));
}

void TrustyAppInstance::AddService(fidl::StringPtr service_name) {
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name.get());
  if (status == ZX_OK) {
    FXL_LOG(WARNING) << "Ignored existed service " << service_name.get();
    return;
  }

  registered_services_.push_back(service_name);
  app_->NotifyServiceAdded(app_name_, service_name);
}

void TrustyAppInstance::WaitOnService(fidl::StringPtr service_name,
                                      WaitOnServiceCallback callback) {
  app_->WaitOnService(service_name, std::move(callback));
}

void TrustyAppInstance::RemoveService(std::string service_name) {
  auto it = registered_services_.begin();
  while (it != registered_services_.end()) {
    if (*it == service_name) {
      root_->RemoveEntry(*it);
      registered_services_.erase(it);
      return;
    }
    it++;
  }
}

void TrustyAppInstance::RemoveAllServices() {
  for (auto& service : registered_services_) {
    root_->RemoveEntry(service);
  }
  registered_services_.clear();
}

}  // namespace sysmgr
