#pragma once

#include "native/media/media_pipeline.hpp"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>

#include <atomic>

namespace trisvax::android {

class NdkMediaEncoder final : public IMediaEncoder {
public:
  NdkMediaEncoder() = default;
  ~NdkMediaEncoder() override;

  Error open(const ExportSettings& settings) override;
  Error encodeVideoFrameRgba(const uint8_t* rgba, int width, int height, TimeUs ptsUs) override;
  Error encodeAudioPcm(const float* interleavedStereo, size_t frameCount, TimeUs ptsUs) override;
  Error finish() override;
  void abort() override;

private:
  ExportSettings settings_;
  AMediaCodec* videoEncoder_ = nullptr;
  AMediaCodec* audioEncoder_ = nullptr;
  AMediaMuxer* muxer_ = nullptr;
  int outputFd_ = -1;
  ssize_t videoTrackIdx_ = -1;
  ssize_t audioTrackIdx_ = -1;
  bool muxerStarted_ = false;
  std::atomic<bool> aborted_{false};

  void drainEncoder(AMediaCodec* codec, ssize_t& trackIdx, bool endOfStream);
  void cleanup();
};

class NdkMediaBackendFactory final : public MediaBackendFactory {
public:
  std::unique_ptr<IMediaDecoder> createDecoder() override;
  std::unique_ptr<IMediaEncoder> createEncoder() override;
  int64_t availableStorageBytes(const std::string& path) const override;
};

} // namespace trisvax::android
