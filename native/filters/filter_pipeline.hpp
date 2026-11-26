#pragma once

#include "native/engine/types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace trisvax {

class FilterPipeline {
public:
  static FilterSettings clamp(const FilterSettings& input) noexcept;

  // Deterministic CPU RGBA8888 filter pass matching GPU GLSL math
  static void applyRgba8888(
      uint8_t* pixels,
      int width,
      int height,
      int strideBytes,
      const FilterSettings& settings);

  // Generates GLSL ES 3.00 fragment shader shared by Android preview & offscreen export
  static std::string buildGlslEs300FragmentShader();
};

} // namespace trisvax
