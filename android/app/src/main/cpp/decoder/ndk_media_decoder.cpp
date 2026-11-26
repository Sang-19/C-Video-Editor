#include "decoder/ndk_media_decoder.hpp"

#include <android/log.h>
#include <media/NdkMediaFormat.h>
#include <unistd.h>

#include <cstring>

#define LOG_TAG "TrisvaxDecoder"

namespace trisvax::android {

NdkMediaDecoder::~NdkMediaDecoder() {
  close();
}

Error NdkMediaDecoder::open(const std::string& uri) {
  close();
  std::string path = uri;
  if (path.rfind("file://", 0) == 0) {
    path = path.substr(7);
  }
  extractor_ = AMediaExtractor_new();
  if (!extractor_) {
    return Error::make(ErrorCode::CodecError, "Failed to allocate AMediaExtractor");
  }
  media_status_t status = AMediaExtractor_setDataSource(extractor_, path.c_str());
  if (status != AMEDIA_OK) {
    close();
    return Error::make(ErrorCode::IoError, "AMediaExtractor_setDataSource failed for " + path);
  }
  probed_.uri = uri;
  const size_t trackCount = AMediaExtractor_getTrackCount(extractor_);
  for (size_t i = 0; i < trackCount; ++i) {
    AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor_, i);
    const char* mime = nullptr;
    if (fmt && AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && mime) {
      if (std::strncmp(mime, "video/", 6) == 0 && videoTrackIdx_ < 0) {
        videoTrackIdx_ = static_cast<int>(i);
        int64_t durUs = 0;
        int32_t w = 1080, h = 1920;
        AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &durUs);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
        probed_.durationUs = durUs > 0 ? durUs : kDefaultImageDurationUs;
        probed_.width = w;
        probed_.height = h;
        AMediaExtractor_selectTrack(extractor_, i);
        videoCodec_ = AMediaCodec_createDecoderByType(mime);
        if (videoCodec_) {
          AMediaCodec_configure(videoCodec_, fmt, nullptr, nullptr, 0);
          AMediaCodec_start(videoCodec_);
        }
      }
    }
    if (fmt) AMediaFormat_delete(fmt);
  }
  if (videoTrackIdx_ < 0 || !videoCodec_) {
    close();
    return Error::make(ErrorCode::CodecError, "No supported video track found in media");
  }
  return Error::success();
}

Error NdkMediaDecoder::openFd(int fd, int64_t offset, int64_t length) {
  close();
  ownedFd_ = dup(fd);
  if (ownedFd_ < 0) {
    return Error::make(ErrorCode::IoError, "Failed to duplicate media file descriptor");
  }
  extractor_ = AMediaExtractor_new();
  if (!extractor_) {
    close();
    return Error::make(ErrorCode::CodecError, "Failed to allocate AMediaExtractor");
  }
  media_status_t status = AMediaExtractor_setDataSourceFd(extractor_, ownedFd_, offset, length);
  if (status != AMEDIA_OK) {
    close();
    return Error::make(ErrorCode::IoError, "AMediaExtractor_setDataSourceFd failed");
  }
  const size_t trackCount = AMediaExtractor_getTrackCount(extractor_);
  for (size_t i = 0; i < trackCount; ++i) {
    AMediaFormat* fmt = AMediaExtractor_getTrackFormat(extractor_, i);
    const char* mime = nullptr;
    if (fmt && AMediaFormat_getString(fmt, AMEDIAFORMAT_KEY_MIME, &mime) && mime) {
      if (std::strncmp(mime, "video/", 6) == 0 && videoTrackIdx_ < 0) {
        videoTrackIdx_ = static_cast<int>(i);
        int64_t durUs = 0;
        int32_t w = 1080, h = 1920;
        AMediaFormat_getInt64(fmt, AMEDIAFORMAT_KEY_DURATION, &durUs);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, &w);
        AMediaFormat_getInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, &h);
        probed_.durationUs = durUs > 0 ? durUs : kDefaultImageDurationUs;
        probed_.width = w;
        probed_.height = h;
        AMediaExtractor_selectTrack(extractor_, i);
        videoCodec_ = AMediaCodec_createDecoderByType(mime);
        if (videoCodec_) {
          AMediaCodec_configure(videoCodec_, fmt, nullptr, nullptr, 0);
          AMediaCodec_start(videoCodec_);
        }
      }
    }
    if (fmt) AMediaFormat_delete(fmt);
  }
  return videoCodec_ ? Error::success() : Error::make(ErrorCode::CodecError, "No video track");
}

