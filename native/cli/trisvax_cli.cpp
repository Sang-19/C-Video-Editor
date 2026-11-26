#include "native/engine/video_editor_engine.hpp"
#include "native/export/export_engine.hpp"
#include "native/filters/filter_pipeline.hpp"
#include "native/rendering/compositor_math.hpp"
#include "native/storage/json_codec.hpp"
#include "native/timeline/timeline_engine.hpp"

#include <iostream>
#include <sstream>
#include <string>

using namespace trisvax;

namespace {

std::string readStdinAll() {
  std::ostringstream oss;
  oss << std::cin.rdbuf();
  return oss.str();
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "Usage: trisvax_cli <validate-timeline|normalize-export|shader|info>\n";
    return 1;
  }

  const std::string cmd = argv[1];
  if (cmd == "info") {
    JsonObject obj{
        {"engine", "trisvax_core"},
        {"cppStandard", 20},
        {"timeUnit", "microseconds"},
        {"capabilities", JsonArray{"trim", "split", "crop", "rotate", "speed", "volume", "filters", "transitions", "text", "audio_mix", "undo_redo", "mp4_export_1080p30"}},
    };
    std::cout << JsonValue(std::move(obj)).dump(2) << "\n";
    return 0;
  }

  if (cmd == "shader") {
    std::cout << FilterPipeline::buildGlslEs300FragmentShader() << "\n";
    return 0;
  }

  if (cmd == "validate-timeline") {
    const std::string input = readStdinAll();
    auto parsed = JsonValue::parse(input);
    if (!parsed.ok) {
      std::cerr << parsed.error.message << "\n";
      return 2;
    }
    TimelineSnapshot tl = ProjectJsonCodec::deserializeTimeline(parsed.value);
    TimelineEngine::recomputePositions(tl.clips);
    const TimeUs totalUs = TimelineEngine::totalDurationUs(tl);
    const auto dims = CompositorMath::computeTargetDimensions(tl.aspectRatio, tl.customAspectWidth, tl.customAspectHeight);

    JsonObject resp{
        {"valid", !tl.clips.empty() && totalUs >= kMinClipDurationUs},
        {"totalDurationUs", totalUs},
        {"totalDurationMs", usToMs(totalUs)},
        {"exportWidth", dims.width},
        {"exportHeight", dims.height},
        {"estimatedFrames30Fps", ExportEngine::computeTotalFrames(totalUs, 30)},
        {"timeline", ProjectJsonCodec::serializeTimeline(tl)},
    };
    std::cout << JsonValue(std::move(resp)).dump(2) << "\n";
    return 0;
  }

  if (cmd == "normalize-export") {
    const std::string input = readStdinAll();
    auto parsed = JsonValue::parse(input);
    if (!parsed.ok) {
      std::cerr << parsed.error.message << "\n";
      return 2;
    }
    Project proj;
    proj.timeline = ProjectJsonCodec::deserializeTimeline(parsed.value);
    TimelineEngine::recomputePositions(proj.timeline.clips);
    for (auto& c : proj.timeline.clips) {
      c.mediaAvailable = true;
    }
    ExportSettings req;
    req.outputPath = "export.mp4";
    req.fps = 30;
    const ExportSettings norm = ExportEngine::normalizeSettings(proj, req);
    const Error valErr = ExportEngine::validateProjectForExport(proj, norm);
    const TimeUs totalUs = TimelineEngine::totalDurationUs(proj.timeline);

    JsonObject resp{
        {"valid", valErr.ok()},
        {"error", valErr.message},
        {"width", norm.width},
        {"height", norm.height},
        {"fps", norm.fps},
        {"videoBitrateBps", norm.videoBitrateBps},
        {"audioBitrateBps", norm.audioBitrateBps},
        {"watermarkEnabled", norm.watermarkEnabled},
        {"totalDurationUs", totalUs},
        {"totalFrames", ExportEngine::computeTotalFrames(totalUs, norm.fps)},
    };
    std::cout << JsonValue(std::move(resp)).dump(2) << "\n";
    return valErr.ok() ? 0 : 3;
  }

  std::cerr << "Unknown command: " << cmd << "\n";
  return 1;
}
