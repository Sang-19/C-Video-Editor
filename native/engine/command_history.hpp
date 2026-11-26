#pragma once

#include "native/engine/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace trisvax {

class IEditCommand {
public:
  virtual ~IEditCommand() = default;
  virtual std::string label() const = 0;
  virtual void execute(TimelineSnapshot& state) = 0;
  virtual void undo(TimelineSnapshot& state) = 0;
};

class SnapshotEditCommand final : public IEditCommand {
public:
  SnapshotEditCommand(std::string label, TimelineSnapshot before, TimelineSnapshot after)
      : label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}

  std::string label() const override { return label_; }
  void execute(TimelineSnapshot& state) override { state = after_; }
  void undo(TimelineSnapshot& state) override { state = before_; }

private:
  std::string label_;
  TimelineSnapshot before_;
  TimelineSnapshot after_;
};

class CommandHistory {
public:
  static constexpr size_t kMaxUndoDepth = 100;

  void pushExecuted(std::unique_ptr<IEditCommand> cmd) {
    if (!cmd) return;
    redoStack_.clear();
    undoStack_.push_back(std::move(cmd));
    if (undoStack_.size() > kMaxUndoDepth) {
      undoStack_.erase(undoStack_.begin());
    }
  }

  bool canUndo() const noexcept { return !undoStack_.empty(); }
  bool canRedo() const noexcept { return !redoStack_.empty(); }

  size_t undoCount() const noexcept { return undoStack_.size(); }
  size_t redoCount() const noexcept { return redoStack_.size(); }

  bool undo(TimelineSnapshot& state) {
    if (undoStack_.empty()) return false;
    auto cmd = std::move(undoStack_.back());
    undoStack_.pop_back();
    cmd->undo(state);
    redoStack_.push_back(std::move(cmd));
    return true;
  }

  bool redo(TimelineSnapshot& state) {
    if (redoStack_.empty()) return false;
    auto cmd = std::move(redoStack_.back());
    redoStack_.pop_back();
    cmd->execute(state);
    undoStack_.push_back(std::move(cmd));
    return true;
  }

  void clear() noexcept {
    undoStack_.clear();
    redoStack_.clear();
  }

private:
  std::vector<std::unique_ptr<IEditCommand>> undoStack_;
  std::vector<std::unique_ptr<IEditCommand>> redoStack_;
};

} // namespace trisvax
