#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace trisvax {

using TimeUs = int64_t;

constexpr TimeUs kMinClipDurationUs = 250'000;   // 250 ms
constexpr TimeUs kMinTrimSpanUs = 300'000;       // 300 ms
constexpr TimeUs kDefaultImageDurationUs = 3'000'000; // 3 s

constexpr TimeUs msToUs(int64_t ms) noexcept { return ms * 1'000; }
constexpr int64_t usToMs(TimeUs us) noexcept { return us / 1'000; }

template <typename Tag>
struct TypedId {
  std::string value;

  TypedId() = default;
  explicit TypedId(std::string v) : value(std::move(v)) {}

  bool empty() const noexcept { return value.empty(); }
  bool operator==(const TypedId& other) const noexcept { return value == other.value; }
  bool operator!=(const TypedId& other) const noexcept { return value != other.value; }
  bool operator<(const TypedId& other) const noexcept { return value < other.value; }
};

struct ProjectTag {};
struct ClipTag {};
struct AudioTrackTag {};
struct VoiceoverTag {};
struct TextOverlayTag {};

using ProjectId = TypedId<ProjectTag>;
using ClipId = TypedId<ClipTag>;
using AudioTrackId = TypedId<AudioTrackTag>;
using VoiceoverId = TypedId<VoiceoverTag>;
using TextOverlayId = TypedId<TextOverlayTag>;

enum class ErrorCode {
  Ok = 0,
  InvalidArgument,
  NotFound,
  IoError,
  CodecError,
  InsufficientStorage,
  Cancelled,
  PermissionDenied,
  Unsupported
};

struct Error {
  ErrorCode code = ErrorCode::Ok;
  std::string message;

  bool ok() const noexcept { return code == ErrorCode::Ok; }
  static Error success() { return {}; }
  static Error make(ErrorCode c, std::string msg) { return {c, std::move(msg)}; }
};

template <typename T>
struct Result {
  bool ok = false;
  T value{};
  Error error{};

  static Result<T> success(T val) {
    Result<T> r;
    r.ok = true;
    r.value = std::move(val);
    return r;
  }

  static Result<T> failure(ErrorCode code, std::string msg) {
    Result<T> r;
    r.ok = false;
    r.error = Error::make(code, std::move(msg));
    return r;
  }
};

enum class ProjectCategory {
  All,
  Videos,
  Reels,
  Drafts
};

enum class AspectRatioPreset {
  Portrait9x16,
  Landscape16x9,
  Square1x1,
  Freeform
};

enum class AudioSpeedMode {
  RateCoupled = 0,     // Changes pitch proportionally with speed
  PitchPreserved = 1   // Time-stretch preserving original vocal pitch
};

enum class TransitionType {
  Cut = 0,
  Fade = 1,
  CrossDissolve = 2
};

struct TransitionSpec {
  TransitionType type = TransitionType::Cut;
  TimeUs durationUs = 500'000; // 0.5s default when Fade/CrossDissolve
};

struct FilterSettings {
  float brightness = 0.0f;   // -1.0 .. 1.0
  float contrast = 1.0f;     //  0.5 .. 2.0
  float saturation = 1.0f;   //  0.0 .. 2.0
  float temperature = 0.0f;  // -1.0 .. 1.0 (cool .. warm)
  float exposure = 0.0f;     // -1.0 .. 1.0 (EV stops)
  float sharpness = 0.0f;    //  0.0 .. 1.0

  bool isIdentity() const noexcept;
  bool operator==(const FilterSettings& other) const noexcept = default;
};

struct CropSettings {
  AspectRatioPreset preset = AspectRatioPreset::Portrait9x16;
  int aspectWidth = 9;
  int aspectHeight = 16;
  float offsetX = 0.0f;      // -1.0 .. 1.0 normalized pan
  float offsetY = 0.0f;      // -1.0 .. 1.0 normalized pan
  float zoom = 1.0f;         //  1.0 .. 3.0
  int rotationDegrees = 0;   //  0, 90, 180, 270

  bool operator==(const CropSettings& other) const noexcept = default;
};

struct MediaSource {
  std::string uri;
  std::string displayName;
  bool isImage = false;
  TimeUs durationUs = kDefaultImageDurationUs;
  int width = 1080;
  int height = 1920;
  int64_t fileSizeBytes = 0;
  std::string thumbnailPath;
};

struct Clip {
  ClipId id;
  std::string sourceUri;
  std::string displayName;
  bool isImage = false;
  TimeUs sourceDurationUs = kDefaultImageDurationUs;
  TimeUs inPointUs = 0;
  TimeUs outPointUs = kDefaultImageDurationUs;
  TimeUs timelinePositionUs = 0;
  int trackIndex = 0;
  double playbackSpeed = 1.0;
  AudioSpeedMode audioSpeedMode = AudioSpeedMode::RateCoupled;
  float volume = 1.0f;       // 0.0 .. 2.0
  bool isMuted = false;
  CropSettings crop;
  FilterSettings filter;
  TransitionSpec transitionIn;
  std::string thumbnailPath;
  bool mediaAvailable = true;
};

struct AudioClip {
  AudioTrackId id;
  std::string title;
  std::string sourceUri;
  bool isBundledAsset = false;
  TimeUs sourceDurationUs = 15'000'000;
  TimeUs trimStartUs = 0;
  TimeUs trimEndUs = 15'000'000;
  TimeUs timelineStartUs = 0;
  int trackIndex = 0;
  float volume = 0.8f;       // 0.0 .. 1.0
  bool isMuted = false;
  bool loopToFit = true;
  TimeUs fadeInUs = 0;
  TimeUs fadeOutUs = 0;
};

struct VoiceoverClip {
  VoiceoverId id;
  std::string title = "Voiceover";
  std::string filePath;
  TimeUs sourceDurationUs = 0;
  TimeUs trimStartUs = 0;
  TimeUs trimEndUs = 0;
  TimeUs timelineStartUs = 0;
  int trackIndex = 1;
  float volume = 1.0f;
  bool isMuted = false;
};

enum class TextAlign {
  Left = 0,
  Center = 1,
  Right = 2
};

enum class TextAnimation {
  None = 0,
  FadeInOut = 1,
  SlideUp = 2
};

struct TextOverlay {
  TextOverlayId id;
  std::string text;
  std::string fontFamily = "Sans";
  float fontSizeSp = 28.0f;
  uint32_t colorArgb = 0xFFFFFFFF;
  TextAlign alignment = TextAlign::Center;
  float normalizedX = 0.5f;  // 0.0 .. 1.0
  float normalizedY = 0.5f;  // 0.0 .. 1.0
  TimeUs startUs = 0;
  TimeUs endUs = 3'500'000;
  int trackIndex = 0;
  TextAnimation animation = TextAnimation::None;
};

enum class TrackKind {
  Video = 0,
  Audio = 1,
  Text = 2
};

struct TimelineTrackState {
  TrackKind kind = TrackKind::Video;
  int trackIndex = 0;
  bool locked = false;
  bool muted = false;
  bool hidden = false;
  bool solo = false;
};

struct TimelineSelection {
  std::vector<ClipId> clipIds;
  std::vector<AudioTrackId> audioIds;
  std::vector<VoiceoverId> voiceoverIds;
  std::vector<TextOverlayId> textIds;

  bool empty() const noexcept {
    return clipIds.empty() && audioIds.empty() && voiceoverIds.empty() && textIds.empty();
  }
  size_t totalCount() const noexcept {
    return clipIds.size() + audioIds.size() + voiceoverIds.size() + textIds.size();
  }
};

struct TimelineSnapshot {
  AspectRatioPreset aspectRatio = AspectRatioPreset::Portrait9x16;
  int customAspectWidth = 9;
  int customAspectHeight = 16;
  std::vector<Clip> clips;
  std::optional<AudioClip> musicTrack;
  std::vector<VoiceoverClip> voiceovers;
  std::vector<TextOverlay> textOverlays;
  std::vector<TimelineTrackState> trackStates;
};

struct ExportSettings {
  std::string outputPath;
  int width = 1080;
  int height = 1920;
  int fps = 30;
  int videoBitrateBps = 12'000'000;
  int audioSampleRate = 44'100;
  int audioBitrateBps = 160'000;
  bool includeAudio = true;
  bool watermarkEnabled = false; // Non-negotiable: no watermark
};

enum class ExportStage {
  Idle = 0,
  Validating,
  Preparing,
  Rendering,
  Muxing,
  Completed,
  Failed,
  Cancelled
};

struct ExportProgress {
  ExportStage stage = ExportStage::Idle;
  int percent = 0;
  std::string statusMessage;
  std::string outputPath;
  Error error;
};

struct ProjectMetadata {
  ProjectId id;
  std::string name = "New project";
  ProjectCategory category = ProjectCategory::Drafts;
  std::string thumbnailPath;
  TimeUs durationUs = 0;
  int64_t storageSizeBytes = 0;
  int64_t createdAtMs = 0;
  int64_t lastModifiedAtMs = 0;
  std::string exportedPath;
  int missingMediaCount = 0;
};

struct Project {
  ProjectMetadata metadata;
  TimelineSnapshot timeline;
  ExportSettings exportSettings;
};

using ExportProgressCallback = std::function<void(const ExportProgress&)>;

std::string generateUuidLike(const std::string& prefix = "id");
std::string aspectPresetToString(AspectRatioPreset preset);
AspectRatioPreset aspectPresetFromString(const std::string& s);
std::string categoryToString(ProjectCategory category);
ProjectCategory categoryFromString(const std::string& s);

} // namespace trisvax
