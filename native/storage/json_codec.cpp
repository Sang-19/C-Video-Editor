#include "native/storage/json_codec.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>

namespace trisvax {

namespace {

class Parser {
public:
  explicit Parser(const std::string& src) : s_(src) {}

  Result<JsonValue> parseRoot() {
    skipWs();
    auto res = parseValue();
    if (!res.ok) return res;
    skipWs();
    if (pos_ != s_.size()) {
      return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Trailing characters in JSON");
    }
    return res;
  }

private:
  const std::string& s_;
  size_t pos_ = 0;

  void skipWs() noexcept {
    while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) {
      ++pos_;
    }
  }

  Result<JsonValue> parseValue() {
    skipWs();
    if (pos_ >= s_.size()) {
      return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Unexpected end of JSON");
    }
    char c = s_[pos_];
    if (c == '{') return parseObject();
    if (c == '[') return parseArray();
    if (c == '"') {
      auto str = parseString();
      if (!str.ok) return Result<JsonValue>::failure(str.error.code, str.error.message);
      return Result<JsonValue>::success(JsonValue(std::move(str.value)));
    }
    if (c == 't' && s_.compare(pos_, 4, "true") == 0) {
      pos_ += 4;
      return Result<JsonValue>::success(JsonValue(true));
    }
    if (c == 'f' && s_.compare(pos_, 5, "false") == 0) {
      pos_ += 5;
      return Result<JsonValue>::success(JsonValue(false));
    }
    if (c == 'n' && s_.compare(pos_, 4, "null") == 0) {
      pos_ += 4;
      return Result<JsonValue>::success(JsonValue(nullptr));
    }
    if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
      return parseNumber();
    }
    return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Invalid JSON token");
  }

