// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADMISSION_CONTROL_H_
#define GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADMISSION_CONTROL_H_

#include <lib/fit/function.h>
#include <zircon/compiler.h>

#include <mutex>

#include <fbl/macros.h>

// Controls how many Codec instances are concurrently served by this process.
//
// There's a limit of 1 for single-instance decoders, but arbitrarily-many
// multi-instance decoders can be used if there's no single-instance decoder.
class DeviceCtx;
class CodecAdmission;
class CodecAdmissionControl {
 public:
  // Should be created by DeviceCtx only.
  //
  // The CodecAdmissionControl is a member of DeviceCtx so inherently out-lasts
  // the parent device_ctx pointer.
  explicit CodecAdmissionControl(DeviceCtx* device_ctx);

  // Get a move-only CodecAdmission as a move-only ticket that allows creation
  // of a CodecImpl.
  //
  // TODO(dustingreen): The attempt to add a codec should not be started until
  // after any previously-initiated Codec channel closes are fully done being
  // processed.  This method signature allows for that fencing to be added later
  // without changing the call site, but the actual fencing isn't really there
  // yet - currently a single re-post is done to make the async-ness real, but
  // (at least) because close processing itself needs to post around to get
  // everything shut down cleanly, the overall fencing isn't really there yet.
  //
  // TODO(dustingreen): std::optional<> instead when C++17.
  void TryAddCodec(bool multi_instance,
                   fit::function<void(std::unique_ptr<CodecAdmission>)>
                       continue_after_previously_started_channel_closes_done);

  // Anything posted here will run after any previously-posted items here or via
  // TryAddCodec().
  //
  // Run the posted closure after all previously-started closes are done being
  // processed, and after all previously-queued closures via this method are
  // done.
  //
  // TODO(dustingreen): This doesn't actually do what it says yet, though
  // items queued via this method and TryAddCodec() do run in order.
  void PostAfterPreviouslyStartedClosesDone(fit::closure to_run);

 private:
  friend class CodecAdmission;

  // This is called after exactly one post via
  // PostAfterPreviouslyStartedClosesDone() performed by TryAddCodec().
  std::unique_ptr<CodecAdmission> TryAddCodecInternal(bool multi_instance);

  void RemoveCodec(bool multi_instance);

  DeviceCtx* device_ctx_ = nullptr;

  std::mutex lock_;
  uint32_t single_instance_codec_count_ __TA_GUARDED(lock_);
  uint32_t multi_instance_codec_count_ __TA_GUARDED(lock_);

  DISALLOW_COPY_ASSIGN_AND_MOVE(CodecAdmissionControl);
};

class CodecAdmission {
 public:
  // move-only
  //
  // These user-declared but not user-provided move construct / move assign
  // still count as user-delcared, so these (either of them) cause normal copy
  // and assign to be implicitly deleted (both of them).
  CodecAdmission(CodecAdmission&& from) = default;
  CodecAdmission& operator=(CodecAdmission&& from) = default;

  ~CodecAdmission();

 private:
  friend class CodecAdmissionControl;
  CodecAdmission(CodecAdmissionControl* codec_admission_control,
                 bool multi_instance);

  CodecAdmissionControl* codec_admission_control_ = nullptr;
  bool multi_instance_;
};

#endif  // GARNET_DRIVERS_VIDEO_AMLOGIC_DECODER_CODEC_ADMISSION_CONTROL_H_
