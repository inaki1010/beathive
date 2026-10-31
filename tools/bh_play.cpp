// Developer tool: gapless command-line player built on the playback engine and controller.
//   bh_play [--null] [--device ID] files...        play files in order
//   bh_play --list-devices
// Keys (type then Enter):  p pause/resume   n next   b previous   s <sec> seek   v <0-100> volume
//                          r repeat all     z shuffle   q quit
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "core/playback/controller.hpp"
#include "core/tags/tag_io.hpp"

using namespace bh;
using namespace bh::playback;
using namespace std::chrono_literals;

int main(int argc, char** argv) {
  bool null_backend = false, list_devices = false;
  std::string device;
  std::vector<std::filesystem::path> files;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--null") null_backend = true;
    else if (a == "--list-devices") list_devices = true;
    else if (a == "--device" && i + 1 < argc) device = argv[++i];
    else files.emplace_back(a);
  }

  auto output = make_miniaudio_output(null_backend);
  if (list_devices) {
    for (auto& d : output->devices())
      std::printf("%s%s\n  id: %s\n", d.name.c_str(), d.is_default ? "  [default]" : "", d.id.c_str());
    return 0;
  }
  if (files.empty()) {
    std::fprintf(stderr, "usage: %s [--null] [--device ID] files...\n", argv[0]);
    return 2;
  }

  Engine engine(std::move(output));
  if (!device.empty()) engine.set_output_device(device);
  std::atomic<bool> device_error{false};

  PlaybackController ctl(engine, [&](TrackId id) -> std::optional<std::filesystem::path> {
    auto idx = static_cast<std::size_t>(id.value - 1);
    return idx < files.size() ? std::optional(files[idx]) : std::nullopt;
  });
  ctl.set_on_skipped([&](TrackId id, const Error& e) {
    std::fprintf(stderr, "\nskipped %s: %s\n", files[static_cast<std::size_t>(id.value - 1)].string().c_str(),
                 e.message.c_str());
    if (e.code == ErrorCode::Device) device_error = true;
  });

  std::vector<TrackId> ids;
  for (std::size_t i = 0; i < files.size(); ++i) ids.emplace_back(static_cast<std::int64_t>(i + 1));
  ctl.play_tracks(ids);

  std::atomic<bool> quit{false};
  std::thread input([&] {
    std::string line;
    bool repeat = false, shuffle = false;
    while (!quit && std::getline(std::cin, line)) {
      char c = line.empty() ? ' ' : line[0];
      double arg = line.size() > 2 ? std::atof(line.c_str() + 2) : 0;
      switch (c) {
        case 'p': ctl.toggle_pause(); break;
        case 'n': ctl.next(); break;
        case 'b': ctl.previous(); break;
        case 's': engine.seek(std::chrono::milliseconds(static_cast<long long>(arg * 1000))); break;
        case 'v': engine.set_volume(static_cast<float>(arg / 100.0)); break;
        case 'r': repeat = !repeat; ctl.set_repeat(repeat ? RepeatMode::All : RepeatMode::Off); break;
        case 'z': shuffle = !shuffle; ctl.set_shuffle(shuffle); break;
        case 'q': quit = true; break;
        default: break;
      }
    }
  });  // stdin closing (e.g. run from a script) just stops key handling; playback continues
  input.detach();

  while (!quit && !device_error) {
    std::this_thread::sleep_for(200ms);
    auto st = engine.state();
    if (st == PlayState::Stopped) break;
    auto now = ctl.now_playing();
    auto pos = engine.position();
    if (now && pos) {
      auto name = files[static_cast<std::size_t>(now->track.value - 1)].filename().string();
      std::printf("\r%s %-40.40s %5.1f / %5.1f s   ", st == PlayState::Paused ? "||" : "> ", name.c_str(),
                  static_cast<double>(pos->position.count()) / 1000, static_cast<double>(pos->duration.count()) / 1000);
      std::fflush(stdout);
    }
  }
  std::printf("\n");
  return device_error ? 1 : 0;
}