MediaSource NdkMediaDecoder::probeInfo() const {
  return probed_;
}

Error NdkMediaDecoder::seek(TimeUs ptsUs) {
  if (!extractor_ || !videoCodec_) {
    return Error::make(ErrorCode::InvalidArgument, "Decoder not open");
  }
  eos_ = false;
  AMediaCodec_flush(videoCodec_);
  AMediaExtractor_seekTo(extractor_, ptsUs, AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC);
  return Error::success();
}

bool NdkMediaDecoder::readNextVideoFrame(DecodedVideoFrame& outFrame) {
  if (!extractor_ || !videoCodec_) return false;
  for (int attempt = 0; attempt < 24; ++attempt) {
    if (!eos_) {
      ssize_t inIdx = AMediaCodec_dequeueInputBuffer(videoCodec_, 5000);
      if (inIdx >= 0) {
        size_t bufSize = 0;
        uint8_t* inBuf = AMediaCodec_getInputBuffer(videoCodec_, static_cast<size_t>(inIdx), &bufSize);
        ssize_t sampleSize = AMediaExtractor_readSampleData(extractor_, inBuf, bufSize);
        if (sampleSize < 0) {
          AMediaCodec_queueInputBuffer(videoCodec_, static_cast<size_t>(inIdx), 0, 0, 0,
                                       AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM);
          eos_ = true;
        } else {
          int64_t pts = AMediaExtractor_getSampleTime(extractor_);
          AMediaCodec_queueInputBuffer(videoCodec_, static_cast<size_t>(inIdx), 0,
                                       static_cast<size_t>(sampleSize), static_cast<uint64_t>(pts), 0);
          AMediaExtractor_advance(extractor_);
        }
      }
    }

    AMediaCodecBufferInfo info{};
    ssize_t outIdx = AMediaCodec_dequeueOutputBuffer(videoCodec_, &info, 5000);
    if (outIdx >= 0) {
      size_t outSize = 0;
      uint8_t* outBuf = AMediaCodec_getOutputBuffer(videoCodec_, static_cast<size_t>(outIdx), &outSize);
      outFrame.width = probed_.width;
      outFrame.height = probed_.height;
      outFrame.ptsUs = info.presentationTimeUs;
      outFrame.valid = (outBuf != nullptr && info.size > 0);
      if (outFrame.valid) {
        const size_t expectedRgba = static_cast<size_t>(outFrame.width) * static_cast<size_t>(outFrame.height) * 4;
        outFrame.rgbaPixels.assign(expectedRgba, 0);
        const size_t copyLen = std::min(expectedRgba, static_cast<size_t>(info.size));
        std::memcpy(outFrame.rgbaPixels.data(), outBuf + info.offset, copyLen);
      }
      AMediaCodec_releaseOutputBuffer(videoCodec_, static_cast<size_t>(outIdx), false);
      return outFrame.valid;
    }
  }
  return false;
}

bool NdkMediaDecoder::readNextAudioChunk(DecodedAudioChunk&) {
  return false;
}

void NdkMediaDecoder::close() {
  if (videoCodec_) {
    AMediaCodec_stop(videoCodec_);
    AMediaCodec_delete(videoCodec_);
    videoCodec_ = nullptr;
  }
  if (extractor_) {
    AMediaExtractor_delete(extractor_);
    extractor_ = nullptr;
  }
  if (ownedFd_ >= 0) {
    ::close(ownedFd_);
    ownedFd_ = -1;
  }
  videoTrackIdx_ = -1;
  eos_ = false;
}

} // namespace trisvax::android
