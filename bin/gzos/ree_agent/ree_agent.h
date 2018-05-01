#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>
#include <lib/async/cpp/wait.h>

#include "garnet/bin/gzos/ree_agent/trusty_ipc.h"

namespace ree_agent {

class ReeAgent {
 public:
  ReeAgent(zx::channel client, zx::channel loader)
      : client_(fbl::move(client)), loader_(fbl::move(loader)) {
    zx_signals_t signals = ZX_CHANNEL_PEER_CLOSED | ZX_CHANNEL_READABLE;
    client_wait_.set_object(client_);
    client_wait_.set_trigger(signals);
    client_wait_.set_handler(
        fbl::BindMember(this, &TipcDevice::Stream::OnClientMessageReady));
  }

  zx_status_t Start(async_t *async) {  
    return client_wait_.Begin(async);
  }

  async_wait_result_t OnClientMessageReady(async_t* async,
                                           zx_status_t status,
                                           const zx_packet_signal_t* signal) {}

 private:
  TipcPortManager port_manager_;

  // To send command to Trusted App Loader
  zx::channel loader_;

  // To send/receive messages to/from REE
  zx::channel client_;

  async::Wait client_wait_;
};

}  // namespace reeagent
