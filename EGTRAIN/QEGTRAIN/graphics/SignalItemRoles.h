#pragma once

// The item data roles that the network canvas sets on the items of a signal. The window and the
// scripted runs in tests/e2e read them.

/// True on the head, the post and the base of a signal.
constexpr int kSignalDecorationRole = 0;
/// The index of the track line of the signal in a run; -1 in the preview.
constexpr int kSignalTrackRole = 1;
/// The item shows while the signal layer is shown.
constexpr int kSignalBaseVisibleRole = 2;
/// On a head: its position in scene coordinates, where the window puts it back.
constexpr int kSignalAnchorRole = 3;
