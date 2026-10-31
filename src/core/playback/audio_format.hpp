#pragma once
namespace bh::playback {

struct AudioFormat {
  int sample_rate = 0;
  int channels = 0;
  friend bool operator==(AudioFormat, AudioFormat) = default;
};

}  // namespace bh::playback
