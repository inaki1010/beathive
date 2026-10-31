extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <cstring>
#include <vector>

#include "core/playback/decoder.hpp"
#include "core/util/paths.hpp"

namespace bh::playback {

namespace {

struct FormatCtxDeleter { void operator()(AVFormatContext* c) const { avformat_close_input(&c); } };
struct CodecCtxDeleter { void operator()(AVCodecContext* c) const { avcodec_free_context(&c); } };
struct SwrDeleter { void operator()(SwrContext* c) const { swr_free(&c); } };
struct FrameDeleter { void operator()(AVFrame* f) const { av_frame_free(&f); } };
struct PacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };

Error ff_error(const char* what, int code) {
  char buf[AV_ERROR_MAX_STRING_SIZE] = {};
  av_strerror(code, buf, sizeof buf);
  return {code == AVERROR(ENOENT) ? ErrorCode::NotFound : ErrorCode::Unsupported, std::string(what) + ": " + buf};
}

class FFmpegDecoder final : public IDecoder {
 public:
  Result<void> open(const std::filesystem::path& file, AudioFormat target) {
    target_ = target;
    AVFormatContext* raw = nullptr;
    int rc = avformat_open_input(&raw, to_utf8(file).c_str(), nullptr, nullptr);
    if (rc < 0) return std::unexpected(ff_error("open", rc));
    fmt_.reset(raw);
    if ((rc = avformat_find_stream_info(fmt_.get(), nullptr)) < 0) return std::unexpected(ff_error("probe", rc));

    const AVCodec* codec = nullptr;
    stream_ = av_find_best_stream(fmt_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream_ < 0) return std::unexpected(Error{ErrorCode::Unsupported, "no audio stream"});

    dec_.reset(avcodec_alloc_context3(codec));
    AVStream* st = fmt_->streams[stream_];
    if ((rc = avcodec_parameters_to_context(dec_.get(), st->codecpar)) < 0) return std::unexpected(ff_error("params", rc));
    if ((rc = avcodec_open2(dec_.get(), codec, nullptr)) < 0) return std::unexpected(ff_error("codec", rc));

    packet_.reset(av_packet_alloc());
    frame_.reset(av_frame_alloc());
    BH_TRY(init_resampler());

    if (fmt_->duration > 0)
      duration_ = std::chrono::milliseconds{fmt_->duration * 1000 / AV_TIME_BASE};
    return {};
  }

  std::size_t read(float* dst, std::size_t frames) override {
    const auto ch = static_cast<std::size_t>(target_.channels);
    std::size_t done = 0;
    while (done < frames) {
      if (pending_pos_ >= pending_.size()) {
        pending_.clear();
        pending_pos_ = 0;
        if (!fill_pending()) break;  // end of stream
      }
      auto avail = (pending_.size() - pending_pos_) / ch;
      auto n = std::min(avail, frames - done);
      std::memcpy(dst + done * ch, pending_.data() + pending_pos_, n * ch * sizeof(float));
      pending_pos_ += n * ch;
      done += n;
    }
    return done;
  }

  Result<void> seek(std::chrono::milliseconds pos) override {
    AVStream* st = fmt_->streams[stream_];
    int64_t ts = av_rescale_q(pos.count(), AVRational{1, 1000}, st->time_base);
    int rc = av_seek_frame(fmt_.get(), stream_, ts, AVSEEK_FLAG_BACKWARD);
    if (rc < 0) return std::unexpected(ff_error("seek", rc));
    avcodec_flush_buffers(dec_.get());
    BH_TRY(init_resampler());  // drops samples buffered inside swr
    pending_.clear();
    pending_pos_ = 0;
    eof_ = false;
    draining_ = false;
    // Seeking lands on a packet at or before the target; discard up to the exact frame.
    skip_until_ = static_cast<int64_t>(pos.count()) * target_.sample_rate / 1000;
    next_frame_ = -1;
    return {};
  }

  std::chrono::milliseconds duration() const override { return duration_; }

 private:
  Result<void> init_resampler() {
    swr_.reset();
    AVChannelLayout out_layout;
    av_channel_layout_default(&out_layout, target_.channels);
    SwrContext* raw = nullptr;
    int rc = swr_alloc_set_opts2(&raw, &out_layout, AV_SAMPLE_FMT_FLT, target_.sample_rate, &dec_->ch_layout,
                                 dec_->sample_fmt, dec_->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&out_layout);
    if (rc < 0) return std::unexpected(ff_error("resampler", rc));
    swr_.reset(raw);
    if ((rc = swr_init(swr_.get())) < 0) return std::unexpected(ff_error("resampler init", rc));
    return {};
  }

