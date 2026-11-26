#include "encoder/ndk_media_encoder.hpp"
#include "decoder/ndk_media_decoder.hpp"

#include <android/log.h>
#include <fcntl.h>
#include <media/NdkMediaFormat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace trisvax::android {

namespace {

// Converts RGBA8888 to NV12/YUV420SemiPlanar for H.264 MediaCodec COLOR_FormatYUV420Flexible
void rgbaToYuv420Planar(const uint8_t* rgba, int width, int height, uint8_t* dstYuv, size_t dstCap) {
  const size_t frameSize = static_cast<size_t>(width) * static_cast<size_t>(height);
  if (dstCap < frameSize * 3 / 2) return;
  uint8_t* yPlane = dstYuv;
  uint8_t* uPlane = dstYuv + frameSize;
  uint8_t* vPlane = uPlane + frameSize / 4;

  for (int j = 0; j < height; ++j) {
    for (int i = 0; i < width; ++i) {
      const uint8_t* px = rgba + (j * width + i) * 4;
      const int r = px[0], g = px[1], b = px[2];
      const int y = ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16;
      yPlane[j * width + i] = static_cast<uint8_t>(std::clamp(y, 0, 255));
      if ((j & 1) == 0 && (i & 1) == 0) {
        const int u = ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128;
        const int v = ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128;
        const int uvIdx = (j / 2) * (width / 2) + (i / 2);
        uPlane[uvIdx] = static_cast<uint8_t>(std::clamp(u, 0, 255));
        vPlane[uvIdx] = static_cast<uint8_t>(std::clamp(v, 0, 255));
      }
    }
  }
}

} // namespace

NdkMediaEncoder::~NdkMediaEncoder() {
  cleanup();
}

