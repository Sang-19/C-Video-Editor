#pragma once

#include "native/engine/types.hpp"

#include <functional>
#include <string>
#include <vector>

namespace trisvax {

using UriAvailabilityChecker = std::function<bool(const std::string& uri)>;

class ProjectStore {
public:
  explicit ProjectStore(std::string workspaceRootDir);

  void setUriAvailabilityChecker(UriAvailabilityChecker checker);

  Error saveProject(Project& project);
  Result<Project> loadProject(const ProjectId& id);
  std::vector<ProjectMetadata> listProjects();
  Error renameProject(const ProjectId& id, const std::string& newName);
  Error deleteProject(const ProjectId& id);

  int validateMediaAvailability(Project& project) const;

  std::string projectDirPath(const ProjectId& id) const;
  std::string projectManifestPath(const ProjectId& id) const;

private:
  std::string rootDir_;
  UriAvailabilityChecker uriChecker_;

  bool checkSourceExists(const std::string& uri) const;
  int64_t computeProjectStorageBytes(const ProjectId& id) const;
};

} // namespace trisvax
