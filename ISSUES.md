# Issues

## Tearing during heavy sloshing

**Seen:** when the gauge is shaken hard, the image occasionally shows a horizontal "dislocation": a line with the two sides out of step. Subtle, and only with lots of movement.

**Cause:** the CO5300 refreshes the panel from its memory at about 60 Hz (about 16 ms per top-to-bottom scan) while the firmware writes each frame over about 22 ms, with nothing tying the two together. When a scan passes the row being written, the panel shows the new frame above that row and the old one below it.

**Fix:** the panel's TE (tearing effect) output is wired to GPIO13 (board schematic: `LCD_TE`). Arduino_GFX leaves it off: `TEARON` (0x35) is commented out of `co5300_init_operations`. Send `TEARON`, count TE edges in an interrupt, and have the display task on core 0 start each frame's first strip at a fixed point in the refresh. Writes have to start a little behind the scan: the narrow top and bottom strips go out faster than the panel scans, so starting level with it would let the write catch up near the top. Setting the tear scan line (0x44) is one way to place that point. Use a timeout so a missing TE signal can't freeze the gauge.

**Unknowns to measure first:** the actual refresh rate, which TE edge marks the start of a scan, and how far behind the scan to start writing.

**Costs:**
- Frames land on whole refreshes, so a frame that overruns two refreshes waits for a third: a 50 ms hitch instead of a slightly late frame. A full tank is already close to the 33 ms budget.
- Waiting for the refresh idles the display and fills the strip queue, so some of the overlap that reaches 30 fps is lost.
- The simulation steps a fixed 1/30 s per frame, so the liquid's speed follows the panel's real refresh rate (58 Hz would run about 3% slow). A tuner target that doesn't divide the refresh rate rounds down.
- The cost model in `PerformanceModel.cpp` would need to round frame times up to whole refreshes.

**Before fixing:** free up frame time first, mainly by making rendering faster (core 1 spends about 18.8 ms of each frame on it), so the gauge sits well inside two refreshes even with a full tank.