  // Decodes more audio into pending_. Returns false at end of stream.
  bool fill_pending() {
    for (;;) {
      if (!draining_) {
        int rc = av_read_frame(fmt_.get(), packet_.get());
        if (rc < 0) {
          avcodec_send_packet(dec_.get(), nullptr);  // flush decoder
          draining_ = true;
        } else {
          if (packet_->stream_index == stream_) avcodec_send_packet(dec_.get(), packet_.get());
          av_packet_unref(packet_.get());
        }
      }
      bool got = false;
      while (avcodec_receive_frame(dec_.get(), frame_.get()) == 0) {
        convert(frame_.get());
        av_frame_unref(frame_.get());
        got = true;
      }
      if (!pending_.empty()) return true;
      if (draining_ && !got) {
        if (!eof_) {  // drain what swr still holds (resampler delay)
          eof_ = true;
          convert(nullptr);
          if (!pending_.empty()) return true;
        }
        return false;
      }
    }
  }

  void convert(const AVFrame* in) {
    const auto ch = static_cast<std::size_t>(target_.channels);
    int in_samples = in ? in->nb_samples : 0;
    int max_out = swr_get_out_samples(swr_.get(), in_samples);
    if (max_out <= 0) return;
    std::vector<float> tmp(static_cast<std::size_t>(max_out) * ch);
    uint8_t* out_planes[1] = {reinterpret_cast<uint8_t*>(tmp.data())};
    int n = swr_convert(swr_.get(), out_planes, max_out, in ? in->extended_data : nullptr, in_samples);
    if (n <= 0) return;
    std::size_t begin = 0;
    auto count = static_cast<std::size_t>(n);

    // Frame-accurate seeking: drop output frames that precede the requested position.
    if (skip_until_ > 0) {
      if (next_frame_ < 0) {
        int64_t pts = in ? (in->best_effort_timestamp != AV_NOPTS_VALUE ? in->best_effort_timestamp : in->pts) : 0;
        next_frame_ = pts == AV_NOPTS_VALUE
                          ? skip_until_  // unknown timing: assume we are already there
                          : av_rescale_q(pts, fmt_->streams[stream_]->time_base, AVRational{1, target_.sample_rate});
      }
      auto drop = static_cast<std::size_t>(std::clamp<int64_t>(skip_until_ - next_frame_, 0, n));
      begin = drop;
      next_frame_ += n;
      if (next_frame_ >= skip_until_) skip_until_ = 0;
    }
    pending_.insert(pending_.end(), tmp.begin() + static_cast<std::ptrdiff_t>(begin * ch),
                    tmp.begin() + static_cast<std::ptrdiff_t>(count * ch));
  }

  AudioFormat target_{};
  std::unique_ptr<AVFormatContext, FormatCtxDeleter> fmt_;
  std::unique_ptr<AVCodecContext, CodecCtxDeleter> dec_;
  std::unique_ptr<SwrContext, SwrDeleter> swr_;
  std::unique_ptr<AVPacket, PacketDeleter> packet_;
  std::unique_ptr<AVFrame, FrameDeleter> frame_;
  int stream_ = -1;
  std::chrono::milliseconds duration_{0};
  std::vector<float> pending_;
  std::size_t pending_pos_ = 0;
  bool draining_ = false;
  bool eof_ = false;
  int64_t skip_until_ = 0;   // target-rate frame index to resume output from after a seek
  int64_t next_frame_ = -1;  // target-rate index of the next frame convert() will see
};

}  // namespace

Result<std::unique_ptr<IDecoder>> open_ffmpeg_decoder(const std::filesystem::path& file, AudioFormat target) {
  if (target.sample_rate <= 0 || target.channels <= 0)
    return std::unexpected(Error{ErrorCode::InvalidArgument, "invalid target format"});
  static const bool quiet = (av_log_set_level(AV_LOG_ERROR), true);  // demuxer chatter about odd timestamps
  (void)quiet;
  auto d = std::make_unique<FFmpegDecoder>();
  BH_TRY(d->open(file, target));
  return d;
}

}  // namespace bh::playback
