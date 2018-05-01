#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>

#include <fs/managed-vfs.h>
#include <fs/pseudo-dir.h>
#include <fs/service.h>
#include <fs/vfs.h>
#include <lib/async-loop/cpp/loop.h>

#include "garnet/bin/gzos/ree_agent/trusty_ipc.h"
#include "lib/fxl/logging.h"
#include "lib/svc/cpp/service_namespace.h"

using component::ServiceNamespace;

namespace ree_agent {

class ReeAgent {
 public:
  ReeAgent(async_t* async)
      : vfs_(async),
        svc_directory_(fbl::AdoptRef(new fs::PseudoDir())),
        namespace_(svc_directory_) {}

  zx::channel OpenAsDirectory();
  bool ServeDirectory(zx::channel channel);

  template <typename Interface>
  void AddService(ServiceNamespace::InterfaceRequestHandler<Interface> handler,
                  const std::string& service_name = Interface::Name_) {
    namespace_.AddService(handler, service_name);
  }

 private:
  fs::ManagedVfs vfs_;
  fbl::RefPtr<fs::PseudoDir> svc_directory_;
  component::ServiceNamespace namespace_;
};

}  // namespace ree_agent
