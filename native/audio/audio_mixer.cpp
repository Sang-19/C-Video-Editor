#include "native/audio/audio_mixer.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace trisvax {

float AudioMixer::computeEnvelopeGain(
    TimeUs localOffsetUs,
    TimeUs activeSpanUs,
    TimeUs fadeInUs,
    TimeUs fadeOutUs) noexcept {
  if (localOffsetUs < 0 || activeSpanUs <= 0 || localOffsetUs >= activeSpanUs) {
    return 0.0f;
  }
  float gain = 1.0f;
  if (fadeInUs > 0 && localOffsetUs < fadeInUs) {
    gain *= static_cast<float>(localOffsetUs) / static_cast<float>(fadeInUs);
  }
  if (fadeOutUs > 0) {
    const TimeUs rem = activeSpanUs - localOffsetUs;
    if (rem < fadeOutUs) {
      gain *= static_cast<float>(rem) / static_cast<float>(fadeOutUs);
    }
  }
  return std::clamp(gain, 0.0f, 1.0f);
}

void AudioMixer::mixStereoBuffer(
    float* outInterleavedStereo,
    size_t outFrameCount,
    int sampleRateHz,
    TimeUs bufferStartTimelineUs,
    const std::vector<AudioTrackMixSource>& sources) {
  if (outInterleavedStereo == nullptr || outFrameCount == 0 || sampleRateHz <= 0) {
    return;
  }
  std::memset(outInterleavedStereo, 0, outFrameCount * 2 * sizeof(float));

  const double usPerFrame = 1'000'000.0 / static_cast<double>(sampleRateHz);

  for (const auto& src : sources) {
    if (src.muted || src.gain <= 0.0001f || src.interleavedStereoPcm == nullptr || src.frameCount == 0) {
      continue;
    }
    const TimeUs trimStart = std::max<TimeUs>(0, src.trimStartUs);
    const TimeUs totalSrcUs = static_cast<TimeUs>(
        (static_cast<double>(src.frameCount) * 1'000'000.0) / static_cast<double>(sampleRateHz));
    const TimeUs trimEnd = (src.trimEndUs > trimStart && src.trimEndUs <= totalSrcUs)
                               ? src.trimEndUs
                               : totalSrcUs;
    const TimeUs activeSpanUs = std::max<TimeUs>(1, trimEnd - trimStart);

    for (size_t i = 0; i < outFrameCount; ++i) {
      const TimeUs frameTimelineUs = bufferStartTimelineUs +
          static_cast<TimeUs>(std::llround(static_cast<double>(i) * usPerFrame));
      TimeUs relUs = frameTimelineUs - src.timelineStartUs;
      if (relUs < 0) {
        continue;
      }
      if (relUs >= activeSpanUs) {
        if (src.loop) {
          relUs %= activeSpanUs;
        } else {
          continue;
        }
      }

      const TimeUs sourceTimeUs = trimStart + relUs;
      const double exactFrame = (static_cast<double>(sourceTimeUs) * static_cast<double>(sampleRateHz)) / 1'000'000.0;
      const size_t f0 = static_cast<size_t>(std::max(0.0, std::floor(exactFrame)));
      if (f0 >= src.frameCount) {
        continue;
      }
      const size_t f1 = std::min(src.frameCount - 1, f0 + 1);
      const float frac = static_cast<float>(exactFrame - static_cast<double>(f0));

      const float l0 = src.interleavedStereoPcm[f0 * 2];
      const float r0 = src.interleavedStereoPcm[f0 * 2 + 1];
      const float l1 = src.interleavedStereoPcm[f1 * 2];
      const float r1 = src.interleavedStereoPcm[f1 * 2 + 1];

      const float sampleL = l0 + (l1 - l0) * frac;
      const float sampleR = r0 + (r1 - r0) * frac;

      const float env = computeEnvelopeGain(relUs, activeSpanUs, src.fadeInUs, src.fadeOutUs);
      const float g = src.gain * env;

      outInterleavedStereo[i * 2] += sampleL * g;
      outInterleavedStereo[i * 2 + 1] += sampleR * g;
    }
  }

  // Soft-clipping limiter to prevent digital distortion when summing tracks
  for (size_t i = 0; i < outFrameCount * 2; ++i) {
    const float v = outInterleavedStereo[i];
    if (v > 0.95f || v < -0.95f) {
      outInterleavedStereo[i] = std::tanh(v);
    }
  }
}

std::vector<float> AudioMixer::processSpeed(
    const float* inInterleavedStereo,
    size_t inFrameCount,
    int sampleRateHz,
    double speed,
    AudioSpeedMode mode) {
  if (inInterleavedStereo == nullptr || inFrameCount == 0 || sampleRateHz <= 0) {
    return {};
  }
  const double clampedSpeed = std::clamp(speed, 0.25, 4.0);
  if (std::fabs(clampedSpeed - 1.0) < 1e-4) {
    return std::vector<float>(inInterleavedStereo, inInterleavedStereo + inFrameCount * 2);
  }

  const size_t outFrames = std::max<size_t>(
      1,
      static_cast<size_t>(std::llround(static_cast<double>(inFrameCount) / clampedSpeed)));
  std::vector<float> out(outFrames * 2, 0.0f);

  if (mode == AudioSpeedMode::RateCoupled) {
    // Direct linear resampling: changes playback rate and pitch proportionally
    for (size_t i = 0; i < outFrames; ++i) {
      const double srcPos = static_cast<double>(i) * clampedSpeed;
      const size_t f0 = std::min(inFrameCount - 1, static_cast<size_t>(srcPos));
      const size_t f1 = std::min(inFrameCount - 1, f0 + 1);
      const float frac = static_cast<float>(srcPos - static_cast<double>(f0));
      out[i * 2] = inInterleavedStereo[f0 * 2] * (1.0f - frac) + inInterleavedStereo[f1 * 2] * frac;
      out[i * 2 + 1] = inInterleavedStereo[f0 * 2 + 1] * (1.0f - frac) + inInterleavedStereo[f1 * 2 + 1] * frac;
    }
    return out;
  }

  // Pitch-preserved overlap-add (SOLA) windowing (~20ms Hann-windowed grains)
  const size_t grainFrames = static_cast<size_t>(std::max(256, sampleRateHz / 50)); // 20ms
  const size_t hopOut = grainFrames / 2;
  std::vector<float> weightSum(outFrames, 0.0f);

  for (size_t outStart = 0; outStart < outFrames; outStart += hopOut) {
    const size_t inStart = std::min(
        inFrameCount > 1 ? inFrameCount - 1 : 0,
        static_cast<size_t>(std::llround(static_cast<double>(outStart) * clampedSpeed)));
    for (size_t k = 0; k < grainFrames && (outStart + k) < outFrames && (inStart + k) < inFrameCount; ++k) {
      const float hann = 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * static_cast<float>(k) /
                                                 static_cast<float>(grainFrames)));
      out[(outStart + k) * 2] += inInterleavedStereo[(inStart + k) * 2] * hann;
      out[(outStart + k) * 2 + 1] += inInterleavedStereo[(inStart + k) * 2 + 1] * hann;
      weightSum[outStart + k] += hann;
    }
  }

  for (size_t i = 0; i < outFrames; ++i) {
    if (weightSum[i] > 1e-4f) {
      out[i * 2] /= weightSum[i];
      out[i * 2 + 1] /= weightSum[i];
    }
  }
  return out;
}

} // namespace trisvax
