#pragma once

#include "native/engine/types.hpp"

#include <cstdint>
#include <vector>

namespace trisvax {

struct AudioTrackMixSource {
  const float* interleavedStereoPcm = nullptr;
  size_t frameCount = 0;
  float gain = 1.0f;
  bool muted = false;
  TimeUs timelineStartUs = 0;
  TimeUs trimStartUs = 0;
  TimeUs trimEndUs = 0;
  TimeUs fadeInUs = 0;
  TimeUs fadeOutUs = 0;
  bool loop = false;
};

class AudioMixer {
public:
  static float computeEnvelopeGain(
      TimeUs localOffsetUs,
      TimeUs activeSpanUs,
      TimeUs fadeInUs,
      TimeUs fadeOutUs) noexcept;

  static void mixStereoBuffer(
      float* outInterleavedStereo,
      size_t outFrameCount,
      int sampleRateHz,
      TimeUs bufferStartTimelineUs,
      const std::vector<AudioTrackMixSource>& sources);

  // Resamples interleaved stereo PCM according to speed & AudioSpeedMode
  static std::vector<float> processSpeed(
      const float* inInterleavedStereo,
      size_t inFrameCount,
      int sampleRateHz,
      double speed,
      AudioSpeedMode mode);
};

} // namespace trisvax
