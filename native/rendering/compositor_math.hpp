#pragma once

#include "native/engine/types.hpp"

#include <array>

namespace trisvax {

struct RenderDimensions {
  int width = 1080;
  int height = 1920;
};

struct NormalizedUvRect {
  float left = 0.0f;
  float top = 0.0f;
  float right = 1.0f;
  float bottom = 1.0f;
};

struct TransitionEval {
  bool active = false;
  TransitionType type = TransitionType::Cut;
  float alpha = 1.0f; // 0.0 = start of transition, 1.0 = full current clip
};

struct TextVisualState {
  bool visible = false;
  float alpha = 1.0f;
  float offsetYNorm = 0.0f;
};

class CompositorMath {
public:
  static RenderDimensions computeTargetDimensions(
      AspectRatioPreset preset,
      int customW = 9,
      int customH = 16) noexcept;

  static NormalizedUvRect computeCropUvRect(
      int sourceWidth,
      int sourceHeight,
      int targetWidth,
      int targetHeight,
      const CropSettings& crop) noexcept;

  // 4x4 column-major MVP matrix combining rotation, aspect fit/crop, zoom, and pan
  static std::array<float, 16> computeTransformMatrix(
      int sourceWidth,
      int sourceHeight,
      int targetWidth,
      int targetHeight,
      const CropSettings& crop) noexcept;

  static TransitionEval evaluateTransition(
      const Clip& clip,
      TimeUs clipOffsetUs) noexcept;

  static TextVisualState evaluateTextOverlay(
      const TextOverlay& overlay,
      TimeUs timelinePosUs) noexcept;
};

} // namespace trisvax
