#pragma once
#include <Qt>

namespace bh::ui {

// Extra data roles the list models provide for TrackDelegate.
enum ItemRole {
  PlayingRole = Qt::UserRole + 10,  // bool: this row is the audible track
  KindRole = Qt::UserRole + 11,     // CellKind
  RatingRole = Qt::UserRole + 12,   // int 0..5
  SubtitleRole = Qt::UserRole + 13, // QString: a dimmer second line under a Primary cell
};

// How TrackDelegate draws a cell.
enum class CellKind { Primary, Secondary, Numeric, Marker, Rating };

constexpr int kRowHeight = 30;
constexpr int kTwoLineRowHeight = 46;

}  // namespace bh::ui
