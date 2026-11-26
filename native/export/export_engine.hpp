#pragma once

#include "native/engine/types.hpp"
#include "native/media/media_pipeline.hpp"

#include <atomic>
#include <memory>

namespace trisvax {

class ExportEngine {
public:
  static constexpr int64_t kMinRequiredFreeBytes = 100LL * 1024LL * 1024LL; // 100 MB

  ExportEngine() = default;
  explicit ExportEngine(std::shared_ptr<MediaBackendFactory> backend);

  static ExportSettings normalizeSettings(const Project& project, const ExportSettings& requested);
  static Error validateProjectForExport(const Project& project, const ExportSettings& settings);
  static int64_t computeTotalFrames(TimeUs durationUs, int fps) noexcept;

  Error exportProject(
      const Project& project,
      const ExportSettings& requested,
      const ExportProgressCallback& progressCb);

  void cancel() noexcept;
  bool isCancelled() const noexcept;

private:
  std::shared_ptr<MediaBackendFactory> backend_;
  std::atomic<bool> cancelRequested_{false};
};

} // namespace trisvax
