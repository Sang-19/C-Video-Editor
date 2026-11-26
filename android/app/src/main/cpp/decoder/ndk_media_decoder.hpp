#pragma once

#include "native/media/media_pipeline.hpp"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaExtractor.h>

#include <string>

namespace trisvax::android {

class NdkMediaDecoder final : public IMediaDecoder {
public:
  NdkMediaDecoder() = default;
  ~NdkMediaDecoder() override;

  Error open(const std::string& uri) override;
  Error openFd(int fd, int64_t offset, int64_t length);
  MediaSource probeInfo() const override;
  Error seek(TimeUs ptsUs) override;
  bool readNextVideoFrame(DecodedVideoFrame& outFrame) override;
  bool readNextAudioChunk(DecodedAudioChunk& outAudio) override;
  void close() override;

private:
  AMediaExtractor* extractor_ = nullptr;
  AMediaCodec* videoCodec_ = nullptr;
  int videoTrackIdx_ = -1;
  MediaSource probed_;
  int ownedFd_ = -1;
  bool eos_ = false;
};

} // namespace trisvax::android
