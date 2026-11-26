#pragma once

#include "native/engine/types.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace trisvax {

struct DecodedVideoFrame {
  int width = 0;
  int height = 0;
  TimeUs ptsUs = 0;
  std::vector<uint8_t> rgbaPixels;
  bool valid = false;
};

struct DecodedAudioChunk {
  int sampleRateHz = 44'100;
  int channels = 2;
  TimeUs ptsUs = 0;
  std::vector<float> interleavedStereoPcm;
};

class IMediaDecoder {
public:
  virtual ~IMediaDecoder() = default;
  virtual Error open(const std::string& uri) = 0;
  virtual MediaSource probeInfo() const = 0;
  virtual Error seek(TimeUs ptsUs) = 0;
  virtual bool readNextVideoFrame(DecodedVideoFrame& outFrame) = 0;
  virtual bool readNextAudioChunk(DecodedAudioChunk& outAudio) = 0;
  virtual void close() = 0;
};

class IMediaEncoder {
public:
  virtual ~IMediaEncoder() = default;
  virtual Error open(const ExportSettings& settings) = 0;
  virtual Error encodeVideoFrameRgba(const uint8_t* rgba, int width, int height, TimeUs ptsUs) = 0;
  virtual Error encodeAudioPcm(const float* interleavedStereo, size_t frameCount, TimeUs ptsUs) = 0;
  virtual Error finish() = 0;
  virtual void abort() = 0;
};

class MediaBackendFactory {
public:
  virtual ~MediaBackendFactory() = default;
  virtual std::unique_ptr<IMediaDecoder> createDecoder() = 0;
  virtual std::unique_ptr<IMediaEncoder> createEncoder() = 0;
  virtual int64_t availableStorageBytes(const std::string& path) const = 0;
};

} // namespace trisvax
