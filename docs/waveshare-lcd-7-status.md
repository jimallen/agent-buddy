# Waveshare ESP32-S3-Touch-LCD-7 (800×480 RGB): status and recommendations

A working record of the 7-inch board port: what exists, what has been measured, what has been
tried and disproved, and what I recommend next. Written after a long debugging session in which
several confident diagnoses turned out to be wrong, so the emphasis is on **evidence** and on
marking clearly what is still unknown.

---

## 1. Where we are

| Area | State |
| --- | --- |
| Board bring-up (power, backlight, reset via CH422G) | Working |
| 800×480 RGB panel (ST7262, 16 data lines) | Working; timing matches Waveshare's reference verbatim |
| Colour / byte order | Correct (`kPanelSwappedBytes = true`) |
| Single-character view (centred, 412×466 content) | Working, steady |
| Fleet view (one column per rig) | Working; repaints only on change |
| Rig labels + per-rig harness icon | Working |
| Subagent badge strip | Working (added late; was previously never drawn on this board) |
| Usage / token lines ("Tokens: 1.2M") | Working (added late; was previously never drawn at all) |
| Wi-Fi | Deliberately off unless configured, or started with the `n` command |
| Touch (GT911 @ 0x5D, reset via CH422G EXIO1) | Configured, **untested** — nothing exercises it |
| Continuous character animation | **Off by default** (`fleetStepMicros = 0`). Available via the `f` command, but still shows artefacts |
| Steadiness under continuous writes | **The open problem** (section 6) |

Firmware builds clean for all three boards (`waveshare-lcd-70`, `waveshare-amoled-175`,
`waveshare-lcd-154`), and the host test suites pass (`tools/test_rig_fleet.sh`,
`tools/test_device_inputs.sh` — 16 checks, Python `unittest`, daemon `npm test` 324 pass / 0 fail).

Everything is committed and pushed to the fork (`jimallen/agent-buddy`), 23 commits ahead of
upstream. Upstream push is deliberately disabled; this is a permanent fork.

---

## 2. Hardware and verified configuration

Values below were taken from Waveshare's own reference configuration and confirmed on the device,
not inferred.

| Setting | Value | Source |
| --- | --- | --- |
| Panel | 800×480, ST7262, RGB565 over 16 data lines | Waveshare demo |
| PCLK | 16 MHz | reference |
| HSYNC / pulse / front / back | polarity 1, pulse 4, front 8, back 8 | reference |
| VSYNC / pulse / front / back | polarity 1, pulse 4, front 8, back 8 | reference |
| PCLK active negative | 1 | reference |
| Bounce buffer | 800 × 10 px | reference (`EXAMPLE_LCD_RGB_BUFFER_SIZE`) |
| Framebuffers | 1 (`num_fbs = 1`) | current choice, see §6 |
| Framebuffer location | PSRAM (`fb_in_psram = true`) | required — 800×480×2 = 768 KB |
| PSRAM / SRAM trans align | 64 / 4 | vendor library values |
| Panel reset | through CH422G (no GPIO) | board wiring |
| Backlight | CH422G enable line, on/off only | board wiring |

**Effective refresh rate is ~39 Hz**, not 60: `16 MHz / ((800+8+8+4) × (480+8+8+4)) ≈ 39`. This
matters — 39 Hz is low enough that flicker is perceptible in peripheral vision.

Ports: native USB (`/dev/cu.usbmodem211401`, Espressif `0x303A`) reaches the daemon; the other
USB-C socket is a CH343P UART for flashing and logs.

---

## 3. Architecture

The RGB board does not use the SPI "push a frame" model. LCD_CAM scans a framebuffer in PSRAM
continuously, so the display layer composes into memory rather than transmitting.

