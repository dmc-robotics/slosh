# Issues

## Tearing during heavy sloshing

**Seen:** when the gauge is shaken hard, the image occasionally shows a horizontal "dislocation": a line with the two sides out of step. Subtle, and only with lots of movement. Likely more visible at a full tank, where more of the screen changes between frames.

**Cause:** the CO5300 refreshes the panel from its memory at 60 Hz (about 16.6 ms per top-to-bottom scan) while the firmware writes each frame over about 21.5 ms, with nothing tying the two together. When a scan passes the row being written, the panel shows the new frame above that row and the old one below it.

**Measured on 2026-10-08** with a diagnostic build (since removed):
- The panel's TE (tearing effect) output is wired to GPIO13 (`LCD_TE` on the board schematic). Arduino_GFX leaves it off: `TEARON` (0x35) is commented out of `co5300_init_operations`. Sending `0x35, 0x00` (V-blank only) with `displayBus->writeC8D8` inside `beginWrite`/`endWrite` turns it on.
- The refresh rate is 60.1 Hz, so two refreshes per frame gives 30 fps and the liquid's speed stays right.
- TE goes high for 0.62 ms each refresh: the vertical blanking interval. The falling edge marks the start of a scan.

**Fix:** count TE falling edges in an interrupt, and have the display task on core 0 start each frame's first strip a fixed time after a falling edge, every second refresh. Writes have to start behind the scan: the narrow top and bottom strips go out faster than the panel scans, so starting level with it would let the write catch up near the top. The frame's writes run 5.9 ms ahead of or behind a steady scan at most, leaving about 10 ms of slack in where to start them, so the exact offset isn't critical. Use a timeout so a missing TE signal can't freeze the gauge, and pace frames by TE instead of the `FRAME_PERIOD` timer, or the two clocks will drift against each other.

**Feasibility:** a frame-by-frame model of the strip pipeline (fitted to the board: it predicts 29.4 fps against 28.4 measured) says that with TE sync, rendering is paced by the display write, so the simulation has to fit in what's left of the two refreshes:

| strip buffers | simulation limit | full tank (9.1 ms) |
|---|---|---|
| 6 | 13.2 ms | about 4 ms spare |
| 8 | 14.7 ms | about 5.5 ms spare |
| 10 (now) | 16.3 ms | about 7 ms spare, likely ~6 ms on the board |

So it's feasible with the current 10 buffers, with a few milliseconds of margin at a full tank. Speeding up rendering doesn't help with TE sync; only a faster simulation or more buffers do. There's no RAM left for more buffers without PSRAM.

**Costs:**
- Frames land on whole refreshes, so a frame that overruns two refreshes waits for a third: a 50 ms hitch instead of a slightly late frame. Heavier tuner settings than the current ones would hitch at a full tank.
- The cost model in `PerformanceModel.cpp` would need to round frame times up to whole refreshes and warn about the simulation limit above.

## Frame-time headroom at a full tank

**Status:** a full tank (720 particles) holds 30 fps, and the model puts it at about 39 fps unthrottled. `BENCH_FILL_LEVEL` in `src/hardware/PowerBusConfig.h` is 1.0, so the bench shows this worst case.

**Measured on 2026-10-08** (2026-10-05 tuned settings, full tank, ms per frame):

| | `-Os`, 6 buffers | `-O2`, 6 buffers | `-O2`, 10 buffers | fewer divisions | fast inverse square root (now) |
|---|---|---|---|---|---|
| fps | ~28 | 28.4 | 30.0 | 30.3 | 30.2 |
| simulation (core 1) | 14.4 | 13.9 | 12.9–14.5 | 10.45 | 8.9–9.2 |
| render (core 1) | 19.2 | 9.7 (1.4 of it preparing the density field) | 9.7 | 9.8 | 9.7 |
| transfer (core 0, alongside) | 21.8 | 21.7 | 21.5 | 21.5 | 21.4 |
| core 1 waiting on the display | 0.9 | 10.3 | 7.1 | 7.1 | 7.2 |

The last two columns aren't strictly comparable: the board lay nearly flat for "fewer divisions" and was being moved for the last one, which packs the liquid tighter and gives separation about 45% more overlaps to resolve (counted on the host).

**What got it there:**
- `build_opt.h` compiles the firmware with `-O2`. The core's default `-Os` turned the render loop into a register spill and two branches per pixel. `-O2` makes it a 6-instruction hardware loop, which halved rendering. The simulation only gained about 4%.
- 10 strip buffers instead of 6, so the display keeps sending while the next simulation step runs. This cost 60 KB, so the tuner's heap budget dropped from 200 KB to 170 KB; `free_heap` reads about 148 KB.
- Fewer float divisions in the simulation. Each one is a call to `__divsf3`, roughly 60 cycles even with the FPU's divide-assist instructions. Cell lookups multiply by a precomputed inverse cell size, the pressure solve reads `1 / open sides` from a table, and the grid transfer and wall collisions multiply by one reciprocal. That took 3.8 ms off.
- A fast inverse square root (a bit-level estimate and two Newton steps, within 5e-6) for separating overlapping particles and for wall collisions, in place of a `sqrtf` call and a division each. At least 1.4 ms off. The first try gained nothing: the ESP32 core compiles with `-fno-builtin-memcpy`, so the `memcpy`s that reinterpret the float's bits became two more library calls; `__builtin_memcpy` fixed it.

**Where the simulation time goes now** (the firmware prints these phases every second): separate about 4.2 ms, from grid 1.63, to grid 1.51, pressure 1.12, density 0.41, walls 0.12, integrate 0.10. Separation is still the largest: a full tank is tightly packed, and each particle checks 15–23 neighbors per step and overlaps 4.5–6.5 of them. Handling each overlapping pair once instead of twice would roughly halve that work, but changes the behavior slightly, so the look would need rechecking in the tuner.

Lighter tuner settings, such as a lower `fullChargeFill`, a larger `particleRadiusRatio` or fewer pressure iterations, also help, at some cost to the look.
