#pragma once

#include "native/audio/audio_mixer.hpp"

#include <aaudio/AAudio.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace trisvax::android {

class AAudioPreviewPlayer {
public:
  AAudioPreviewPlayer() = default;
  ~AAudioPreviewPlayer();

  bool start(int sampleRateHz = 44'100);
  void stop();
  void setPlayheadUs(TimeUs timelineUs);
  void setPlaying(bool playing);
  void setSources(std::vector<AudioTrackMixSource> sources, std::vector<std::vector<float>> ownedBuffers);

private:
  AAudioStream* stream_ = nullptr;
  int sampleRate_ = 44'100;
  std::atomic<bool> playing_{false};
  std::atomic<TimeUs> playheadUs_{0};
  std::mutex sourceMutex_;
  std::vector<AudioTrackMixSource> sources_;
  std::vector<std::vector<float>> ownedBuffers_;

  static aaudio_data_callback_result_t onAudioReady(
      AAudioStream* stream,
      void* userData,
      void* audioData,
      int32_t numFrames);
};

} // namespace trisvax::android