| File | Responsibility |
| --- | --- |
| `src/RgbPanel.h` | Creates the RGB panel ourselves via `esp_lcd_new_rgb_panel` (replacing Arduino_GFX's `Arduino_ESP32RGBPanel`), because that class hardcodes settings we need to own. Exposes `framebuffer(i)`. |
| `src/RgbScanout.h` | Composites a content frame into the panel framebuffer, with optional rotation, byte-order conversion (two pixels per `BSWAP`) and banded pacing (`setPacing`). |
| `src/RgbDisplay.h` | `Arduino_Canvas` subclass with the same surface as the SPI boards. `flushFrame()` for the single-character view, `presentRegion()` for the fleet. |
| `src/RigFleet.{h,cpp}` | Host-testable packet parsing (`=label:state:agent`), column geometry, `blitRigColumn`/`blitRigColumnNative`. |
| `src/AgentBadges.{h,cpp}` | Icon masks, active-agent list, usage lines. |
| `src/CharacterEffects.cpp` | Badge/overlay drawing with anti-aliased discs. |
| `AgentCompanion.ino` | `renderFleetOnPanel()`, the fleet/presenter split, diagnostics. |

### Fleet view layout (current)

```text
┌───────────────────────────────────────────────┐
│ chrome band, 40 px: badges (left) + usage (right), over black │
├───────────────────────────────────────────────┤
│ character art, one column per rig (max 4)      │
│                                                │
├───────────────────────────────────────────────┤
│ label band, 26 px: rig name + harness icon    │
└───────────────────────────────────────────────┘
```

The chrome band exists for a specific reason: overlays over black can be cleared and redrawn
without repainting a character column (see §5).

### When does the panel get written?

`renderFleetOnPanel()` repaints only when something actually changed:

| Trigger | Region written |
| --- | --- |
| Fleet packet layout change (rigs added/removed/renamed) | whole panel |
| A rig's state changed (working/idle/attention) | that column, below the chrome band |
| Badges or usage lines changed | chrome band only (40 rows) |
| Animation enabled (`f`) | one column per tick |
| Nothing changed | **nothing** — the panel is not touched at all |

That last row is the property that makes the picture steady. It is also the single most important
design constraint discovered (§6).

---

## 4. The central finding: the panel and the CPU share one PSRAM bus

This is the root of nearly every problem in this port, and it is worth stating precisely.

The panel is scanned **out of PSRAM**, and it needs roughly:

```text
800 × 480 × 2 bytes × 39 Hz ≈ 30 MB/s   (46 MB/s at 60 Hz)
```

Nothing about that is negotiable — if the scan is starved, the glass misbehaves. Meanwhile the
CPU decodes character frames, composites, and copies framebuffers **also in PSRAM**.

Measured consequence: a whole-frame present that copies ~1.5 MB ran at about **7 MB/s**, and took
**242 ms**. During those 242 ms the panel was starved, and the user saw both a freeze *and*
tearing — which for several rounds were misdiagnosed as two separate faults. They are one event.

The rule that follows, and the design that now implements it:

> **Write as few bytes as possible, as rarely as possible, and never in one long burst.**

---

## 5. What we tried — hypotheses, experiments, outcomes

Recorded because several of these were plausible, were acted on, and were wrong. The "lesson"
column is what should stop the next person repeating them.

| # | Hypothesis | Experiment / change | Result | Lesson |
| --- | --- | --- | --- | --- |
| 1 | Wrong RGB timing or sync polarity causes jumping | Compared every value against Waveshare's reference; matched verbatim | No change | Timing was already right — do not tune what already matches the reference |
| 2 | Wi-Fi stack steals PSRAM bandwidth | Gated Wi-Fi to start only when a network is configured (`n` to start manually) | Improved; device had never had credentials | A large idle subsystem on the same bus is a real suspect |
| 3 | Per-frame blanking causes flashing | Removed the whole-panel `memset` before redraw; repaint columns edge-to-edge | Fixed a visible flash | Clearing then drawing is worse than overwriting |
| 4 | Stale pixels cause "bouncing" | Repaint each column from the full source frame (which carries its own black surround) | Fixed the bounce | Partial repaints leave ghosts that read as motion |
| 5 | Per-pixel division in the scale is the cost | Integer accumulation (Bresenham) instead of divide per pixel | Helped | ~360k divisions per column is real |
| 6 | Uneven frame delivery reads as jitter | One column per tick instead of two | Better | Even cadence matters as much as average rate |
| 7 | Byte swap is the cost | Two pixels per 32-bit `BSWAP` | Frame compose 40 ms → much less | Sequential swap beats swapping inside a scaled blit |
| 8 | Per-pixel swap inside the scaled blit | Swap whole frame once into a separate buffer, then blit plain copies | 57 ms → ~2 ms + fast blit | Never reorder bytes while scaling |
| 9 | **In-place swap corrupts frames** | Swapping the render target in place broke incremental patching and the effects' saved pixels | Colour garbage — a bug **I** introduced | The render target is stateful; never mutate it under the renderer |
| 10 | `dimRigPixel` byte order | Pre-swapping broke away-rig dimming; a host test caught it (`swapBytes(dimmed[0])`) | Fixed via a separate `blitRigColumnNative` | Do not redefine an existing flag's meaning; add an entry point and pin it with a test |
| 11 | Library defects: `num_fbs = 1`, `bb_invalidate_cache = 0` | Inspected Arduino_GFX's `Arduino_ESP32RGBPanel.cpp` | Real, but **the vendor library uses the same values** and is steady | Inspect the *vendor's* config before blaming the library |
| 12 | Double buffering is the answer | Created panel with `num_fbs = 2`, swapped via `esp_lcd_panel_draw_bitmap` (the call Waveshare's `switchFrameBufferTo()` makes) | Still glitchy; cost a 1.5 MB mirror copy per present | Double buffering is worthless if the copy itself starves the scan |
| 13 | Two writers alternate (presenter + fleet) | Instrumented both present paths and logged them | Only `fleet` ever presents | Measure the writers before theorising about them |
| 14 | Driver alternates buffers, needs both identical | Mirror every present into both framebuffers | Steady while static; **glitched as soon as tokens animated** | Mirroring writes the scanned buffer — worse than useless during updates |
| 15 | "Freeze" is a crash | Triggered `&`(agents) and `$`(usage) packets and watched the log | No FATAL, no watchdog, heap and stack flat, `render = 0.08 ms` | A "freeze" can simply mean *nothing asked for a repaint* |
| 16 | **Overlays were never drawn and never triggered repaints** | `rigBadgeStrip()` existed only in the push-display branch; usage lines were never drawn anywhere | Added drawing + repaint triggers | The bug was a missing call, not a hardware fault |
| 17 | Overlay repaint is too expensive | Chrome band on black + region presents with paced rows | **242 ms → 8.8 ms** | Restructure the layout so the cheap update is possible |

### Mistakes worth remembering

- **The "static" test was broken.** Setting the step to `0` made the due-check `start >= next`
  always true, so "frozen" actually repainted as fast as the loop could go. That single bug
  invalidated a whole bisection round and sent the investigation at the wrong layer for several
  iterations. A zero interval must mean *never*, and that is now explicit.
- **A blind loop is expensive.** Without a camera on the panel, every change needed a human to
  describe the symptom. Several rounds optimised numbers that could never have fixed the
  reported problem.
- **The vendor demo is the control.** It draws once and never writes again — and it is steady. It
  therefore proves the panel, cable and power are fine, and that *our writes* are the variable.

---

## 6. Current measured budget

| Operation | Cost | Frequency |
| --- | --- | --- |
| Overlay (badges/usage) update | **8.8 ms** | on change only |
| Rig state change (one column) | tens of ms (not isolated yet) | on change only |
| Layout change (whole panel) | ~200 ms+ | rare (rigs added/removed) |
| Idle tick | 0.1–0.4 ms | always |
| Fleet tick when animating | 30–190 ms | every tick — this is why animation is off |
| Frame decode (new pose) | ~27 ms | per column repaint |
| Frame decode (cached base) | ~10 ms | per column repaint |

Panel refresh ≈ 39 Hz. Loop otherwise runs ~26–30 fps.

---

## 7. Open problems and unknowns

1. **Animation still shows artefacts.** With `num_fbs = 1`, any sustained writing to the scanned
   buffer breaks the picture. The honest statement is: *on this board, with this core and this
   driver, we have not found a way to animate continuously and stay steady.*
2. **Column repaint cost is not isolated.** State changes write ~350 KB; whether that is visible
   is unknown. It should be measured the same way the overlay path was.
3. **Layout changes cost ~200 ms** — a rig coming online will hitch visibly.
4. **`bb_invalidate_cache` remains untested.** Enabling it might fix PSRAM/cache coherence for
   the bounce-buffer DMA. It is a one-line change in our own panel creation, and worth a single
   experiment.
5. **Refresh rate is 39 Hz.** Raising PCLK raises bandwidth pressure; the interaction with
   steadiness is unmeasured.
6. **Touch (GT911) is configured but untested.**
7. **No physical observation channel.** I cannot see the panel; every visual conclusion came from
   the user's descriptions. This is the largest methodological weakness in this work.

---

## 8. Recommendations

Ordered by value per unit of risk. Items 1–3 are cheap and should come first.

### 1. Settle the current build (minutes)

Have the user confirm whether the tokens/subagents case is now acceptable. The measurement says
8.8 ms per overlay update instead of 242 ms; if that still reads as a glitch, then overlay updates
are not the remaining problem and item 2 becomes mandatory before anything else.

### 2. Isolate the column-repaint cost (half an hour)

The only large remaining write on a normal path. Instrument `presentRegion` for the column case
exactly like the overlay case, log bytes/ms, and see whether a state change is visible. If it is,
the same treatment applies: dirty-rectangle the character itself (the pack already has a patch
model — `SpriteRenderer` patches only changed regions), which could cut a column repaint from
~350 KB to a few KB.

### 3. Try `bb_invalidate_cache = true` (one experiment)

One line in `RgbPanel.h`. If the bounce-buffer DMA is reading stale PSRAM behind the CPU's
write-back cache, this is the fix, and it costs nothing to test. Our own panel creation makes it
a single flag.

### 4. Decide what the fleet view is *for* (product decision)

This is the important one, and it is not a code question.

A status board that updates on change is **already correct and steady**, and it is what the
current default does: characters appear, states change, badges and token counts appear and
update, labels show the harness. That is the actual product.

Continuous character animation is a *different* product, and on this board it fights the panel for
memory bandwidth. Options:

- **Accept the status board** (recommended default). Keep `fleetStepMicros = 0`.
- **Animate only on demand** — e.g. an attention state triggers a short burst of frames, then
  settles. Short bursts are affordable; continuous motion is not.
- **Move animation to a board where it is cheap.** The SPI/QSPI boards already animate smoothly
  because the display is written by DMA, not scanned from contended PSRAM. If continuous
    animation matters, the 7-inch board is the wrong vehicle for it.

### 5. If steady animation is a hard requirement (significant work, uncertain payoff)

Two credible routes, both substantial:

- **Vendor a patched panel driver** that supports a genuine frame swap on this IDF (this core has
  no `esp_lcd_rgb_panel_show_frame`; `esp_lcd_panel_draw_bitmap` did not behave as a swap for us).
  High effort, tied to the core version.
- **Move this board's display layer to Waveshare's `ESP32_Display_Panel`.** Cleaner long-term and
  vendor-supported, but note their RGB path also uses `num_fbs = 1`, so it is **not** by itself a
  fix for animation; it would be worth it mainly for maintenance and for their panel handling.

Both should be gated on item 3 and on a reliable way to observe the panel.

### 6. Close the observation gap

Not being able to see the display cost many iterations. Cheapest fixes, in order:

- A phone photo or screenshot pasted into the conversation at each checkpoint.
- Point the desk camera at the panel for a few seconds and record video, then measure
  frame-to-frame displacement in pixels — that turns "glitchy" into a number.
- The `k` calibration pattern (border, colour bars, ruler every 40 rows) exists precisely to make
  a scanout fault legible: where the image wraps, which rows repeat, where the border breaks.

---

## 9. Reproduction and diagnostics

Build and flash:

```bash
export ARDUINO_CLI="$PWD/.deps/bin/arduino-cli" \
       ARDUINO_CONFIG="$PWD/.deps/arduino/arduino-cli.yaml" \
       WAVESHARE_DIR="$PWD/.deps/waveshare"
BOARD=waveshare-lcd-70 bash tools/arduino.sh build
BOARD=waveshare-lcd-70 bash tools/arduino.sh upload /dev/cu.usbmodem211401
```

Stop the daemon to free the port:

```bash
launchctl bootout gui/$(id -u)/com.danwahlin.esp32-agent-companion
# or send {"type":"stopService"} to "~/Library/Application Support/ESP32 Agent Companion/daemon.sock"
```

Device commands over serial: `k` calibration pattern (freezes the loop), `f` cycle fleet rate
(static/4/8/15/30), `n` start Wi-Fi, `s` capture the panel framebuffer, `=` fleet packet,
`&` agent badges, `$` usage lines, `%` agent icon.

Useful log lines: `PERF` (fps, render, max_render, free PSRAM), `SCANOUT` (compose time),
`STAGES` (motion/decode/inflate/predict), `PACING` (min/max frame gap), `MEM` (heap and stack).

---

## 10. Honest assessment

**What is solid:** the board runs our firmware, the fleet view is correct and legible, colour and
orientation are right, updates are incremental and cheap where it matters most (8.8 ms for the
overlay path), and the whole thing builds and tests clean on three boards.

**What is not:** continuous animation. The evidence is consistent and simple — the panel's scan
needs ~30 MB/s from PSRAM, and any sustained writing starves it. On a single-framebuffer RGB panel
with no usable swap call in this core, I do not currently know how to animate continuously without
producing the artefacts you have been seeing.

**What I would do in your position:** keep the status-board behaviour as the default, run items 2
and 3 above (both cheap), and treat continuous animation as a separate project with a camera
pointed at the panel. If animation is the point of the device, the SPI/QSPI board is currently the
better vehicle for it — and the fleet work, which is the real feature, is board-independent.

**The larger lesson from this session:** the decisive information was almost always a *measurement*
(`render = 0.08 ms` proving nothing repainted; 242 ms proving the copy starved the scan), and the
expensive mistakes were all reasoning from a plausible story instead of measuring — including one
measurement I broke myself, which cost several rounds.
