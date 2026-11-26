#include "native/engine/types.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace trisvax {

bool FilterSettings::isIdentity() const noexcept {
  return std::fabs(brightness) < 1e-4f &&
         std::fabs(contrast - 1.0f) < 1e-4f &&
         std::fabs(saturation - 1.0f) < 1e-4f &&
         std::fabs(temperature) < 1e-4f &&
         std::fabs(exposure) < 1e-4f &&
         std::fabs(sharpness) < 1e-4f;
}

std::string generateUuidLike(const std::string& prefix) {
  static std::atomic<uint64_t> counter{1};
  const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  const uint64_t seq = counter.fetch_add(1, std::memory_order_relaxed);
  std::ostringstream oss;
  oss << prefix << "_" << std::hex << (now & 0xFFFFFFFFULL) << "_" << seq;
  return oss.str();
}

std::string aspectPresetToString(AspectRatioPreset preset) {
  switch (preset) {
    case AspectRatioPreset::Portrait9x16: return "PORTRAIT_9_16";
    case AspectRatioPreset::Landscape16x9: return "LANDSCAPE_16_9";
    case AspectRatioPreset::Square1x1: return "SQUARE_1_1";
    case AspectRatioPreset::Freeform: return "FREEFORM";
  }
  return "PORTRAIT_9_16";
}

AspectRatioPreset aspectPresetFromString(const std::string& s) {
  if (s == "LANDSCAPE_16_9") return AspectRatioPreset::Landscape16x9;
  if (s == "SQUARE_1_1") return AspectRatioPreset::Square1x1;
  if (s == "FREEFORM") return AspectRatioPreset::Freeform;
  return AspectRatioPreset::Portrait9x16;
}

std::string categoryToString(ProjectCategory category) {
  switch (category) {
    case ProjectCategory::All: return "ALL";
    case ProjectCategory::Videos: return "VIDEOS";
    case ProjectCategory::Reels: return "REELS";
    case ProjectCategory::Drafts: return "DRAFTS";
  }
  return "DRAFTS";
}

ProjectCategory categoryFromString(const std::string& s) {
  if (s == "ALL") return ProjectCategory::All;
  if (s == "VIDEOS") return ProjectCategory::Videos;
  if (s == "REELS") return ProjectCategory::Reels;
  return ProjectCategory::Drafts;
}

} // namespace trisvax
