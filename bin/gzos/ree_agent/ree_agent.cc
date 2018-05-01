#include "garnet/bin/gzos/ree_agent/ree_agent.h"

namespace ree_agent {

bool ReeAgent::ServeDirectory(zx::channel channel) {
  return vfs_.ServeDirectory(svc_directory_, std::move(channel)) == ZX_OK;
}

zx::channel ReeAgent::OpenAsDirectory() {
  zx::channel h1, h2;
  if (zx::channel::create(0, &h1, &h2) < 0)
    return zx::channel();
  if (!ServeDirectory(std::move(h1)))
    return zx::channel();
  return h2;
}

}  // namespace ree_agent
