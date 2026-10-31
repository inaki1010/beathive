#include <catch2/catch_test_macros.hpp>
#include <unordered_set>

#include "core/model/ids.hpp"

using namespace bh;

TEST_CASE("Id defaults to none and is distinct per tag") {
  TrackId none;
  CHECK_FALSE(none);
  CHECK(TrackId{5});
  CHECK(TrackId{1} < TrackId{2});
  static_assert(!std::is_convertible_v<TrackId, PlaylistId>);
  std::unordered_set<TrackId> s{TrackId{1}, TrackId{1}, TrackId{2}};
  CHECK(s.size() == 2);
}
