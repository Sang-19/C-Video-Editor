#include "native/filters/filter_pipeline.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace trisvax {

namespace {

inline uint8_t clampByte(float v) noexcept {
  return static_cast<uint8_t>(std::clamp(std::lround(v * 255.0f), 0L, 255L));
}

} // namespace

FilterSettings FilterPipeline::clamp(const FilterSettings& input) noexcept {
  FilterSettings out = input;
  out.brightness = std::clamp(out.brightness, -1.0f, 1.0f);
  out.contrast = std::clamp(out.contrast, 0.5f, 2.0f);
  out.saturation = std::clamp(out.saturation, 0.0f, 2.0f);
  out.temperature = std::clamp(out.temperature, -1.0f, 1.0f);
  out.exposure = std::clamp(out.exposure, -1.0f, 1.0f);
  out.sharpness = std::clamp(out.sharpness, 0.0f, 1.0f);
  return out;
}

void FilterPipeline::applyRgba8888(
    uint8_t* pixels,
    int width,
    int height,
    int strideBytes,
    const FilterSettings& rawSettings) {
  if (pixels == nullptr || width <= 0 || height <= 0 || strideBytes < width * 4) {
    return;
  }
  const FilterSettings s = clamp(rawSettings);
  if (s.isIdentity()) {
    return;
  }

  const float exposureMul = std::pow(2.0f, s.exposure);
  const float tempR = 1.0f + s.temperature * 0.16f;
  const float tempB = 1.0f - s.temperature * 0.16f;

  // Color adjustment pass
  for (int y = 0; y < height; ++y) {
    uint8_t* row = pixels + y * strideBytes;
    for (int x = 0; x < width; ++x) {
      uint8_t* px = row + x * 4;
      float r = static_cast<float>(px[0]) / 255.0f;
      float g = static_cast<float>(px[1]) / 255.0f;
      float b = static_cast<float>(px[2]) / 255.0f;

      // Exposure + brightness
      r = r * exposureMul + s.brightness * 0.35f;
      g = g * exposureMul + s.brightness * 0.35f;
      b = b * exposureMul + s.brightness * 0.35f;

      // Temperature
      r *= tempR;
      b *= tempB;

      // Contrast around 0.5
      r = (r - 0.5f) * s.contrast + 0.5f;
      g = (g - 0.5f) * s.contrast + 0.5f;
      b = (b - 0.5f) * s.contrast + 0.5f;

      // Saturation using Rec.709 luminance
      const float luma = 0.2126f * r + 0.7152f * g + 0.0722f * b;
      r = luma + (r - luma) * s.saturation;
      g = luma + (g - luma) * s.saturation;
      b = luma + (b - luma) * s.saturation;

      px[0] = clampByte(r);
      px[1] = clampByte(g);
      px[2] = clampByte(b);
    }
  }

  // Optional 4-neighbor unsharp mask pass when sharpness > 0
  if (s.sharpness > 0.01f && width >= 3 && height >= 3) {
    std::vector<uint8_t> copy(static_cast<size_t>(strideBytes) * static_cast<size_t>(height));
    std::memcpy(copy.data(), pixels, copy.size());
    const float amount = s.sharpness * 1.25f;

    for (int y = 1; y < height - 1; ++y) {
      const uint8_t* prevRow = copy.data() + (y - 1) * strideBytes;
      const uint8_t* currRow = copy.data() + y * strideBytes;
      const uint8_t* nextRow = copy.data() + (y + 1) * strideBytes;
      uint8_t* dstRow = pixels + y * strideBytes;

      for (int x = 1; x < width - 1; ++x) {
        const int idx = x * 4;
        for (int c = 0; c < 3; ++c) {
          const float center = static_cast<float>(currRow[idx + c]);
          const float neighbors =
              (static_cast<float>(prevRow[idx + c]) +
               static_cast<float>(nextRow[idx + c]) +
               static_cast<float>(currRow[idx - 4 + c]) +
               static_cast<float>(currRow[idx + 4 + c])) * 0.25f;
          const float sharpened = center + amount * (center - neighbors);
          dstRow[idx + c] = static_cast<uint8_t>(std::clamp(std::lround(sharpened), 0L, 255L));
        }
      }
    }
  }
}

std::string FilterPipeline::buildGlslEs300FragmentShader() {
  return R"GLSL(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;

in vec2 vTexCoord;
uniform samplerExternalOES uTexture;
uniform sampler2D uPrevTexture;
uniform int uUsePrevTexture;
uniform float uTransitionAlpha;
uniform int uTransitionType; // 0=cut, 1=fade, 2=cross_dissolve

uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uTemperature;
uniform float uExposure;
uniform float uSharpness;
uniform vec2 uTexelSize;

out vec4 outColor;

vec3 applyGrade(vec3 rgb) {
  float expMul = pow(2.0, uExposure);
  vec3 c = rgb * expMul + vec3(uBrightness * 0.35);
  c.r *= (1.0 + uTemperature * 0.16);
  c.b *= (1.0 - uTemperature * 0.16);
  c = (c - 0.5) * uContrast + 0.5;
  float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
  c = mix(vec3(luma), c, uSaturation);
  return clamp(c, 0.0, 1.0);
}

void main() {
  vec4 baseSample = texture(uTexture, vTexCoord);
  vec3 rgb = baseSample.rgb;

  if (uSharpness > 0.01) {
    vec3 n = texture(uTexture, vTexCoord + vec2(0.0, uTexelSize.y)).rgb;
    vec3 s = texture(uTexture, vTexCoord - vec2(0.0, uTexelSize.y)).rgb;
    vec3 e = texture(uTexture, vTexCoord + vec2(uTexelSize.x, 0.0)).rgb;
    vec3 w = texture(uTexture, vTexCoord - vec2(uTexelSize.x, 0.0)).rgb;
    vec3 avg = (n + s + e + w) * 0.25;
    rgb = clamp(rgb + (rgb - avg) * (uSharpness * 1.25), 0.0, 1.0);
  }

  vec3 graded = applyGrade(rgb);

  if (uTransitionType == 1) {
    graded *= clamp(uTransitionAlpha, 0.0, 1.0);
  } else if (uTransitionType == 2 && uUsePrevTexture == 1) {
    vec3 prevRgb = texture(uPrevTexture, vTexCoord).rgb;
    graded = mix(prevRgb, graded, clamp(uTransitionAlpha, 0.0, 1.0));
  }

  outColor = vec4(graded, baseSample.a);
}
)GLSL";
}

} // namespace trisvax
