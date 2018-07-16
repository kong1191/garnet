// Due to nanosleep() and close() are already defined in zircon header,
// we should change and undefine the function names before implementing
// the trusty version of the two functions.
#define nanosleep nanosleep_orig
#define close close_orig

#include <inttypes.h>

#include <lib/async-loop/cpp/loop.h>

#include "garnet/public/lib/gzos/trusty_ipc/cpp/channel.h"
#include "garnet/public/lib/gzos/trusty_ipc/cpp/object_manager.h"
#include "garnet/public/lib/gzos/trusty_ipc/cpp/port.h"

#include "garnet/public/lib/gzos/trusty_app/trusty_ipc.h"
#include "garnet/public/lib/gzos/trusty_app/trusty_uuid.h"
#include "garnet/public/lib/gzos/trusty_app/uapi/err.h"

#include "lib/app/cpp/startup_context.h"
#include "lib/fxl/logging.h"

#undef nanosleep
#undef close

using namespace trusty_ipc;

extern "C" {

static fbl::Mutex async_loop_lock;
static bool async_loop_started = false;
async::Loop* loop_ptr;

static inline long zx_status_to_lk_err(zx_status_t status) {
  if (status == ZX_OK) {
    return NO_ERROR;
  }

  switch (status) {
    case ZX_ERR_INTERNAL:
      return ERR_GENERIC;
    case ZX_ERR_NOT_SUPPORTED:
      return ERR_NOT_SUPPORTED;
    case ZX_ERR_NO_RESOURCES:
      return ERR_NO_RESOURCES;
    case ZX_ERR_NO_MEMORY:
      return ERR_NO_MEMORY;
    case ZX_ERR_INVALID_ARGS:
      return ERR_INVALID_ARGS;
    case ZX_ERR_BAD_HANDLE:
      return ERR_BAD_HANDLE;
    case ZX_ERR_WRONG_TYPE:
      return ERR_BAD_HANDLE;
    case ZX_ERR_BAD_SYSCALL:
      return ERR_NOT_VALID;
    case ZX_ERR_OUT_OF_RANGE:
      return ERR_OUT_OF_RANGE;
    case ZX_ERR_BUFFER_TOO_SMALL:
      return ERR_NOT_ENOUGH_BUFFER;
    case ZX_ERR_BAD_STATE:
      return ERR_BAD_STATE;
    case ZX_ERR_TIMED_OUT:
      return ERR_TIMED_OUT;
    case ZX_ERR_SHOULD_WAIT:
      return ERR_NOT_READY;
    case ZX_ERR_CANCELED:
      return ERR_CANCELLED;
    case ZX_ERR_PEER_CLOSED:
      return ERR_CHANNEL_CLOSED;
    case ZX_ERR_NOT_FOUND:
      return ERR_NOT_FOUND;
    case ZX_ERR_ALREADY_EXISTS:
      return ERR_ALREADY_EXISTS;
    case ZX_ERR_ALREADY_BOUND:
      return ERR_ALREADY_EXISTS;
    case ZX_ERR_UNAVAILABLE:
      return ERR_NOT_VALID;
    case ZX_ERR_ACCESS_DENIED:
      return ERR_ACCESS_DENIED;
    case ZX_ERR_IO:
      return ERR_IO;
    default:
      FXL_DLOG(WARNING) << "Unsupported status: " << status;
      return ERR_GENERIC;
  }
}

static constexpr char kScanUuidFormatString[] =
    "%8" SCNx32
    "-"
    "%4" SCNx16
    "-"
    "%4" SCNx16
    "-"
    "%2" SCNx8 "%2" SCNx8
    "-"
    "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8 "%2" SCNx8;

static inline void string_to_uuid(std::string& str, uuid_t* uuid) {
  sscanf(str.c_str(), kScanUuidFormatString,
         &uuid->time_low, &uuid->time_mid,
         &uuid->time_hi_and_version,
         &uuid->clock_seq_and_node[0],
         &uuid->clock_seq_and_node[1],
         &uuid->clock_seq_and_node[2],
         &uuid->clock_seq_and_node[3],
         &uuid->clock_seq_and_node[4],
         &uuid->clock_seq_and_node[5],
         &uuid->clock_seq_and_node[6],
         &uuid->clock_seq_and_node[7]);
}

static zx_status_t get_object_by_id(TipcObject::ObjectType type,
                             uint32_t handle_id,
                             fbl::RefPtr<TipcObject>* obj_out) {
  auto obj_mgr = TipcObjectManager::Instance();
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = obj_mgr->GetObject(handle_id, &obj);
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to get object, handle_id: " << handle_id
                    << " status: " << status;
    return status;
  }

  if (type != TipcObject::ANY) {
    if (obj->type() != type) {
      FXL_DLOG(ERROR) << "Tipc object type mismatch, expect: " << type
                      << " actual: " << obj->type()
                      << " handle_id: " << handle_id;
      return ZX_ERR_BAD_HANDLE;
    }
  }

  *obj_out = fbl::move(obj);
  return ZX_OK;
}

