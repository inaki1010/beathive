#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include <miniaudio.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <cstdio>
#include <cstring>

#include "core/playback/audio_output.hpp"

namespace bh::playback {

namespace {

std::string id_to_hex(const ma_device_id& id) {
  std::string out;
  char buf[3];
  auto* bytes = reinterpret_cast<const unsigned char*>(&id);
  std::size_t used = sizeof id;
  while (used > 0 && bytes[used - 1] == 0) --used;  // ids are zero-padded unions; keep them short
  for (std::size_t i = 0; i < used; ++i) {
    std::snprintf(buf, sizeof buf, "%02x", bytes[i]);
    out += buf;
  }
  return out;
}

bool hex_to_id(const std::string& hex, ma_device_id& id) {
  if (hex.size() % 2 != 0 || hex.size() > sizeof id * 2) return false;
  std::memset(&id, 0, sizeof id);
  auto* bytes = reinterpret_cast<unsigned char*>(&id);
  for (std::size_t i = 0; i < hex.size() / 2; ++i) {
    unsigned v = 0;
    if (std::sscanf(hex.c_str() + i * 2, "%2x", &v) != 1) return false;
    bytes[i] = static_cast<unsigned char>(v);
  }
  return true;
}

class MiniaudioOutput final : public IAudioOutput {
 public:
  explicit MiniaudioOutput(bool null_backend) : null_backend_(null_backend) {}
  ~MiniaudioOutput() override { close(); uninit_context(); }

  Result<AudioFormat> open(const std::string& device_id, RenderFn render) override {
    close();
    if (auto r = init_context(); !r) return std::unexpected(r.error());
    render_ = std::move(render);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = 0;  // the device's native rate; the engine resamples to it
    cfg.performanceProfile = ma_performance_profile_conservative;
    cfg.dataCallback = &MiniaudioOutput::on_data;
    cfg.pUserData = this;

    ma_device_id chosen{};
    if (!device_id.empty()) {
      if (!hex_to_id(device_id, chosen)) return std::unexpected(Error{ErrorCode::Device, "invalid device id"});
      cfg.playback.pDeviceID = &chosen;
    }
    if (ma_device_init(&ctx_, &cfg, &device_) != MA_SUCCESS)
      return std::unexpected(Error{ErrorCode::Device, "could not open audio device"});
    open_ = true;
    return AudioFormat{static_cast<int>(device_.sampleRate), static_cast<int>(device_.playback.channels)};
  }

  Result<void> start() override {
    if (!open_) return std::unexpected(Error{ErrorCode::Device, "device not open"});
    if (ma_device_start(&device_) != MA_SUCCESS) return std::unexpected(Error{ErrorCode::Device, "could not start device"});
    return {};
  }

  void stop() override { if (open_) ma_device_stop(&device_); }

  void close() override {
    if (!open_) return;
    ma_device_uninit(&device_);  // stops the device and waits for the callback to finish
    open_ = false;
  }

  std::vector<AudioDeviceInfo> devices() override {
    std::vector<AudioDeviceInfo> out;
    if (!init_context()) return out;
    ma_device_info* infos = nullptr;
    ma_uint32 count = 0;
    if (ma_context_get_devices(&ctx_, &infos, &count, nullptr, nullptr) != MA_SUCCESS) return out;
    for (ma_uint32 i = 0; i < count; ++i)
      out.push_back({id_to_hex(infos[i].id), infos[i].name, infos[i].isDefault != 0});
    return out;
  }

 private:
  static void on_data(ma_device* d, void* out, const void*, ma_uint32 frames) {
    auto* self = static_cast<MiniaudioOutput*>(d->pUserData);
    self->render_(static_cast<float*>(out), frames);
  }

  Result<void> init_context() {
    if (ctx_ok_) return {};
    ma_backend null_only[] = {ma_backend_null};
    ma_result rc = null_backend_ ? ma_context_init(null_only, 1, nullptr, &ctx_)
                                 : ma_context_init(nullptr, 0, nullptr, &ctx_);
    if (rc != MA_SUCCESS) return std::unexpected(Error{ErrorCode::Device, "no audio backend available"});
    ctx_ok_ = true;
    return {};
  }
  void uninit_context() {
    if (ctx_ok_) ma_context_uninit(&ctx_);
    ctx_ok_ = false;
  }

  bool null_backend_;
  ma_context ctx_{};
  ma_device device_{};
  bool ctx_ok_ = false;
  bool open_ = false;
  RenderFn render_;
};

}  // namespace

std::unique_ptr<IAudioOutput> make_miniaudio_output(bool null_backend) {
  return std::make_unique<MiniaudioOutput>(null_backend);
}

}  // namespace bh::playback
