#include "native/rendering/compositor_math.hpp"

#include <algorithm>
#include <cmath>

namespace trisvax {

namespace {

int makeEven(int val) noexcept {
  return (val % 2 == 0) ? val : std::max(2, val - 1);
}

constexpr float kPi = 3.14159265358979323846f;

} // namespace

RenderDimensions CompositorMath::computeTargetDimensions(
    AspectRatioPreset preset,
    int customW,
    int customH) noexcept {
  switch (preset) {
    case AspectRatioPreset::Portrait9x16:
      return {1080, 1920};
    case AspectRatioPreset::Landscape16x9:
      return {1920, 1080};
    case AspectRatioPreset::Square1x1:
      return {1080, 1080};
    case AspectRatioPreset::Freeform: {
      const float safeW = static_cast<float>(std::max(1, customW));
      const float safeH = static_cast<float>(std::max(1, customH));
      const float ratio = safeW / safeH;
      if (ratio >= 1.0f) {
        int h = 1080;
        int w = std::clamp(makeEven(static_cast<int>(std::lround(h * ratio))), 480, 1920);
        return {w, h};
      } else {
        int w = 1080;
        int h = std::clamp(makeEven(static_cast<int>(std::lround(w / ratio))), 480, 1920);
        return {w, h};
      }
    }
  }
  return {1080, 1920};
}

NormalizedUvRect CompositorMath::computeCropUvRect(
    int sourceWidth,
    int sourceHeight,
    int targetWidth,
    int targetHeight,
    const CropSettings& crop) noexcept {
  const bool swapped = (crop.rotationDegrees % 180) != 0;
  const float effSrcW = static_cast<float>(std::max(1, swapped ? sourceHeight : sourceWidth));
  const float effSrcH = static_cast<float>(std::max(1, swapped ? sourceWidth : sourceHeight));
  const float dstRatio = static_cast<float>(std::max(1, targetWidth)) /
                         static_cast<float>(std::max(1, targetHeight));
  const float srcRatio = effSrcW / effSrcH;

  float spanU = 1.0f;
  float spanV = 1.0f;
  if (srcRatio > dstRatio) {
    spanU = dstRatio / srcRatio;
  } else {
    spanV = srcRatio / dstRatio;
  }

  const float zoom = std::clamp(crop.zoom, 1.0f, 3.0f);
  spanU /= zoom;
  spanV /= zoom;

  const float maxShiftU = std::max(0.0f, 0.5f - spanU * 0.5f);
  const float maxShiftV = std::max(0.0f, 0.5f - spanV * 0.5f);
  const float centerU = 0.5f + std::clamp(crop.offsetX, -1.0f, 1.0f) * maxShiftU;
  const float centerV = 0.5f + std::clamp(crop.offsetY, -1.0f, 1.0f) * maxShiftV;

  return NormalizedUvRect{
      std::clamp(centerU - spanU * 0.5f, 0.0f, 1.0f),
      std::clamp(centerV - spanV * 0.5f, 0.0f, 1.0f),
      std::clamp(centerU + spanU * 0.5f, 0.0f, 1.0f),
      std::clamp(centerV + spanV * 0.5f, 0.0f, 1.0f)};
}

std::array<float, 16> CompositorMath::computeTransformMatrix(
    int sourceWidth,
    int sourceHeight,
    int targetWidth,
    int targetHeight,
    const CropSettings& crop) noexcept {
  const bool swapped = (crop.rotationDegrees % 180) != 0;
  const float effSrcW = static_cast<float>(std::max(1, swapped ? sourceHeight : sourceWidth));
  const float effSrcH = static_cast<float>(std::max(1, swapped ? sourceWidth : sourceHeight));
  const float srcRatio = effSrcW / effSrcH;
  const float dstRatio = static_cast<float>(std::max(1, targetWidth)) /
                         static_cast<float>(std::max(1, targetHeight));

  float scaleX = 1.0f;
  float scaleY = 1.0f;
  if (srcRatio > dstRatio) {
    scaleX = srcRatio / dstRatio;
  } else {
    scaleY = dstRatio / srcRatio;
  }

  const float zoom = std::clamp(crop.zoom, 1.0f, 3.0f);
  scaleX *= zoom;
  scaleY *= zoom;

  const float rad = static_cast<float>(crop.rotationDegrees) * (kPi / 180.0f);
  const float c = std::cos(rad);
  const float s = std::sin(rad);

  const float maxPanX = std::max(0.0f, scaleX - 1.0f);
  const float maxPanY = std::max(0.0f, scaleY - 1.0f);
  const float tx = -std::clamp(crop.offsetX, -1.0f, 1.0f) * maxPanX;
  const float ty = std::clamp(crop.offsetY, -1.0f, 1.0f) * maxPanY;

  // Column-major 4x4 matrix: Translate * Scale * RotateZ
  return {
      c * scaleX, s * scaleY, 0.0f, 0.0f,
      -s * scaleX, c * scaleY, 0.0f, 0.0f,
      0.0f, 0.0f, 1.0f, 0.0f,
      tx, ty, 0.0f, 1.0f};
}

TransitionEval CompositorMath::evaluateTransition(
    const Clip& clip,
    TimeUs clipOffsetUs) noexcept {
  if (clip.transitionIn.type == TransitionType::Cut || clip.transitionIn.durationUs <= 0) {
    return {false, TransitionType::Cut, 1.0f};
  }
  if (clipOffsetUs < 0 || clipOffsetUs >= clip.transitionIn.durationUs) {
    return {false, clip.transitionIn.type, 1.0f};
  }
  const float alpha = std::clamp(
      static_cast<float>(clipOffsetUs) / static_cast<float>(clip.transitionIn.durationUs),
      0.0f,
      1.0f);
  return {true, clip.transitionIn.type, alpha};
}

TextVisualState CompositorMath::evaluateTextOverlay(
    const TextOverlay& overlay,
    TimeUs timelinePosUs) noexcept {
  if (timelinePosUs < overlay.startUs || timelinePosUs > overlay.endUs) {
    return {false, 0.0f, 0.0f};
  }
  const TimeUs dur = std::max<TimeUs>(1, overlay.endUs - overlay.startUs);
  const TimeUs fadeWindow = std::min<TimeUs>(300'000, dur / 4);
  const TimeUs into = timelinePosUs - overlay.startUs;
  const TimeUs remaining = overlay.endUs - timelinePosUs;

  float alpha = 1.0f;
  float offsetY = 0.0f;
  if (overlay.animation == TextAnimation::FadeInOut && fadeWindow > 0) {
    if (into < fadeWindow) {
      alpha = static_cast<float>(into) / static_cast<float>(fadeWindow);
    } else if (remaining < fadeWindow) {
      alpha = static_cast<float>(remaining) / static_cast<float>(fadeWindow);
    }
  } else if (overlay.animation == TextAnimation::SlideUp && fadeWindow > 0) {
    if (into < fadeWindow) {
      const float t = static_cast<float>(into) / static_cast<float>(fadeWindow);
      alpha = t;
      offsetY = (1.0f - t) * 0.04f;
    }
  }
  return {true, std::clamp(alpha, 0.0f, 1.0f), offsetY};
}

} // namespace trisvax