long nanosleep(uint32_t clock_id, uint32_t flags, uint64_t sleep_time) {
  return ERR_NOT_IMPLEMENTED;
}

long port_create(const char *path, uint32_t num_recv_bufs,
                 uint32_t recv_buf_size, uint32_t flags) {
  static fbl::Mutex context_lock;
  static async::Loop loop(&kAsyncLoopConfigMakeDefault);
  static std::unique_ptr<fuchsia::sys::StartupContext> startup_context;
  {
    fbl::AutoLock lock(&context_lock);
    if (startup_context == nullptr) {
      startup_context =
          fbl::move(fuchsia::sys::StartupContext::CreateFromStartupInfo());

      fbl::AutoLock lock(&async_loop_lock);
      loop_ptr = &loop;
    }
  }

  auto port = fbl::MakeRefCounted<TipcPortImpl>(num_recv_bufs, recv_buf_size,
                                                flags);
  if (port == nullptr) {
    return ERR_NO_MEMORY;
  }

  auto obj_mgr = TipcObjectManager::Instance();
  zx_status_t status = obj_mgr->InstallObject(port);
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to install object: " << status;
    return zx_status_to_lk_err(status);
  }

  std::string service_name(path);
  status = startup_context->outgoing().AddPublicService<TipcPort>(
      [port](fidl::InterfaceRequest<TipcPort> request) {
        FXL_LOG(INFO) << "port bind, handle_id: " << port->handle_id();
        port->Bind(std::move(request));
      },
      service_name);

  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to publish port service: " << status;
    obj_mgr->RemoveObject(port->handle_id());
    return zx_status_to_lk_err(status);
  }

  return (long)port->handle_id();
}

long connect(const char *path, uint32_t flags) {
  return ERR_NOT_IMPLEMENTED;
}

long accept(uint32_t handle_id, uuid_t* peer_uuid) {
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::PORT, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto port = fbl::RefPtr<TipcPortImpl>::Downcast(fbl::move(obj));

  std::string uuid_str;
  fbl::RefPtr<TipcChannelImpl> new_channel;
  status = port->Accept(&uuid_str, &new_channel);
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to accept new connection,"
                    << " handle_id: " << handle_id
                    << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  if (peer_uuid) {
    if (uuid_str.empty()) {
      memset(peer_uuid, 0, sizeof(uuid_t));
    } else {
      string_to_uuid(uuid_str, peer_uuid);
    }
  }

  FXL_DLOG(INFO) << "accept: port handle_id: " << port->handle_id()
                 << " new_channel handle_id: " << new_channel->handle_id();

  return (long)new_channel->handle_id();
}

long close(uint32_t handle_id) {
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::ANY, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  obj->Shutdown();
  return NO_ERROR;
}

long set_cookie(uint32_t handle_id, void *cookie) {
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::ANY, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  obj->set_cookie(cookie);
  return NO_ERROR;
}

long handle_set_create(void) {
  return ERR_NOT_IMPLEMENTED;
}

long handle_set_ctrl(uint32_t handle, uint32_t cmd, struct uevent *evt) {
  return ERR_NOT_IMPLEMENTED;
}