  Result<std::string> parseString() {
    if (pos_ >= s_.size() || s_[pos_] != '"') {
      return Result<std::string>::failure(ErrorCode::InvalidArgument, "Expected string");
    }
    ++pos_;
    std::string out;
    while (pos_ < s_.size()) {
      char c = s_[pos_++];
      if (c == '"') return Result<std::string>::success(std::move(out));
      if (c == '\\') {
        if (pos_ >= s_.size()) {
          return Result<std::string>::failure(ErrorCode::InvalidArgument, "Unterminated escape");
        }
        char esc = s_[pos_++];
        switch (esc) {
          case '"': out.push_back('"'); break;
          case '\\': out.push_back('\\'); break;
          case '/': out.push_back('/'); break;
          case 'b': out.push_back('\b'); break;
          case 'f': out.push_back('\f'); break;
          case 'n': out.push_back('\n'); break;
          case 'r': out.push_back('\r'); break;
          case 't': out.push_back('\t'); break;
          case 'u': {
            if (pos_ + 4 > s_.size()) {
              return Result<std::string>::failure(ErrorCode::InvalidArgument, "Invalid unicode escape");
            }
            unsigned int cp = std::stoul(s_.substr(pos_, 4), nullptr, 16);
            pos_ += 4;
            if (cp < 0x80) {
              out.push_back(static_cast<char>(cp));
            } else if (cp < 0x800) {
              out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            } else {
              out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
              out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
              out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default:
            out.push_back(esc);
            break;
        }
      } else {
        out.push_back(c);
      }
    }
    return Result<std::string>::failure(ErrorCode::InvalidArgument, "Unterminated string");
  }

  Result<JsonValue> parseNumber() {
    const size_t start = pos_;
    if (s_[pos_] == '-') ++pos_;
    while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    if (pos_ < s_.size() && s_[pos_] == '.') {
      ++pos_;
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
      while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }
    const std::string numStr = s_.substr(start, pos_ - start);
    char* endPtr = nullptr;
    const double val = std::strtod(numStr.c_str(), &endPtr);
    if (endPtr == numStr.c_str()) {
      return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Invalid number");
    }
    return Result<JsonValue>::success(JsonValue(val));
  }

  Result<JsonValue> parseArray() {
    ++pos_; // '['
    JsonArray arr;
    skipWs();
    if (pos_ < s_.size() && s_[pos_] == ']') {
      ++pos_;
      return Result<JsonValue>::success(JsonValue(std::move(arr)));
    }
    while (pos_ < s_.size()) {
      auto item = parseValue();
      if (!item.ok) return item;
      arr.push_back(std::move(item.value));
      skipWs();
      if (pos_ < s_.size() && s_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (pos_ < s_.size() && s_[pos_] == ']') {
        ++pos_;
        return Result<JsonValue>::success(JsonValue(std::move(arr)));
      }
      return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Expected ',' or ']' in array");
    }
    return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Unterminated array");
  }

  Result<JsonValue> parseObject() {
    ++pos_; // '{'
    JsonObject obj;
    skipWs();
    if (pos_ < s_.size() && s_[pos_] == '}') {
      ++pos_;
      return Result<JsonValue>::success(JsonValue(std::move(obj)));
    }
    while (pos_ < s_.size()) {
      skipWs();
      auto key = parseString();
      if (!key.ok) return Result<JsonValue>::failure(key.error.code, key.error.message);
      skipWs();
      if (pos_ >= s_.size() || s_[pos_] != ':') {
        return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Expected ':' in object");
      }
      ++pos_;
      auto val = parseValue();
      if (!val.ok) return val;
      obj[std::move(key.value)] = std::move(val.value);
      skipWs();
      if (pos_ < s_.size() && s_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (pos_ < s_.size() && s_[pos_] == '}') {
        ++pos_;
        return Result<JsonValue>::success(JsonValue(std::move(obj)));
      }
      return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Expected ',' or '}' in object");
    }
    return Result<JsonValue>::failure(ErrorCode::InvalidArgument, "Unterminated object");
  }
};

void escapeJsonString(std::ostringstream& oss, const std::string& str) {
  oss << '"';
  for (unsigned char c : str) {
    switch (c) {
      case '"': oss << "\\\""; break;
      case '\\': oss << "\\\\"; break;
      case '\b': oss << "\\b"; break;
      case '\f': oss << "\\f"; break;
      case '\n': oss << "\\n"; break;
      case '\r': oss << "\\r"; break;
      case '\t': oss << "\\t"; break;
      default:
        if (c < 0x20) {
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
        } else {
          oss << static_cast<char>(c);
        }
        break;
    }
  }
  oss << '"';
}

void dumpRec(std::ostringstream& oss, const JsonValue& v, int indent, int depth) {
  auto pad = [&](int d) {
    if (indent > 0) oss << std::string(static_cast<size_t>(d * indent), ' ');
  };
  const std::string nl = indent > 0 ? "\n" : "";

  if (v.isNull()) {
    oss << "null";
  } else if (v.isBool()) {
    oss << (v.asBool() ? "true" : "false");
  } else if (v.isNumber()) {
    double d = v.asDouble();
    if (std::isfinite(d) && std::fabs(d - std::round(d)) < 1e-9 && std::fabs(d) < 9e15) {
      oss << static_cast<int64_t>(std::llround(d));
    } else {
      oss << std::setprecision(12) << d;
    }
  } else if (v.isString()) {
    escapeJsonString(oss, v.asString());
  } else if (const auto* arr = v.asArray()) {
    if (arr->empty()) {
      oss << "[]";
      return;
    }
    oss << "[" << nl;
    for (size_t i = 0; i < arr->size(); ++i) {
      pad(depth + 1);
      dumpRec(oss, (*arr)[i], indent, depth + 1);
      if (i + 1 < arr->size()) oss << ",";
      oss << nl;
    }
    pad(depth);
    oss << "]";
  } else if (const auto* obj = v.asObject()) {
    if (obj->empty()) {
      oss << "{}";
      return;
    }
    oss << "{" << nl;
    size_t idx = 0;
    for (const auto& [k, val] : *obj) {
      pad(depth + 1);
      escapeJsonString(oss, k);
      oss << (indent > 0 ? ": " : ":");
      dumpRec(oss, val, indent, depth + 1);
      if (++idx < obj->size()) oss << ",";
      oss << nl;
    }
    pad(depth);
    oss << "}";
  }
}

const JsonValue* getField(const JsonObject* obj, const std::string& key) {
  if (!obj) return nullptr;
  auto it = obj->find(key);
  return it != obj->end() ? &it->second : nullptr;
}

} // namespace

bool JsonValue::asBool(bool fallback) const noexcept {
  if (auto* b = std::get_if<bool>(&data)) return *b;
  return fallback;
}

double JsonValue::asDouble(double fallback) const noexcept {
  if (auto* d = std::get_if<double>(&data)) return *d;
  return fallback;
}

int64_t JsonValue::asInt64(int64_t fallback) const noexcept {
  if (auto* d = std::get_if<double>(&data)) return static_cast<int64_t>(std::llround(*d));
  return fallback;
}

int JsonValue::asInt(int fallback) const noexcept {
  return static_cast<int>(asInt64(fallback));
}

std::string JsonValue::asString(const std::string& fallback) const {
  if (auto* s = std::get_if<std::string>(&data)) return *s;
  return fallback;
}

const JsonArray* JsonValue::asArray() const noexcept {
  return std::get_if<JsonArray>(&data);
}

const JsonObject* JsonValue::asObject() const noexcept {
  return std::get_if<JsonObject>(&data);
}

Result<JsonValue> JsonValue::parse(const std::string& text) {
  Parser p(text);
  return p.parseRoot();
}

std::string JsonValue::dump(int indent) const {
  std::ostringstream oss;
  dumpRec(oss, *this, indent, 0);
  return oss.str();
}

JsonValue ProjectJsonCodec::serializeTimeline(const TimelineSnapshot& timeline) {
  JsonArray clipsArr;
  for (const auto& c : timeline.clips) {
    JsonObject cropObj{
        {"preset", aspectPresetToString(c.crop.preset)},
        {"aspectWidth", c.crop.aspectWidth},
        {"aspectHeight", c.crop.aspectHeight},
        {"offsetX", static_cast<double>(c.crop.offsetX)},
        {"offsetY", static_cast<double>(c.crop.offsetY)},
        {"zoom", static_cast<double>(c.crop.zoom)},
        {"rotationDegrees", c.crop.rotationDegrees},
    };
    JsonObject filterObj{
        {"brightness", static_cast<double>(c.filter.brightness)},
        {"contrast", static_cast<double>(c.filter.contrast)},
        {"saturation", static_cast<double>(c.filter.saturation)},
        {"temperature", static_cast<double>(c.filter.temperature)},
        {"exposure", static_cast<double>(c.filter.exposure)},
        {"sharpness", static_cast<double>(c.filter.sharpness)},
    };
    JsonObject transObj{
        {"type", static_cast<int>(c.transitionIn.type)},
        {"durationUs", c.transitionIn.durationUs},
    };
    clipsArr.emplace_back(JsonObject{
        {"id", c.id.value},
        {"sourceUri", c.sourceUri},
        {"displayName", c.displayName},
        {"isImage", c.isImage},
        {"sourceDurationUs", c.sourceDurationUs},
        {"inPointUs", c.inPointUs},
        {"outPointUs", c.outPointUs},
        {"timelinePositionUs", c.timelinePositionUs},
        {"trackIndex", c.trackIndex},
        {"playbackSpeed", c.playbackSpeed},
        {"audioSpeedMode", static_cast<int>(c.audioSpeedMode)},
        {"volume", static_cast<double>(c.volume)},
        {"isMuted", c.isMuted},
        {"crop", std::move(cropObj)},
        {"filter", std::move(filterObj)},
        {"transitionIn", std::move(transObj)},
        {"thumbnailPath", c.thumbnailPath},
        {"mediaAvailable", c.mediaAvailable},
    });
  }

  JsonValue musicVal{nullptr};
  if (timeline.musicTrack.has_value()) {
    const auto& m = *timeline.musicTrack;
    musicVal = JsonObject{
        {"id", m.id.value},
        {"title", m.title},
        {"sourceUri", m.sourceUri},
        {"isBundledAsset", m.isBundledAsset},
        {"sourceDurationUs", m.sourceDurationUs},
        {"trimStartUs", m.trimStartUs},
        {"trimEndUs", m.trimEndUs},
        {"timelineStartUs", m.timelineStartUs},
        {"trackIndex", m.trackIndex},
        {"volume", static_cast<double>(m.volume)},
        {"isMuted", m.isMuted},
        {"loopToFit", m.loopToFit},
        {"fadeInUs", m.fadeInUs},
        {"fadeOutUs", m.fadeOutUs},
    };
  }

  JsonArray voArr;
  for (const auto& v : timeline.voiceovers) {
    voArr.emplace_back(JsonObject{
        {"id", v.id.value},
        {"title", v.title},
        {"filePath", v.filePath},
        {"sourceDurationUs", v.sourceDurationUs},
        {"trimStartUs", v.trimStartUs},
        {"trimEndUs", v.trimEndUs},
        {"timelineStartUs", v.timelineStartUs},
        {"trackIndex", v.trackIndex},
        {"volume", static_cast<double>(v.volume)},
        {"isMuted", v.isMuted},
    });
  }

  JsonArray textArr;
  for (const auto& t : timeline.textOverlays) {
    textArr.emplace_back(JsonObject{
        {"id", t.id.value},
        {"text", t.text},
        {"fontFamily", t.fontFamily},
        {"fontSizeSp", static_cast<double>(t.fontSizeSp)},
        {"colorArgb", static_cast<int64_t>(t.colorArgb)},
        {"alignment", static_cast<int>(t.alignment)},
        {"normalizedX", static_cast<double>(t.normalizedX)},
        {"normalizedY", static_cast<double>(t.normalizedY)},
        {"startUs", t.startUs},
        {"endUs", t.endUs},
        {"trackIndex", t.trackIndex},
        {"animation", static_cast<int>(t.animation)},
    });
  }

  JsonArray trackStateArr;
  for (const auto& ts : timeline.trackStates) {
    trackStateArr.emplace_back(JsonObject{
        {"kind", static_cast<int>(ts.kind)},
        {"trackIndex", ts.trackIndex},
        {"locked", ts.locked},
        {"muted", ts.muted},
        {"hidden", ts.hidden},
        {"solo", ts.solo},
    });
  }

  return JsonObject{
      {"aspectRatio", aspectPresetToString(timeline.aspectRatio)},
      {"customAspectWidth", timeline.customAspectWidth},
      {"customAspectHeight", timeline.customAspectHeight},
      {"clips", std::move(clipsArr)},
      {"musicTrack", std::move(musicVal)},
      {"voiceovers", std::move(voArr)},
      {"textOverlays", std::move(textArr)},
      {"trackStates", std::move(trackStateArr)},
  };
}

TimelineSnapshot ProjectJsonCodec::deserializeTimeline(const JsonValue& val) {
  TimelineSnapshot t;
  const auto* obj = val.asObject();
  if (!obj) return t;

  if (auto* f = getField(obj, "aspectRatio")) t.aspectRatio = aspectPresetFromString(f->asString());
  if (auto* f = getField(obj, "customAspectWidth")) t.customAspectWidth = f->asInt(9);
  if (auto* f = getField(obj, "customAspectHeight")) t.customAspectHeight = f->asInt(16);

  if (auto* clipsVal = getField(obj, "clips")) {
    if (auto* arr = clipsVal->asArray()) {
      for (const auto& item : *arr) {
        const auto* co = item.asObject();
        if (!co) continue;
        Clip c;
        if (auto* f = getField(co, "id")) c.id = ClipId{f->asString()};
        if (auto* f = getField(co, "sourceUri")) c.sourceUri = f->asString();
        if (auto* f = getField(co, "displayName")) c.displayName = f->asString();
        if (auto* f = getField(co, "isImage")) c.isImage = f->asBool(false);
        if (auto* f = getField(co, "sourceDurationUs")) c.sourceDurationUs = f->asInt64(kDefaultImageDurationUs);
        if (auto* f = getField(co, "inPointUs")) c.inPointUs = f->asInt64(0);
        if (auto* f = getField(co, "outPointUs")) c.outPointUs = f->asInt64(c.sourceDurationUs);
        if (auto* f = getField(co, "timelinePositionUs")) c.timelinePositionUs = f->asInt64(0);
        if (auto* f = getField(co, "trackIndex")) c.trackIndex = f->asInt(0);
        if (auto* f = getField(co, "playbackSpeed")) c.playbackSpeed = f->asDouble(1.0);
        if (auto* f = getField(co, "audioSpeedMode")) c.audioSpeedMode = static_cast<AudioSpeedMode>(f->asInt(0));
        if (auto* f = getField(co, "volume")) c.volume = static_cast<float>(f->asDouble(1.0));
        if (auto* f = getField(co, "isMuted")) c.isMuted = f->asBool(false);
        if (auto* f = getField(co, "thumbnailPath")) c.thumbnailPath = f->asString();
        if (auto* f = getField(co, "mediaAvailable")) c.mediaAvailable = f->asBool(true);

        if (auto* cropVal = getField(co, "crop")) {
          if (auto* cro = cropVal->asObject()) {
            if (auto* f = getField(cro, "preset")) c.crop.preset = aspectPresetFromString(f->asString());
            if (auto* f = getField(cro, "aspectWidth")) c.crop.aspectWidth = f->asInt(9);
            if (auto* f = getField(cro, "aspectHeight")) c.crop.aspectHeight = f->asInt(16);
            if (auto* f = getField(cro, "offsetX")) c.crop.offsetX = static_cast<float>(f->asDouble(0.0));
            if (auto* f = getField(cro, "offsetY")) c.crop.offsetY = static_cast<float>(f->asDouble(0.0));
            if (auto* f = getField(cro, "zoom")) c.crop.zoom = static_cast<float>(f->asDouble(1.0));
            if (auto* f = getField(cro, "rotationDegrees")) c.crop.rotationDegrees = f->asInt(0);
          }
        }
        if (auto* fltVal = getField(co, "filter")) {
          if (auto* fo = fltVal->asObject()) {
            if (auto* f = getField(fo, "brightness")) c.filter.brightness = static_cast<float>(f->asDouble(0.0));
            if (auto* f = getField(fo, "contrast")) c.filter.contrast = static_cast<float>(f->asDouble(1.0));
            if (auto* f = getField(fo, "saturation")) c.filter.saturation = static_cast<float>(f->asDouble(1.0));
            if (auto* f = getField(fo, "temperature")) c.filter.temperature = static_cast<float>(f->asDouble(0.0));
            if (auto* f = getField(fo, "exposure")) c.filter.exposure = static_cast<float>(f->asDouble(0.0));
            if (auto* f = getField(fo, "sharpness")) c.filter.sharpness = static_cast<float>(f->asDouble(0.0));
          }
        }
        if (auto* trVal = getField(co, "transitionIn")) {
          if (auto* tro = trVal->asObject()) {
            if (auto* f = getField(tro, "type")) c.transitionIn.type = static_cast<TransitionType>(f->asInt(0));
            if (auto* f = getField(tro, "durationUs")) c.transitionIn.durationUs = f->asInt64(0);
          }
        }
        t.clips.push_back(std::move(c));
      }
    }
  }

  if (auto* musicVal = getField(obj, "musicTrack")) {
    if (auto* mo = musicVal->asObject()) {
      AudioClip m;
      if (auto* f = getField(mo, "id")) m.id = AudioTrackId{f->asString()};
      if (auto* f = getField(mo, "title")) m.title = f->asString();
      if (auto* f = getField(mo, "sourceUri")) m.sourceUri = f->asString();
      if (auto* f = getField(mo, "isBundledAsset")) m.isBundledAsset = f->asBool(false);
      if (auto* f = getField(mo, "sourceDurationUs")) m.sourceDurationUs = f->asInt64(15'000'000);
      if (auto* f = getField(mo, "trimStartUs")) m.trimStartUs = f->asInt64(0);
      if (auto* f = getField(mo, "trimEndUs")) m.trimEndUs = f->asInt64(m.sourceDurationUs);
      if (auto* f = getField(mo, "timelineStartUs")) m.timelineStartUs = f->asInt64(0);
      if (auto* f = getField(mo, "trackIndex")) m.trackIndex = f->asInt(0);
      if (auto* f = getField(mo, "volume")) m.volume = static_cast<float>(f->asDouble(0.8));
      if (auto* f = getField(mo, "isMuted")) m.isMuted = f->asBool(false);
      if (auto* f = getField(mo, "loopToFit")) m.loopToFit = f->asBool(true);
      if (auto* f = getField(mo, "fadeInUs")) m.fadeInUs = f->asInt64(0);
      if (auto* f = getField(mo, "fadeOutUs")) m.fadeOutUs = f->asInt64(0);
      t.musicTrack = std::move(m);
    }
  }

  if (auto* voVal = getField(obj, "voiceovers")) {
    if (auto* arr = voVal->asArray()) {
      for (const auto& item : *arr) {
        const auto* vo = item.asObject();
        if (!vo) continue;
        VoiceoverClip v;
        if (auto* f = getField(vo, "id")) v.id = VoiceoverId{f->asString()};
        if (auto* f = getField(vo, "title")) v.title = f->asString("Voiceover");
        if (auto* f = getField(vo, "filePath")) v.filePath = f->asString();
        if (auto* f = getField(vo, "sourceDurationUs")) v.sourceDurationUs = f->asInt64(0);
        if (auto* f = getField(vo, "trimStartUs")) v.trimStartUs = f->asInt64(0);
        if (auto* f = getField(vo, "trimEndUs")) v.trimEndUs = f->asInt64(v.sourceDurationUs);
        if (auto* f = getField(vo, "timelineStartUs")) v.timelineStartUs = f->asInt64(0);
        if (auto* f = getField(vo, "trackIndex")) v.trackIndex = f->asInt(1);
        if (auto* f = getField(vo, "volume")) v.volume = static_cast<float>(f->asDouble(1.0));
        if (auto* f = getField(vo, "isMuted")) v.isMuted = f->asBool(false);
        t.voiceovers.push_back(std::move(v));
      }
    }
  }

  if (auto* textVal = getField(obj, "textOverlays")) {
    if (auto* arr = textVal->asArray()) {
      for (const auto& item : *arr) {
        const auto* to = item.asObject();
        if (!to) continue;
        TextOverlay tx;
        if (auto* f = getField(to, "id")) tx.id = TextOverlayId{f->asString()};
        if (auto* f = getField(to, "text")) tx.text = f->asString();
        if (auto* f = getField(to, "fontFamily")) tx.fontFamily = f->asString("Sans");
        if (auto* f = getField(to, "fontSizeSp")) tx.fontSizeSp = static_cast<float>(f->asDouble(28.0));
        if (auto* f = getField(to, "colorArgb")) tx.colorArgb = static_cast<uint32_t>(f->asInt64(0xFFFFFFFF));
        if (auto* f = getField(to, "alignment")) tx.alignment = static_cast<TextAlign>(f->asInt(1));
        if (auto* f = getField(to, "normalizedX")) tx.normalizedX = static_cast<float>(f->asDouble(0.5));
        if (auto* f = getField(to, "normalizedY")) tx.normalizedY = static_cast<float>(f->asDouble(0.5));
        if (auto* f = getField(to, "startUs")) tx.startUs = f->asInt64(0);
        if (auto* f = getField(to, "endUs")) tx.endUs = f->asInt64(3'500'000);
        if (auto* f = getField(to, "trackIndex")) tx.trackIndex = f->asInt(0);
        if (auto* f = getField(to, "animation")) tx.animation = static_cast<TextAnimation>(f->asInt(0));
        t.textOverlays.push_back(std::move(tx));
      }
    }
  }

  if (auto* tsVal = getField(obj, "trackStates")) {
    if (auto* arr = tsVal->asArray()) {
      for (const auto& item : *arr) {
        const auto* tso = item.asObject();
        if (!tso) continue;
        TimelineTrackState ts;
        if (auto* f = getField(tso, "kind")) ts.kind = static_cast<TrackKind>(f->asInt(0));
        if (auto* f = getField(tso, "trackIndex")) ts.trackIndex = f->asInt(0);
        if (auto* f = getField(tso, "locked")) ts.locked = f->asBool(false);
        if (auto* f = getField(tso, "muted")) ts.muted = f->asBool(false);
        if (auto* f = getField(tso, "hidden")) ts.hidden = f->asBool(false);
        if (auto* f = getField(tso, "solo")) ts.solo = f->asBool(false);
        t.trackStates.push_back(ts);
      }
    }
  }

  return t;
}

std::string ProjectJsonCodec::serializeProject(const Project& project) {
  JsonObject metaObj{
      {"id", project.metadata.id.value},
      {"name", project.metadata.name},
      {"category", categoryToString(project.metadata.category)},
      {"thumbnailPath", project.metadata.thumbnailPath},
      {"durationUs", project.metadata.durationUs},
      {"storageSizeBytes", project.metadata.storageSizeBytes},
      {"createdAtMs", project.metadata.createdAtMs},
      {"lastModifiedAtMs", project.metadata.lastModifiedAtMs},
      {"exportedPath", project.metadata.exportedPath},
      {"missingMediaCount", project.metadata.missingMediaCount},
  };
  JsonObject exportObj{
      {"outputPath", project.exportSettings.outputPath},
      {"width", project.exportSettings.width},
      {"height", project.exportSettings.height},
      {"fps", project.exportSettings.fps},
      {"videoBitrateBps", project.exportSettings.videoBitrateBps},
      {"audioSampleRate", project.exportSettings.audioSampleRate},
      {"audioBitrateBps", project.exportSettings.audioBitrateBps},
      {"includeAudio", project.exportSettings.includeAudio},
      {"watermarkEnabled", false},
  };
  JsonValue root{JsonObject{
      {"schemaVersion", 1},
      {"metadata", std::move(metaObj)},
      {"timeline", serializeTimeline(project.timeline)},
      {"exportSettings", std::move(exportObj)},
  }};
  return root.dump(2);
}

Result<Project> ProjectJsonCodec::deserializeProject(const std::string& jsonText) {
  auto parsed = JsonValue::parse(jsonText);
  if (!parsed.ok) {
    return Result<Project>::failure(parsed.error.code, parsed.error.message);
  }
  const auto* root = parsed.value.asObject();
  if (!root) {
    return Result<Project>::failure(ErrorCode::InvalidArgument, "Project JSON root is not an object");
  }

  Project p;
  if (auto* metaVal = getField(root, "metadata")) {
    if (auto* mo = metaVal->asObject()) {
      if (auto* f = getField(mo, "id")) p.metadata.id = ProjectId{f->asString()};
      if (auto* f = getField(mo, "name")) p.metadata.name = f->asString("New project");
      if (auto* f = getField(mo, "category")) p.metadata.category = categoryFromString(f->asString());
      if (auto* f = getField(mo, "thumbnailPath")) p.metadata.thumbnailPath = f->asString();
      if (auto* f = getField(mo, "durationUs")) p.metadata.durationUs = f->asInt64(0);
      if (auto* f = getField(mo, "storageSizeBytes")) p.metadata.storageSizeBytes = f->asInt64(0);
      if (auto* f = getField(mo, "createdAtMs")) p.metadata.createdAtMs = f->asInt64(0);
      if (auto* f = getField(mo, "lastModifiedAtMs")) p.metadata.lastModifiedAtMs = f->asInt64(0);
      if (auto* f = getField(mo, "exportedPath")) p.metadata.exportedPath = f->asString();
      if (auto* f = getField(mo, "missingMediaCount")) p.metadata.missingMediaCount = f->asInt(0);
    }
  }

  if (auto* tlVal = getField(root, "timeline")) {
    p.timeline = deserializeTimeline(*tlVal);
  }

  if (auto* expVal = getField(root, "exportSettings")) {
    if (auto* eo = expVal->asObject()) {
      if (auto* f = getField(eo, "outputPath")) p.exportSettings.outputPath = f->asString();
      if (auto* f = getField(eo, "width")) p.exportSettings.width = f->asInt(1080);
      if (auto* f = getField(eo, "height")) p.exportSettings.height = f->asInt(1920);
      if (auto* f = getField(eo, "fps")) p.exportSettings.fps = f->asInt(30);
      if (auto* f = getField(eo, "videoBitrateBps")) p.exportSettings.videoBitrateBps = f->asInt(12'000'000);
      if (auto* f = getField(eo, "audioSampleRate")) p.exportSettings.audioSampleRate = f->asInt(44'100);
      if (auto* f = getField(eo, "audioBitrateBps")) p.exportSettings.audioBitrateBps = f->asInt(160'000);
      if (auto* f = getField(eo, "includeAudio")) p.exportSettings.includeAudio = f->asBool(true);
      p.exportSettings.watermarkEnabled = false;
    }
  }

  return Result<Project>::success(std::move(p));
}

} // namespace trisvax