Error NdkMediaEncoder::open(const ExportSettings& settings) {
  cleanup();
  settings_ = settings;
  aborted_.store(false, std::memory_order_release);

  outputFd_ = ::open(settings.outputPath.c_str(), O_CREAT | O_RDWR | O_TRUNC, 0644);
  if (outputFd_ < 0) {
    return Error::make(ErrorCode::IoError, "Unable to open MP4 output file descriptor.");
  }

  muxer_ = AMediaMuxer_new(outputFd_, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
  if (!muxer_) {
    cleanup();
    return Error::make(ErrorCode::CodecError, "AMediaMuxer_new failed.");
  }

  videoEncoder_ = AMediaCodec_createEncoderByType("video/avc");
  if (!videoEncoder_) {
    cleanup();
    return Error::make(ErrorCode::CodecError, "H.264 encoder unavailable on device.");
  }

  AMediaFormat* vFmt = AMediaFormat_new();
  AMediaFormat_setString(vFmt, AMEDIAFORMAT_KEY_MIME, "video/avc");
  AMediaFormat_setInt32(vFmt, AMEDIAFORMAT_KEY_WIDTH, settings.width);
  AMediaFormat_setInt32(vFmt, AMEDIAFORMAT_KEY_HEIGHT, settings.height);
  AMediaFormat_setInt32(vFmt, AMEDIAFORMAT_KEY_BIT_RATE, settings.videoBitrateBps);
  AMediaFormat_setInt32(vFmt, AMEDIAFORMAT_KEY_FRAME_RATE, settings.fps);
  AMediaFormat_setInt32(vFmt, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, 1);
  AMediaFormat_setInt32(vFmt, AMEDIAFORMAT_KEY_COLOR_FORMAT, 0x7F420888); // COLOR_FormatYUV420Flexible

  media_status_t vStat = AMediaCodec_configure(videoEncoder_, vFmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
  AMediaFormat_delete(vFmt);
  if (vStat != AMEDIA_OK) {
    cleanup();
    return Error::make(ErrorCode::CodecError, "Failed to configure H.264 MediaCodec encoder.");
  }
  AMediaCodec_start(videoEncoder_);

  if (settings.includeAudio) {
    audioEncoder_ = AMediaCodec_createEncoderByType("audio/mp4a-latm");
    if (audioEncoder_) {
      AMediaFormat* aFmt = AMediaFormat_new();
      AMediaFormat_setString(aFmt, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
      AMediaFormat_setInt32(aFmt, AMEDIAFORMAT_KEY_SAMPLE_RATE, settings.audioSampleRate);
      AMediaFormat_setInt32(aFmt, AMEDIAFORMAT_KEY_CHANNEL_COUNT, 2);
      AMediaFormat_setInt32(aFmt, AMEDIAFORMAT_KEY_BIT_RATE, settings.audioBitrateBps);
      AMediaFormat_setInt32(aFmt, AMEDIAFORMAT_KEY_AAC_PROFILE, 2); // AAC-LC
      if (AMediaCodec_configure(audioEncoder_, aFmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE) == AMEDIA_OK) {
        AMediaCodec_start(audioEncoder_);
      } else {
        AMediaCodec_delete(audioEncoder_);
        audioEncoder_ = nullptr;
      }
      AMediaFormat_delete(aFmt);
    }
  }

  return Error::success();
}

void NdkMediaEncoder::drainEncoder(AMediaCodec* codec, ssize_t& trackIdx, bool endOfStream) {
  if (!codec || !muxer_) return;
  if (endOfStream) {
    ssize_t inIdx = AMediaCodec_dequeueInputBuffer(codec, 10'000);
    if (inIdx >= 0) {
      AMediaCodec_queueInputBuffer(codec, static_cast<size_t>(inIdx), 0, 0, 0,
                                   AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
    }
  }

  for (;;) {
    AMediaCodecBufferInfo info{};
    ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(codec, &info, endOfStream ? 10'000 : 0);
    if (outIdx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
      if (!muxerStarted_) {
        AMediaFormat* outFmt = AMediaCodec_getOutputFormat(codec);
        trackIdx = AMediaMuxer_addTrack(muxer_, outFmt);
        AMediaFormat_delete(outFmt);
        if (videoTrackIdx_ >= 0 && (!audioEncoder_ || audioTrackIdx_ >= 0)) {
          AMediaMuxer_start(muxer_);
          muxerStarted_ = true;
        }
      }
      continue;
    }
    if (outIdx < 0) break;

    if ((info.flags & AMEDIACODEC_BUFFER_FLAG_CODEC_CONFIG) == 0 && info.size > 0 && muxerStarted_ && trackIdx >= 0) {
      size_t outSize = 0;
      uint8_t* data = AMediaCodec_getOutputBuffer(codec, static_cast<size_t>(outIdx), &outSize);
      if (data) {
        AMediaMuxer_writeSampleData(muxer_, static_cast<size_t>(trackIdx), data, &info);
      }
    }
    AMediaCodec_releaseOutputBuffer(codec, static_cast<size_t>(outIdx), false);
    if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) break;
  }
}

Error NdkMediaEncoder::encodeVideoFrameRgba(const uint8_t* rgba, int width, int height, TimeUs ptsUs) {
  if (aborted_.load(std::memory_order_acquire)) {
    return Error::make(ErrorCode::Cancelled, "Encoder aborted");
  }
  if (!videoEncoder_ || !rgba) {
    return Error::make(ErrorCode::CodecError, "Video encoder not ready");
  }
  ssize_t inIdx = AMediaCodec_dequeueInputBuffer(videoEncoder_, 20'000);
  if (inIdx >= 0) {
    size_t bufCap = 0;
    uint8_t* dst = AMediaCodec_getInputBuffer(videoEncoder_, static_cast<size_t>(inIdx), &bufCap);
    const size_t yuvBytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 3 / 2;
    if (dst && bufCap >= yuvBytes) {
      rgbaToYuv420Planar(rgba, width, height, dst, bufCap);
      AMediaCodec_queueInputBuffer(videoEncoder_, static_cast<size_t>(inIdx), 0, yuvBytes,
                                   static_cast<uint64_t>(ptsUs), 0);
    }
  }
  drainEncoder(videoEncoder_, videoTrackIdx_, false);
  return Error::success();
}

Error NdkMediaEncoder::encodeAudioPcm(const float* interleavedStereo, size_t frameCount, TimeUs ptsUs) {
  if (!audioEncoder_ || !interleavedStereo || frameCount == 0) {
    return Error::success();
  }
  ssize_t inIdx = AMediaCodec_dequeueInputBuffer(audioEncoder_, 10'000);
  if (inIdx >= 0) {
    size_t bufCap = 0;
    uint8_t* dst = AMediaCodec_getInputBuffer(audioEncoder_, static_cast<size_t>(inIdx), &bufCap);
    const size_t pcmBytes = std::min(bufCap, frameCount * 2 * sizeof(int16_t));
    auto* dst16 = reinterpret_cast<int16_t*>(dst);
    const size_t sampleCount = pcmBytes / sizeof(int16_t);
    for (size_t i = 0; i < sampleCount; ++i) {
      const float s = std::clamp(interleavedStereo[i], -1.0f, 1.0f);
      dst16[i] = static_cast<int16_t>(s * 32767.0f);
    }
    AMediaCodec_queueInputBuffer(audioEncoder_, static_cast<size_t>(inIdx), 0, pcmBytes,
                                 static_cast<uint64_t>(ptsUs), 0);
  }
  drainEncoder(audioEncoder_, audioTrackIdx_, false);
  return Error::success();
}

Error NdkMediaEncoder::finish() {
  if (videoEncoder_) drainEncoder(videoEncoder_, videoTrackIdx_, true);
  if (audioEncoder_) drainEncoder(audioEncoder_, audioTrackIdx_, true);
  cleanup();
  return Error::success();
}

void NdkMediaEncoder::abort() {
  aborted_.store(true, std::memory_order_release);
  cleanup();
}

void NdkMediaEncoder::cleanup() {
  if (videoEncoder_) {
    AMediaCodec_stop(videoEncoder_);
    AMediaCodec_delete(videoEncoder_);
    videoEncoder_ = nullptr;
  }
  if (audioEncoder_) {
    AMediaCodec_stop(audioEncoder_);
    AMediaCodec_delete(audioEncoder_);
    audioEncoder_ = nullptr;
  }
  if (muxer_) {
    if (muxerStarted_) {
      AMediaMuxer_stop(muxer_);
      muxerStarted_ = false;
    }
    AMediaMuxer_delete(muxer_);
    muxer_ = nullptr;
  }
  if (outputFd_ >= 0) {
    ::close(outputFd_);
    outputFd_ = -1;
  }
  videoTrackIdx_ = -1;
  audioTrackIdx_ = -1;
}

std::unique_ptr<IMediaDecoder> NdkMediaBackendFactory::createDecoder() {
  return std::make_unique<NdkMediaDecoder>();
}

std::unique_ptr<IMediaEncoder> NdkMediaBackendFactory::createEncoder() {
  return std::make_unique<NdkMediaEncoder>();
}

int64_t NdkMediaBackendFactory::availableStorageBytes(const std::string& path) const {
  struct statvfs st{};
  std::filesystem::path p(path);
  std::string dir = p.has_parent_path() ? p.parent_path().string() : ".";
  if (::statvfs(dir.c_str(), &st) == 0) {
    return static_cast<int64_t>(st.f_bavail) * static_cast<int64_t>(st.f_frsize);
  }
  return -1;
}

} // namespace trisvax::android
