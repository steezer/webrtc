/*
 *  Copyright 2020 The WebRTC Project Authors. All rights reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include <string>
#include <utility>
#include <vector>

#include "call/adaptation/video_stream_adapter.h"
#include "rtc_base/synchronization/sequence_checker.h"
#include "video/adaptation/bitrate_constraint.h"
#include "video/adaptation/video_stream_encoder_resource_manager.h"

namespace webrtc {

namespace {
bool IsSimulcast(const VideoEncoderConfig& encoder_config) {
  const std::vector<VideoStream>& simulcast_layers =
      encoder_config.simulcast_layers;

  bool is_simulcast = simulcast_layers.size() > 1;
  bool is_lowest_layer_active = simulcast_layers[0].active;
  int num_active_layers =
      std::count_if(simulcast_layers.begin(), simulcast_layers.end(),
                    [](const VideoStream& layer) { return layer.active; });

  // We can't distinguish between simulcast and singlecast when only the
  // lowest spatial layer is active. Treat this case as simulcast.
  return is_simulcast && (num_active_layers > 1 || is_lowest_layer_active);
}
}  // namespace

BitrateConstraint::BitrateConstraint()
    : encoder_settings_(absl::nullopt),
      encoder_target_bitrate_bps_(absl::nullopt) {
  sequence_checker_.Detach();
}

void BitrateConstraint::OnEncoderSettingsUpdated(
    absl::optional<EncoderSettings> encoder_settings) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  encoder_settings_ = std::move(encoder_settings);
}

void BitrateConstraint::OnEncoderTargetBitrateUpdated(
    absl::optional<uint32_t> encoder_target_bitrate_bps) {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  encoder_target_bitrate_bps_ = std::move(encoder_target_bitrate_bps);
}

bool BitrateConstraint::IsAdaptationUpAllowed(
    const VideoStreamInputState& input_state,
    const VideoSourceRestrictions& restrictions_before,
    const VideoSourceRestrictions& restrictions_after) const {
  RTC_DCHECK_RUN_ON(&sequence_checker_);
  // Make sure bitrate limits are not violated.
  if (DidIncreaseResolution(restrictions_before, restrictions_after)) {
    if (!encoder_settings_.has_value()) {
      return true;
    }

    uint32_t bitrate_bps = encoder_target_bitrate_bps_.value_or(0);
    if (bitrate_bps == 0) {
      return true;
    }

    if (IsSimulcast(encoder_settings_->encoder_config())) {
      // Resolution bitrate limits usage is restricted to singlecast.
      return true;
    }

    absl::optional<uint32_t> current_frame_size_px =
        VideoStreamEncoderResourceManager::GetSingleActiveLayerPixels(
            encoder_settings_->video_codec());
    if (!current_frame_size_px.has_value()) {
      return true;
    }

    absl::optional<VideoEncoder::ResolutionBitrateLimits> bitrate_limits =
        encoder_settings_->encoder_info().GetEncoderBitrateLimitsForResolution(
            // Need some sort of expected resulting pixels to be used
            // instead of unrestricted.
            GetHigherResolutionThan(*current_frame_size_px));

    if (bitrate_limits.has_value()) {
      RTC_DCHECK_GE(bitrate_limits->frame_size_pixels, *current_frame_size_px);
      return bitrate_bps >=
             static_cast<uint32_t>(bitrate_limits->min_start_bitrate_bps);
    }
  }
  return true;
}

}  // namespace webrtc
