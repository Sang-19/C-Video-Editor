#include "native/storage/project_store.hpp"

#include "native/storage/json_codec.hpp"
#include "native/timeline/timeline_engine.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace trisvax {

namespace {

int64_t nowEpochMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

ProjectCategory inferCategory(const TimelineSnapshot& tl) {
  if (tl.clips.empty()) return ProjectCategory::Drafts;
  if (tl.aspectRatio == AspectRatioPreset::Portrait9x16) return ProjectCategory::Reels;
  return ProjectCategory::Videos;
}

} // namespace

ProjectStore::ProjectStore(std::string workspaceRootDir)
    : rootDir_(std::move(workspaceRootDir)) {
  std::error_code ec;
  fs::create_directories(rootDir_, ec);
}

void ProjectStore::setUriAvailabilityChecker(UriAvailabilityChecker checker) {
  uriChecker_ = std::move(checker);
}

std::string ProjectStore::projectDirPath(const ProjectId& id) const {
  return (fs::path(rootDir_) / id.value).string();
}

std::string ProjectStore::projectManifestPath(const ProjectId& id) const {
  return (fs::path(rootDir_) / id.value / "project.json").string();
}

bool ProjectStore::checkSourceExists(const std::string& uri) const {
  if (uri.empty()) return false;
  if (uri.rfind("content://", 0) == 0) {
    return uriChecker_ ? uriChecker_(uri) : true;
  }
  std::string path = uri;
  if (path.rfind("file://", 0) == 0) {
    path = path.substr(7);
  }
  std::error_code ec;
  return fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

int ProjectStore::validateMediaAvailability(Project& project) const {
  int missing = 0;
  for (auto& clip : project.timeline.clips) {
    clip.mediaAvailable = checkSourceExists(clip.sourceUri);
    if (!clip.mediaAvailable) {
      ++missing;
    }
  }
  project.metadata.missingMediaCount = missing;
  return missing;
}

int64_t ProjectStore::computeProjectStorageBytes(const ProjectId& id) const {
  std::error_code ec;
  int64_t sum = 0;
  const fs::path dir = projectDirPath(id);
  if (!fs::exists(dir, ec)) return 0;
  for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
    if (entry.is_regular_file(ec)) {
      sum += static_cast<int64_t>(entry.file_size(ec));
    }
  }
  return sum;
}

Error ProjectStore::saveProject(Project& project) {
  if (project.metadata.id.empty()) {
    return Error::make(ErrorCode::InvalidArgument, "Project ID cannot be empty.");
  }

  std::error_code ec;
  const fs::path dir = projectDirPath(project.metadata.id);
  fs::create_directories(dir, ec);
  if (ec) {
    return Error::make(ErrorCode::IoError, "Failed to create project directory: " + ec.message());
  }

  const int64_t now = nowEpochMs();
  if (project.metadata.createdAtMs <= 0) {
    project.metadata.createdAtMs = now;
  }
  project.metadata.lastModifiedAtMs = now;
  project.metadata.durationUs = TimelineEngine::totalDurationUs(project.timeline);
  project.metadata.category = inferCategory(project.timeline);
  if (project.metadata.thumbnailPath.empty() && !project.timeline.clips.empty()) {
    project.metadata.thumbnailPath = project.timeline.clips.front().thumbnailPath;
  }
  validateMediaAvailability(project);
  project.metadata.storageSizeBytes = computeProjectStorageBytes(project.metadata.id);

  const std::string jsonStr = ProjectJsonCodec::serializeProject(project);
  const fs::path target = projectManifestPath(project.metadata.id);
  const fs::path tmp = target.string() + ".tmp";

  {
    std::ofstream ofs(tmp, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
      return Error::make(ErrorCode::IoError, "Unable to open temp manifest for writing.");
    }
    ofs.write(jsonStr.data(), static_cast<std::streamsize>(jsonStr.size()));
    ofs.flush();
    if (!ofs.good()) {
      return Error::make(ErrorCode::IoError, "Failed while writing project manifest.");
    }
  }

  fs::rename(tmp, target, ec);
  if (ec) {
    fs::remove(tmp, ec);
    return Error::make(ErrorCode::IoError, "Failed to finalize project manifest.");
  }

  project.metadata.storageSizeBytes = computeProjectStorageBytes(project.metadata.id);
  return Error::success();
}

Result<Project> ProjectStore::loadProject(const ProjectId& id) {
  const fs::path target = projectManifestPath(id);
  std::error_code ec;
  if (!fs::exists(target, ec)) {
    return Result<Project>::failure(ErrorCode::NotFound, "Project manifest not found.");
  }
  std::ifstream ifs(target, std::ios::binary);
  if (!ifs.is_open()) {
    return Result<Project>::failure(ErrorCode::IoError, "Unable to open project manifest.");
  }
  std::ostringstream oss;
  oss << ifs.rdbuf();
  auto decoded = ProjectJsonCodec::deserializeProject(oss.str());
  if (!decoded.ok) {
    return decoded;
  }
  validateMediaAvailability(decoded.value);
  decoded.value.metadata.storageSizeBytes = computeProjectStorageBytes(id);
  return decoded;
}

std::vector<ProjectMetadata> ProjectStore::listProjects() {
  std::vector<ProjectMetadata> out;
  std::error_code ec;
  if (!fs::exists(rootDir_, ec)) return out;

  for (const auto& entry : fs::directory_iterator(rootDir_, ec)) {
    if (!entry.is_directory(ec)) continue;
    const ProjectId pid{entry.path().filename().string()};
    auto loaded = loadProject(pid);
    if (loaded.ok) {
      out.push_back(std::move(loaded.value.metadata));
    }
  }

  std::sort(out.begin(), out.end(), [](const ProjectMetadata& a, const ProjectMetadata& b) {
    return a.lastModifiedAtMs > b.lastModifiedAtMs;
  });
  return out;
}

Error ProjectStore::renameProject(const ProjectId& id, const std::string& newName) {
  auto loaded = loadProject(id);
  if (!loaded.ok) return loaded.error;
  std::string trimmed = newName;
  trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n"));
  if (!trimmed.empty()) {
    trimmed.erase(trimmed.find_last_not_of(" \t\r\n") + 1);
  }
  loaded.value.metadata.name = trimmed.empty() ? "Untitled" : trimmed;
  return saveProject(loaded.value);
}

Error ProjectStore::deleteProject(const ProjectId& id) {
  std::error_code ec;
  const fs::path dir = projectDirPath(id);
  if (!fs::exists(dir, ec)) {
    return Error::make(ErrorCode::NotFound, "Project folder does not exist.");
  }
  fs::remove_all(dir, ec);
  if (ec) {
    return Error::make(ErrorCode::IoError, "Failed to delete project directory: " + ec.message());
  }
  return Error::success();
}

} // namespace trisvax
