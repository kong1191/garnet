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
constexpr char kDefaultServiceName[] = "trusty_service";

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

  auto child = fbl::AdoptRef(new fs::Service([this](zx::channel channel) {
    startup_context_->ConnectToEnvironmentService(fuchsia::sys::Loader::Name_,
                                                  std::move(channel));
    return ZX_OK;
  }));
  root_->AddEntry(fuchsia::sys::Loader::Name_, std::move(child));

  ScanPublicServices();

  // This service is used by tipc_agent only
  auto s = std::make_unique<TrustyServiceProvider>(root_, this);
  child = fbl::AdoptRef(
      new fs::Service([service = std::move(s)](zx::channel channel) {
        service->Bind(std::move(channel));
        return ZX_OK;
      }));
  root_->AddEntry(kDefaultServiceName, std::move(child));
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

      public_services_.emplace(service, app_name);
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

void TrustyApp::RequestService(fidl::StringPtr service_name) {
  auto it = public_services_.find(service_name);
  if (it != public_services_.end()) {
    std::string app_name(it->second);

    if (launched_apps_.find(app_name) == launched_apps_.end()) {
      auto service = std::make_unique<TrustyServiceProvider>(root_, this);
      FXL_CHECK(service);

      auto additional_services = fuchsia::sys::ServiceList::New();
      FXL_CHECK(additional_services);
      service->BindProvider(additional_services->provider.NewRequest());
      additional_services->names->push_back(kDefaultServiceName);

      fuchsia::sys::LaunchInfo launch_info;
      launch_info.url = app_name;
      launch_info.additional_services = std::move(additional_services);

      MountPackageData(launch_info);

      auto& controller = service->controller();
      env_launcher_->CreateComponent(std::move(launch_info),
                                     controller.NewRequest());
      controller.set_error_handler([this, app_name] {
        FXL_LOG(ERROR) << "Application " << app_name << " died";

        auto it = launched_apps_.find(app_name);
        FXL_CHECK(it != launched_apps_.end());

        auto& service = it->second;
        service->controller().Unbind();  // kills the singleton application
        service->RemoveAllServices();

        launched_apps_.erase(it);
      });

      launched_apps_.emplace(app_name, std::move(service));
    }
  }
}

void TrustyApp::WaitOnService(fidl::StringPtr service_name,
                              ServiceProvider::WaitOnServiceCallback callback) {
  auto waiter = fbl::make_unique<ServiceWaiter>();
  FXL_DCHECK(waiter);

  waiter->service_name = service_name.get();
  waiter->callback = std::move(callback);

  fbl::AutoLock lock(&mutex_);
  waiters_.push_back(std::move(waiter));
}

void TrustyApp::NotifyServiceAdded(std::string service_name) {
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

TrustyServiceProvider::TrustyServiceProvider(fbl::RefPtr<fs::PseudoDir> root,
                                             TrustyApp* app)
    : binding_(this), root_(root), app_(app) {
  service_provider_.AddServiceForName(
      [this](zx::channel channel) { binding_.Bind(std::move(channel)); },
      kDefaultServiceName);
}

void TrustyServiceProvider::LookupService(
    fidl::StringPtr service_name,
    ServiceProvider::LookupServiceCallback callback) {
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name.get());
  if (status != ZX_OK) {
    callback(false);
  } else {
    callback(true);
  }
}

void TrustyServiceProvider::AddService(fidl::StringPtr service_name,
                                       AddServiceCallback callback) {
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name.get());
  if (status == ZX_OK) {
    FXL_LOG(WARNING) << "Ignored existed service " << service_name.get();
    return;
  }

  registered_services_.push_back(service_name);
  root_->AddEntry(service_name->c_str(),
                  fbl::AdoptRef(new fs::Service(
                      [callback = std::move(callback)](zx::channel request) {
                        callback(std::move(request));
                        return ZX_OK;
                      })));

  app_->NotifyServiceAdded(service_name);
}

void TrustyServiceProvider::ConnectToService(fidl::StringPtr service_name,
                                             zx::channel channel) {
  fbl::RefPtr<fs::Vnode> child;
  zx_status_t status = root_->Lookup(&child, service_name.get());
  if (status == ZX_OK) {
    child->Serve(nullptr, std::move(channel), 0);
  }
}

void TrustyServiceProvider::WaitOnService(fidl::StringPtr service_name,
                                          WaitOnServiceCallback callback) {
  app_->WaitOnService(service_name, std::move(callback));
}

void TrustyServiceProvider::RequestService(fidl::StringPtr service_name) {
  app_->RequestService(service_name);
}

void TrustyServiceProvider::RemoveService(std::string service_name) {
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

void TrustyServiceProvider::RemoveAllServices() {
  for (auto& service : registered_services_) {
    root_->RemoveEntry(service);
  }
  registered_services_.clear();
}

}  // namespace sysmgr
