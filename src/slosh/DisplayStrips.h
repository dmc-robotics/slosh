#pragma once

// The firmware streams each frame as strips of rows. Core 1 renders them while core 0 sends them to
// the display, and strips still queued at the end of a frame keep the display busy while core 1
// runs the next simulation step.
constexpr int STRIP_HEIGHT = 16;  // rows; the CO5300 wants even windows
constexpr int STRIP_COUNT = 10;  // 150 KB; fewer leave the display idle while a full tank simulates

// Columns [first, end); empty when first == end.
struct ColumnSpan {
  int first;
  int end;
};

// The columns of a row that fall on the round display.
ColumnSpan litColumns(int row, int outputSize);
// The columns a strip sends: every lit pixel in its rows, widened to start on an even column and
// span an even number of them. The black corners outside the circle are never sent.
ColumnSpan stripColumns(int firstRow, int rowCount, int outputSize);
