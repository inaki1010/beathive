#include "core/tags/tag_io.hpp"

#include <taglib/fileref.h>
#include <taglib/flacproperties.h>
#include <taglib/mp4properties.h>
#include <taglib/tpropertymap.h>
#include <taglib/tvariant.h>
#include <taglib/wavproperties.h>

#include <algorithm>
#include <cstdio>
#include <tuple>

#include "core/tags/tag_parse.hpp"
#include "core/util/text.hpp"

namespace bh::tags {

namespace {

const char* const kExtensions[] = {".mp3", ".flac", ".ogg", ".oga", ".opus", ".m4a", ".mp4", ".aac", ".wav",
                                   ".wma", ".ape",  ".wv",  ".aif", ".aiff", ".mpc"};

std::string ext_lower(const std::filesystem::path& p) { return text::to_lower(p.extension().string()); }

std::string to_std(const TagLib::String& s) { return s.to8Bit(true); }
TagLib::String from_std(const std::string& s) { return TagLib::String(s, TagLib::String::UTF8); }

// All values of a property, with embedded ";"-separated lists expanded.
std::vector<std::string> list_prop(const TagLib::PropertyMap& m, const char* key) {
  std::vector<std::string> out;
  auto it = m.find(key);
  if (it == m.end()) return out;
  for (auto& v : it->second)
    for (auto& part : text::split_multi(to_std(v), ";")) out.push_back(std::move(part));
  return out;
}

std::string first_prop(const TagLib::PropertyMap& m, const char* key) {
  auto it = m.find(key);
  if (it == m.end() || it->second.isEmpty()) return {};
  return to_std(it->second.front());
}

std::string codec_of(const std::filesystem::path& file, TagLib::AudioProperties* ap) {
  auto ext = ext_lower(file);
  if (ext == ".m4a" || ext == ".mp4") {
    if (auto* mp4 = dynamic_cast<TagLib::MP4::Properties*>(ap))
      return mp4->codec() == TagLib::MP4::Properties::ALAC ? "alac" : "aac";
    return "aac";
  }
  if (ext == ".oga") return "vorbis";
  if (ext == ".ogg") return "vorbis";  // Opus-in-.ogg is rare; .opus is detected by extension
  if (ext == ".aif" || ext == ".aiff") return "aiff";
  if (ext == ".aac") return "aac";
  if (ext == ".wma") return "wma";
  return ext.empty() ? std::string{} : ext.substr(1);
}

int bit_depth_of(TagLib::AudioProperties* ap) {
  if (auto* p = dynamic_cast<TagLib::FLAC::Properties*>(ap)) return p->bitsPerSample();
  if (auto* p = dynamic_cast<TagLib::MP4::Properties*>(ap)) return p->bitsPerSample();
  if (auto* p = dynamic_cast<TagLib::RIFF::WAV::Properties*>(ap)) return p->bitsPerSample();
  return 0;
}

std::optional<CoverArt> pick_cover(TagLib::File& f) {
  auto pics = f.complexProperties("PICTURE");
  if (pics.isEmpty()) return std::nullopt;

  const TagLib::VariantMap* best = &pics.front();
  for (auto& p : pics) {
    auto it = p.find("pictureType");
    if (it != p.end() && it->second.toString() == "Front Cover") {
      best = &p;
      break;
    }
  }
  auto data = best->value("data").toByteVector();
  if (data.isEmpty()) return std::nullopt;
  CoverArt c;
  c.data.assign(data.begin(), data.end());
  c.mime_type = to_std(best->value("mimeType").toString());
  return c;
}

std::string format_double(const char* fmt, double v) {
  char buf[64];
  std::snprintf(buf, sizeof buf, fmt, v);
  return buf;
}

void set_list(TagLib::PropertyMap& m, const char* key, const std::vector<std::string>& values) {
  if (values.empty()) {
    m.erase(key);
    return;
  }
  TagLib::StringList l;
  for (auto& v : values) l.append(from_std(v));
  m.replace(key, l);
}

void set_text(TagLib::PropertyMap& m, const char* key, const std::string& value) {
  if (value.empty()) m.erase(key);
  else m.replace(key, TagLib::StringList(from_std(value)));
}

void set_pair(TagLib::PropertyMap& m, const char* key, std::optional<int> n, std::optional<int> total) {
  if (!n) {
    m.erase(key);
    return;
  }
  set_text(m, key, total ? std::to_string(*n) + "/" + std::to_string(*total) : std::to_string(*n));
}

}  // namespace

bool is_supported_extension(const std::filesystem::path& p) {
  auto e = ext_lower(p);
  return std::ranges::any_of(kExtensions, [&](const char* x) { return e == x; });
}

Result<TagFile> read_tags(const std::filesystem::path& file, ReadOptions opt) {
  TagLib::FileRef ref(file.c_str());
  if (ref.isNull() || !ref.file()->isValid())
    return std::unexpected(Error{ErrorCode::Unsupported, "unreadable or unsupported audio file"});
  // TagLib accepts e.g. a text file named .mp3; a real file always has a sample rate.
  if (!ref.audioProperties() || ref.audioProperties()->sampleRate() <= 0)
    return std::unexpected(Error{ErrorCode::Corrupt, "no audio stream found"});

  TagFile out;
  auto props = ref.file()->properties();
  auto& t = out.tags;
  t.title = first_prop(props, "TITLE");
  t.artists = list_prop(props, "ARTIST");
  t.album = first_prop(props, "ALBUM");
  t.album_artists = list_prop(props, "ALBUMARTIST");
  t.genres = list_prop(props, "GENRE");
  t.composers = list_prop(props, "COMPOSER");
  t.comment = first_prop(props, "COMMENT");
  std::tie(t.track_no, t.track_total) = parse_number_pair(first_prop(props, "TRACKNUMBER"));
  std::tie(t.disc_no, t.disc_total) = parse_number_pair(first_prop(props, "DISCNUMBER"));
  if (auto tt = parse_number_pair(first_prop(props, "TRACKTOTAL")).first; tt && !t.track_total) t.track_total = tt;
  if (auto dt = parse_number_pair(first_prop(props, "DISCTOTAL")).first; dt && !t.disc_total) t.disc_total = dt;
  t.year = parse_year(first_prop(props, "DATE"));
  if (!t.year) t.year = parse_year(first_prop(props, "YEAR"));
  t.replay_gain.track_gain_db = parse_gain_db(first_prop(props, "REPLAYGAIN_TRACK_GAIN"));
  t.replay_gain.track_peak = parse_peak(first_prop(props, "REPLAYGAIN_TRACK_PEAK"));
  t.replay_gain.album_gain_db = parse_gain_db(first_prop(props, "REPLAYGAIN_ALBUM_GAIN"));
  t.replay_gain.album_peak = parse_peak(first_prop(props, "REPLAYGAIN_ALBUM_PEAK"));
  t.has_embedded_art = ref.file()->complexPropertyKeys().contains("PICTURE");

  if (auto* ap = ref.audioProperties()) {
    out.audio.duration = std::chrono::milliseconds{ap->lengthInMilliseconds()};
    out.audio.bitrate_kbps = ap->bitrate();
    out.audio.sample_rate = ap->sampleRate();
    out.audio.channels = ap->channels();
    out.audio.bit_depth = bit_depth_of(ap);
    out.audio.codec = codec_of(file, ap);
  }
  if (opt.want_cover && t.has_embedded_art) out.cover = pick_cover(*ref.file());
  return out;
}

Result<std::optional<CoverArt>> read_cover(const std::filesystem::path& file) {
  TagLib::FileRef ref(file.c_str());
  if (ref.isNull() || !ref.file()->isValid())
    return std::unexpected(Error{ErrorCode::Unsupported, "unreadable or unsupported audio file"});
  return pick_cover(*ref.file());
}

Result<void> write_tags(const std::filesystem::path& file, const TagData& t) {
  TagLib::FileRef ref(file.c_str());
  if (ref.isNull() || !ref.file()->isValid())
    return std::unexpected(Error{ErrorCode::Unsupported, "unreadable or unsupported audio file"});

  // setProperties() replaces everything, so start from what's there and change only our keys.
  auto m = ref.file()->properties();
  set_text(m, "TITLE", t.title);
  set_list(m, "ARTIST", t.artists);
  set_text(m, "ALBUM", t.album);
  set_list(m, "ALBUMARTIST", t.album_artists);
  set_list(m, "GENRE", t.genres);
  set_list(m, "COMPOSER", t.composers);
  set_text(m, "COMMENT", t.comment);
  set_pair(m, "TRACKNUMBER", t.track_no, t.track_total);
  set_pair(m, "DISCNUMBER", t.disc_no, t.disc_total);
  m.erase("TRACKTOTAL");
  m.erase("DISCTOTAL");
  set_text(m, "DATE", t.year ? std::to_string(*t.year) : std::string{});
  m.erase("YEAR");
  const auto& rg = t.replay_gain;
  set_text(m, "REPLAYGAIN_TRACK_GAIN", rg.track_gain_db ? format_double("%.2f dB", *rg.track_gain_db) : "");
  set_text(m, "REPLAYGAIN_TRACK_PEAK", rg.track_peak ? format_double("%.6f", *rg.track_peak) : "");
  set_text(m, "REPLAYGAIN_ALBUM_GAIN", rg.album_gain_db ? format_double("%.2f dB", *rg.album_gain_db) : "");
  set_text(m, "REPLAYGAIN_ALBUM_PEAK", rg.album_peak ? format_double("%.6f", *rg.album_peak) : "");

  ref.file()->setProperties(m);
  if (!ref.save()) return std::unexpected(Error{ErrorCode::Io, "could not save tags (read-only file?)"});
  return {};
}

}  // namespace bh::tags
