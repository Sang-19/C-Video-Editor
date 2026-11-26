#pragma once

#include "native/engine/types.hpp"

#include <map>
#include <string>
#include <variant>
#include <vector>

namespace trisvax {

struct JsonValue;
using JsonArray = std::vector<JsonValue>;
using JsonObject = std::map<std::string, JsonValue>;

struct JsonValue {
  using Variant = std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject>;
  Variant data{nullptr};

  JsonValue() = default;
  JsonValue(std::nullptr_t) : data(nullptr) {}
  JsonValue(bool b) : data(b) {}
  JsonValue(int i) : data(static_cast<double>(i)) {}
  JsonValue(int64_t i) : data(static_cast<double>(i)) {}
  JsonValue(double d) : data(d) {}
  JsonValue(const char* s) : data(std::string(s ? s : "")) {}
  JsonValue(std::string s) : data(std::move(s)) {}
  JsonValue(JsonArray arr) : data(std::move(arr)) {}
  JsonValue(JsonObject obj) : data(std::move(obj)) {}

  bool isNull() const noexcept { return std::holds_alternative<std::nullptr_t>(data); }
  bool isBool() const noexcept { return std::holds_alternative<bool>(data); }
  bool isNumber() const noexcept { return std::holds_alternative<double>(data); }
  bool isString() const noexcept { return std::holds_alternative<std::string>(data); }
  bool isArray() const noexcept { return std::holds_alternative<JsonArray>(data); }
  bool isObject() const noexcept { return std::holds_alternative<JsonObject>(data); }

  bool asBool(bool fallback = false) const noexcept;
  double asDouble(double fallback = 0.0) const noexcept;
  int64_t asInt64(int64_t fallback = 0) const noexcept;
  int asInt(int fallback = 0) const noexcept;
  std::string asString(const std::string& fallback = "") const;
  const JsonArray* asArray() const noexcept;
  const JsonObject* asObject() const noexcept;

  static Result<JsonValue> parse(const std::string& text);
  std::string dump(int indent = 2) const;
};

class ProjectJsonCodec {
public:
  static std::string serializeProject(const Project& project);
  static Result<Project> deserializeProject(const std::string& jsonText);

  static JsonValue serializeTimeline(const TimelineSnapshot& timeline);
  static TimelineSnapshot deserializeTimeline(const JsonValue& val);
};

} // namespace trisvax
