#pragma once
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

#include "core/model/error.hpp"
#include "core/playback/audio_format.hpp"

namespace bh::playback {

// Produces interleaved float32 PCM in the format it was opened with.
class IDecoder {
 public:
  virtual ~IDecoder() = default;
  // Writes up to `frames` frames. Returning fewer than requested means end of stream;
  // returning 0 means nothing (more) to read.
  virtual std::size_t read(float* dst, std::size_t frames) = 0;
  virtual Result<void> seek(std::chrono::milliseconds position) = 0;
  virtual std::chrono::milliseconds duration() const = 0;  // 0 if unknown
};

// Opens `file`, converting to `target` (resampling / channel mixing as needed).
using DecoderFactory =
    std::function<Result<std::unique_ptr<IDecoder>>(const std::filesystem::path& file, AudioFormat target)>;

Result<std::unique_ptr<IDecoder>> open_ffmpeg_decoder(const std::filesystem::path& file, AudioFormat target);

}  // namespace bh::playback
