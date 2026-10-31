#include <catch2/catch_test_macros.hpp>
#include <set>

#include "core/model/queue.hpp"

using namespace bh;

namespace {
std::vector<TrackId> ids(int n) {
  std::vector<TrackId> v;
  for (int i = 1; i <= n; ++i) v.emplace_back(i);
  return v;
}
}  // namespace

TEST_CASE("sequential playback and end of queue") {
  PlayQueue q(1);
  q.replace(ids(3));
  CHECK(q.current()->track == TrackId{1});
  CHECK(q.next(true)->track == TrackId{2});
  CHECK(q.next(true)->track == TrackId{3});
  CHECK_FALSE(q.next(true));
  CHECK_FALSE(q.current());
}

TEST_CASE("repeat modes") {
  PlayQueue q(1);
  q.replace(ids(2), 1);
  q.set_repeat(RepeatMode::One);
  CHECK(q.next(true)->track == TrackId{2});  // auto-advance repeats
  CHECK(q.next(false)->track == TrackId{1}); // user next wraps
  q.set_repeat(RepeatMode::All);
  CHECK(q.next(true)->track == TrackId{2});
  CHECK(q.next(true)->track == TrackId{1});
}

TEST_CASE("previous walks history") {
  PlayQueue q(1);
  q.replace(ids(3));
  q.next(true);
  q.next(true);
  CHECK(q.previous()->track == TrackId{2});
  CHECK(q.previous()->track == TrackId{1});
  CHECK(q.previous()->track == TrackId{1});  // no history: stays
}

TEST_CASE("play_next, append and duplicates") {
  PlayQueue q(1);
  q.replace(ids(2));
  q.play_next({TrackId{9}});
  q.append({TrackId{1}});
  std::vector<std::int64_t> got;
  for (auto& e : q.entries()) got.push_back(e.track.value);
  CHECK(got == std::vector<std::int64_t>{1, 9, 2, 1});
  CHECK(q.entries()[0].entry_id != q.entries()[3].entry_id);
}

TEST_CASE("remove and move keep current pointing at the right entry") {
  PlayQueue q(1);
  q.replace(ids(4), 2);  // current = track 3
  q.remove(q.entries()[0].entry_id);
  CHECK(q.current()->track == TrackId{3});
  q.move(0, 2);  // [2,3,4] -> [3,4,2]
  CHECK(q.current()->track == TrackId{3});
  q.remove(q.current()->entry_id);  // falls onto the following entry
  CHECK(q.current()->track == TrackId{4});
}

TEST_CASE("shuffle plays every track once per round") {
  PlayQueue q(42);
  q.replace(ids(6));
  q.set_shuffle(true);
  std::set<std::int64_t> seen{q.current()->track.value};
  for (int i = 0; i < 5; ++i) seen.insert(q.next(true)->track.value);
  CHECK(seen.size() == 6);
  CHECK_FALSE(q.next(true));  // round over, repeat off

  q.replace(ids(6));
  q.set_shuffle(true);
  q.set_repeat(RepeatMode::All);
  for (int i = 0; i < 20; ++i) CHECK(q.next(true));
}

TEST_CASE("peek_next previews without moving") {
  PlayQueue q(1);
  q.replace(ids(3));
  CHECK(q.peek_next()->track == TrackId{2});
  CHECK(q.peek_next()->track == TrackId{2});
  CHECK(q.current()->track == TrackId{1});
  CHECK(q.next(true)->track == TrackId{2});
  q.next(true);
  CHECK_FALSE(q.peek_next());  // end of queue, repeat off

  q.set_repeat(RepeatMode::All);
  CHECK(q.peek_next()->track == TrackId{1});
  q.set_repeat(RepeatMode::One);
  CHECK(q.peek_next()->track == TrackId{3});
}

TEST_CASE("a shuffled peek is a promise: next() delivers exactly that entry") {
  for (std::uint32_t seed = 1; seed <= 20; ++seed) {
    PlayQueue q(seed);
    q.replace(ids(8));
    q.set_shuffle(true);
    for (int i = 0; i < 7; ++i) {
      auto promised = q.peek_next();
      REQUIRE(promised);
      CHECK(q.peek_next()->entry_id == promised->entry_id);  // stable
      CHECK(q.next(true)->entry_id == promised->entry_id);
    }
    CHECK_FALSE(q.peek_next());  // round over
  }
}

TEST_CASE("queue edits change what peek_next promises") {
  PlayQueue q(1);
  q.replace(ids(3));
  CHECK(q.peek_next()->track == TrackId{2});
  q.play_next({TrackId{9}});
  CHECK(q.peek_next()->track == TrackId{9});
  q.remove(q.peek_next()->entry_id);
  CHECK(q.peek_next()->track == TrackId{2});

  q.set_shuffle(true);
  auto p1 = q.peek_next();
  q.set_shuffle(false);
  CHECK(q.peek_next()->track == TrackId{2});
  (void)p1;
}
