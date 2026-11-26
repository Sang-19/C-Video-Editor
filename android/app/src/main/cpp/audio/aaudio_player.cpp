#include "audio/aaudio_player.hpp"

#include <cstring>

namespace trisvax::android {

AAudioPreviewPlayer::~AAudioPreviewPlayer() {
  stop();
}

bool AAudioPreviewPlayer::start(int sampleRateHz) {
  stop();
  sampleRate_ = sampleRateHz;

  AAudioStreamBuilder* builder = nullptr;
  if (AAudio_createStreamBuilder(&builder) != AAUDIO_OK || !builder) {
    return false;
  }
  AAudioStreamBuilder_setDirection(builder, AAUDIO_DIRECTION_OUTPUT);
  AAudioStreamBuilder_setSharingMode(builder, AAUDIO_SHARING_MODE_SHARED);
  AAudioStreamBuilder_setSampleRate(builder, sampleRate_);
  AAudioStreamBuilder_setChannelCount(builder, 2);
  AAudioStreamBuilder_setFormat(builder, AAUDIO_FORMAT_PCM_FLOAT);
  AAudioStreamBuilder_setPerformanceMode(builder, AAUDIO_PERFORMANCE_MODE_LOW_LATENCY);
  AAudioStreamBuilder_setDataCallback(builder, onAudioReady, this);

  aaudio_result_t res = AAudioStreamBuilder_openStream(builder, &stream_);
  AAudioStreamBuilder_delete(builder);
  if (res != AAUDIO_OK || !stream_) {
    stream_ = nullptr;
    return false;
  }
  if (AAudioStream_requestStart(stream_) != AAUDIO_OK) {
    stop();
    return false;
  }
  return true;
}

void AAudioPreviewPlayer::stop() {
  playing_.store(false, std::memory_order_release);
  if (stream_) {
    AAudioStream_requestStop(stream_);
    AAudioStream_close(stream_);
    stream_ = nullptr;
  }
}

void AAudioPreviewPlayer::setPlayheadUs(TimeUs timelineUs) {
  playheadUs_.store(timelineUs, std::memory_order_release);
}

void AAudioPreviewPlayer::setPlaying(bool playing) {
  playing_.store(playing, std::memory_order_release);
}

void AAudioPreviewPlayer::setSources(
    std::vector<AudioTrackMixSource> sources,
    std::vector<std::vector<float>> ownedBuffers) {
  std::lock_guard<std::mutex> lock(sourceMutex_);
  ownedBuffers_ = std::move(ownedBuffers);
  sources_ = std::move(sources);
}

aaudio_data_callback_result_t AAudioPreviewPlayer::onAudioReady(
    AAudioStream*,
    void* userData,
    void* audioData,
    int32_t numFrames) {
  auto* self = static_cast<AAudioPreviewPlayer*>(userData);
  auto* out = static_cast<float*>(audioData);
  const size_t frames = static_cast<size_t>(std::max(0, numFrames));

  if (!self->playing_.load(std::memory_order_acquire)) {
    std::memset(out, 0, frames * 2 * sizeof(float));
    return AAUDIO_CALLBACK_RESULT_CONTINUE;
  }

  const TimeUs startUs = self->playheadUs_.load(std::memory_order_acquire);
  {
    std::lock_guard<std::mutex> lock(self->sourceMutex_);
    AudioMixer::mixStereoBuffer(out, frames, self->sampleRate_, startUs, self->sources_);
  }
  const TimeUs advanceUs = (static_cast<TimeUs>(frames) * 1'000'000) / self->sampleRate_;
  self->playheadUs_.fetch_add(advanceUs, std::memory_order_acq_rel);
  return AAUDIO_CALLBACK_RESULT_CONTINUE;
}

} // namespace trisvax::android
