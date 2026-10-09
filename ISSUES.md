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

**Before fixing:** free up frame time first (see *A full tank runs below 30 fps* below), so the gauge sits well inside two refreshes even with a full tank.

## A full tank runs below 30 fps

**Seen:** with a full tank (720 particles, bench level 1.0) the gauge runs at 27.5–28.2 fps against the 30 fps target. The simulation still steps 1/30 s per frame, so the liquid moves about 7% slower than real time. At 60% fill (432 particles) it holds 30 fps.

**Measured on 2026-10-08** (2026-10-05 tuned settings, ms per frame):

| | 60% | 100% |
|---|---|---|
| fps | 30.0 | ~28 |
| simulation (core 1) | 8.8 | 14.4 |
| render (core 1) | 18.8 | 19.2 |
| transfer (core 0, alongside) | 21.7 | 21.8 |
| core 1 waiting on the display | 1.0 | 0.9 |

Core 1 is the bottleneck: about 34.5 ms of the 35.8 ms frame at a full tank. The other ~1.3 ms is untimed per-frame work (the accelerometer read over I2C, the fuel sensor update, loop overhead). The display isn't the limit now that the corners are skipped.

**The tuner is optimistic:** it estimates 31 fps for a full tank. Its simulation (13.8 ms) and render (18.5 ms) estimates are close, but `PerformanceModel.cpp` leaves out the untimed overhead and core 1's wait on the display, about 2 ms per frame together. Add a measured per-frame overhead so the budget warning matches the board.

**Most promising fix: faster rendering.** Rendering is the largest cost, and it barely changes with fill. The inner loop of `FluidRenderer::renderRows` compiles to 7 instructions per pixel, yet costs about 27 cycles per pixel (19 ms for ~170k lit pixels at 240 MHz), so something other than the arithmetic dominates. Suspects to check:
- memory contention with the display DMA reading the other strip buffers
- instruction-cache misses (the code runs from flash)
- the per-row work: clearing each row, the density row interpolation, and setting up about 17 segments per row

Time `prepare` separately from `renderRows` and try `IRAM_ATTR` on the hot loop to tell these apart. Saving 5–10 ms would give a full tank real headroom, and that headroom is also what the tearing fix needs.

**Other levers:** lighter tuner settings, such as a lower `fullChargeFill`, a larger `particleRadiusRatio` (fewer particles) or fewer pressure iterations, at some cost to the look. The simulation also costs more per particle than first guessed: 8.8 ms for 432 particles is about 20 µs each. It hasn't been profiled.

**Note:** `BENCH_FILL_LEVEL` in `src/hardware/PowerBusConfig.h` is 1.0, so the bench shows this worst case.