// Message loop will start to serve request when TA start to wait event.
// TA should guarantee that all ports are published by port_create()
// before message loop start, or port connect request might be lost.
static void start_message_loop() {
  fbl::AutoLock lock(&async_loop_lock);
  if (!async_loop_started) {
    FXL_DCHECK(loop_ptr);
    loop_ptr->StartThread();
    async_loop_started = true;
  }
}

long wait(uint32_t handle_id, uevent_t *event, uint32_t timeout_ms) {
  start_message_loop();

  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::ANY, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  WaitResult result;
  if (timeout_ms == UINT32_MAX) {
    status = obj->Wait(&result, zx::time::infinite());
  } else {
    status = obj->Wait(&result, zx::deadline_after(zx::msec(timeout_ms)));
  }

  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to wait event, handle_id: " << handle_id
                    << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  event->handle = result.handle_id;
  event->event = result.event;
  event->cookie = result.cookie;
  return NO_ERROR;
}

long wait_any(uevent_t *event, uint32_t timeout_ms) {
  start_message_loop();

  WaitResult result;
  zx_status_t status;
  auto obj_mgr = TipcObjectManager::Instance();

  if (timeout_ms == UINT32_MAX) {
    status = obj_mgr->Wait(&result, zx::time::infinite());
  } else {
    status = obj_mgr->Wait(&result, zx::deadline_after(zx::msec(timeout_ms)));
  }

  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to wait any events: " << status;
    return zx_status_to_lk_err(status);
  }

  event->handle = result.handle_id;
  event->event = result.event;
  event->cookie = result.cookie;
  return NO_ERROR;
}

long get_msg(uint32_t handle_id, ipc_msg_info_t *msg_info) {
  FXL_DCHECK(msg_info);

  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::CHANNEL, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto channel = fbl::RefPtr<TipcChannelImpl>::Downcast(fbl::move(obj));
  uint32_t msg_id;
  size_t len;
  status = channel->GetMessage(&msg_id, &len);
  if (status == ZX_ERR_SHOULD_WAIT) {
    return ERR_NO_MSG;
  }

  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to get message, handle_id: " << handle_id
                    << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  msg_info->id = msg_id;
  msg_info->len = len;
  // TODO(james): support handle transfer
  msg_info->num_handles = 0;

  return NO_ERROR;
}

long read_msg(uint32_t handle_id, uint32_t msg_id, uint32_t offset,
              ipc_msg_t *msg) {
  FXL_DCHECK(msg);
  FXL_DCHECK(msg->iov);
  // TODO(james): support multiple iovs
  FXL_DCHECK(msg->num_iov == 1);


  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::CHANNEL, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto channel = fbl::RefPtr<TipcChannelImpl>::Downcast(fbl::move(obj));
  auto buf = msg->iov->base;
  auto buf_size = msg->iov->len;
  status = channel->ReadMessage(msg_id, offset, buf, &buf_size);
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to read message, handle_id: " << handle_id
                    << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  return NO_ERROR;
}

long put_msg(uint32_t handle_id, uint32_t msg_id) {
  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::CHANNEL, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto channel = fbl::RefPtr<TipcChannelImpl>::Downcast(fbl::move(obj));
  status = channel->PutMessage(msg_id);
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to put message, handle_id: " << handle_id
                    << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  return NO_ERROR;
}

long send_msg(uint32_t handle_id, ipc_msg_t *msg) {
  FXL_DCHECK(msg);
  FXL_DCHECK(msg->iov);
  // TODO(james): support multiple iovs
  FXL_DCHECK(msg->num_iov == 1);

  fbl::RefPtr<TipcObject> obj;
  zx_status_t status = get_object_by_id(TipcObject::CHANNEL, handle_id, &obj);
  if (status != ZX_OK) {
    return zx_status_to_lk_err(status);
  }

  auto channel = fbl::RefPtr<TipcChannelImpl>::Downcast(fbl::move(obj));
  auto buf = msg->iov->base;
  auto buf_size = msg->iov->len;
  status = channel->SendMessage(buf, buf_size);
  if (status != ZX_OK) {
    FXL_DLOG(ERROR) << "Failed to send message, handle_id: " << handle_id
                    << " status: " << status;
    return zx_status_to_lk_err(status);
  }

  return NO_ERROR;
}

}
