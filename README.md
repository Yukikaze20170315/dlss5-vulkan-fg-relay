# Vulkan + DLSS Frame Generation: Generic's NR on the finished frame, now without the stutter

## Short version

- **What it is.** In Vulkan games with native DLSS Frame Generation, RenoDX DLSS 5 Generic cannot serve its "Present" hook point (it falls back to Upscaled). A change to the DLSS 5 bridge add-on (`dlss5-bridge.addon64`) fills that gap: it runs Generic's neural rendering on the game's finished real frame (post-processing included, HUD kept out) right before the native DLSS-G evaluate, once per real frame, and frame generation then interpolates the enhanced frames. Generic itself is not modified.
- **What was wrong with the first version** ([NIGos/dlss5-bridge#51](https://github.com/NIGos/dlss5-bridge/pull/51)). The frame generation command buffer waited on the CPU while NR ran. Only about half of the presented frames reached the screen, so it looked like a high frame counter with a stutter.
- **What this update does.**
  1. The hand-over between Vulkan and the add-on's D3D12 side now happens on the GPU (a "GPU relay"). Frames reaching the 120 Hz screen: 59.5/s → 112.7/s. Share of on-screen frame intervals of 25 ms or more: 22.4 % → 0.02 %.
  2. It holds the game right after its Reflex sleep until the newest frame's NR is done, so frames stop piling up in front of NR. PC latency p50 of the relay: 246 ms → 158 ms, without losing real frame rate (19.9 → 20.9 per second).
  3. It fixes a bridge bug that sometimes made Generic's ReShade page disappear at game start.
- **Trade-off.** Median latency is still about 37 ms higher than the stuttering first version (158 vs 121 ms); the 95th percentile is lower (181 vs 202 ms). Tested in one game only: Arknights: Endfield, 5120x2160, RTX 5090, Generic 8.5.0-rc10 with two NR passes, native DLSS-G fixed 6x.
- **Use it or merge it.** A ready-to-use build, three patches against NIGos/dlss5-bridge `d1cc508` (the first two are #49 and #51 unchanged), full copies of every changed file, the offline tests and the raw measurements are all linked below.

Everything after this point is technical detail.

## Files

All paths below are inside https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay. The pull request to PEQHUB/RenoDX-DLSS5-Generic carries the same tree under `contrib/dlss5-bridge-vulkan-fg-relay/`, without `evidence/raw/`.

| What | Where |
| --- | --- |
| Ready-to-use build: DLL, `vk-present-adapter.ini`, install notes, licenses, SHA-256 sums | Release [`vk-fgrelay-20260929`](https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/releases/tag/vk-fgrelay-20260929), file `dlss5-bridge-vk-fgrelay-20260929.zip`. The DLL alone is attached too. |
| Full source of that build | Same release, `dlss5-bridge-vk-fgrelay-source-d1ef16f.zip`; also branch [`feature/vulkan-fg-relay`](https://github.com/Yukikaze20170315/dlss5-bridge/tree/feature/vulkan-fg-relay) of the bridge fork (commit `d1ef16f`). |
| Patches for merging | `merge/patches/`: `0001` = #49, `0002` = #51, `0003` = this update. `merge/CHANGED-SINCE-PR51.txt` lists what `0003` touches. |
| Full copies of every file changed since `d1cc508` | `merge/changed-files/` (source, tests, docs). |
| Design document | `docs/VULKAN-FG-INPUT-NR.md` |
| Install notes | `docs/INSTALL.md` (same file as in the zip) |
| Offline test results | `evidence/summary/offline/` |
| Measurement summary | `evidence/summary/presentmon-comparison.json` |
| Raw PresentMon captures and game logs | `evidence/raw/` |
| Script that computed the table below | `tools/compare-presentmon.py` |

## What changed since #51

1. **GPU relay** (`src/fg-relay.inc`, `Pipeline=2`). The four input copies (HUD-less image, back buffer, depth, motion vectors) are submitted as the add-on's own Vulkan batch on `DLSSG.CmdQueue`. The private D3D12 queue waits for them on the GPU, runs Generic's NR and the UI-preserving composite, and signals a shared fence. A one-command Vulkan batch waits for that fence (imported as a timeline semaphore) and sets the event that Streamline's command buffer is parked on. No CPU thread waits for NR any more, and the FG evaluate thread never blocks.
2. **Every frame gets NR (the flicker fix).** When the next frame is armed while the previous frame's NR is still running (the normal case when the GPU is full), the copy batch now waits for that NR on the GPU. An earlier build skipped such frames on the CPU instead: in the game, half of the FG groups had no NR (armed 10.4/s, skipped 9.7/s), and groups with and without NR alternating at about 10 Hz looked like heavy flicker. With the GPU wait, one session armed 2945 groups with 0 unarmed and `flips=0` throughout, and the flicker was gone.
3. **Device extension injection** (`Import=1`). The GPU-side hand-over needs `VK_KHR_external_semaphore_win32` on the game's `VkDevice`, and ReShade does not enable it. The add-on hooks the ReShade layer's `vkCreateDevice` and appends the name to a copy of the create info. If the driver refuses, the call is retried with the caller's original create info, so the add-on cannot be the reason device creation fails.
4. **Throttle at the Reflex sleep point** (`Throttle=1`). Reflex only sees the game's own queue, so the game ran ahead of NR and frames queued in front of it (PC latency p50 246 ms). The add-on hooks `slReflexSleep` (found through `sl.reflex.dll!slGetPluginFunction`) and, after Reflex's own sleep, waits until the newest armed frame's NR has finished. Each wait is bounded to 100 ms, and five timeouts in a row turn the throttle off for the process. Waiting at the game's present instead only gave 246 → 209 ms, because the frame had already been simulated; waiting before simulation gave 246 → 158 ms.
5. **Watchdog restart.** Switching Generic's hook point away from Present and back reopened the relay with a stale clock. Its 1.5 s "no fence progress" watchdog fired 0.2 s after reopening, and the process stayed on the old serial path. The clock now restarts at the first release of each session.
6. **Generic's page disappearing** (`src/ngx-module-scan.h`). The bridge's NGX module scan took a reference on every loaded module before checking its exports. If ReShade destroyed a temporary Vulkan instance during the scan, Generic could not unload; ReShade later unregistered it after the final instance existed, and Generic's page was gone. The scan now reads the export table first and references only modules with an NGX Create/Evaluate pair. Reproduced offline with the real ReShade 6.8 and the real Generic: the #51 binary loses Generic in this race; the new build never references it.
7. **Diagnostics.** Every 15 s the bridge log has `[fg-relay] rate` lines: armed, unarmed and skip reasons, GPU waits, `flips`, throttle waits and timeouts. `Trace=1` adds pass-through observers for the game's present, `slSetTag` and queue submits.

## Measurements

PresentMon 2.5.1 (`--track_pc_latency --track_app_timing --track_frame_type`), game in the foreground, 40 s per run, first 3 s dropped. "Real frames" are PresentMon simulation marks. This is not a controlled benchmark: the serial run is from 2026-09-28, the others are from one play session on 2026-09-29, and the scenes are not guaranteed to be identical. Display: 120 Hz.

| | serial feed (#51) | relay, no throttle | **relay + throttle** | NR off |
| --- | --- | --- | --- | --- |
| presents / s | 106.8 | 119.3 | **125.0** | 269.0 |
| displayed / s | 59.5 | 111.4 | **112.7** | 114.9 |
| real frames / s | 21.7 | 19.9 | **20.9** | 44.8 |
| display interval p50 / p95 (ms) | 16.66 / 33.33 | 8.34 / 16.66 | **8.33 / 16.66** | 8.33 / 16.65 |
| display intervals ≥ 25 ms | 22.4 % | 0 % | **0.02 %** | 0.02 % |
| PC latency p50 / p95 (ms) | 120.8 / 202.0 | 245.8 / 268.5 | **157.9 / 181.4** | 81.1 / 101.9 |

- The relay removes the stutter: nearly every present reaches the screen.
- Without the throttle, median latency is about 125 ms higher than the serial feed. A driver frame cap below the GPU's throughput brought it down to 132 ms (at 16.7 real frames per second; capture in `evidence/raw/`), which showed that it is a queue in front of NR. The throttle removes most of it without lowering the frame rate.
- With the throttle, median latency is still about 37 ms above the serial feed, while p95 is about 21 ms lower. The serial feed's lower median came from the game itself waiting on the parked frame; its price was the stutter.
- The GPU is full. NR costs 26.5–27.9 ms per real frame (two passes at 5120x2160), which is about the difference between 20.9 and 44.8 real frames per second. This update does not make NR cheaper.
- The "relay, no throttle" column is test build fgrelay9 and the "relay + throttle" column is test build fgrelay12. The published build is fgrelay12 plus the watchdog restart fix (item 5), without a local diagnostic channel that was never part of these pull requests.

## Tests

Offline, on the published build (all passed; outputs in `evidence/summary/offline/`):

| Test | Result |
| --- | --- |
| Build, MSVC `/W4 /WX` | clean |
| `tests/vulkan-fg-relay/test-fg-trace.cmd`: CPU contracts of the hooks and observers, with the whole add-on source compiled in | 7382 checks, 0 failures |
| `tests/vulkan-fg-relay/scan/`: `ngx-module-scan.h` against the real Windows loader | 20705 assertions passed |
| `tests/vulkan-fg-relay/lifetime/`: real ReShade 6.8 + real Generic, two Vulkan instances | #51 binary, `race`: Generic unregistered (bug reproduced). New build, `stable`: 2 registrations, 0 references taken, clean unload. `control` (no bridge): pass. `late-ref` negative control: detected, exit 12 as expected. |
| `tests/vulkan-fg-relay/inject/`: device created without the extension | extension appended by the hook; D3D12 Signal → Vulkan timeline value, and D3D12 Signal → `vkWaitSemaphores`: both pass |
| `tests/vulkan-fg-input/run-roundtrip.py`, SDR with the RR module loaded, and HDR10 | passed (one Generic workset, zero ROI error) |
| `tests/vulkan-fg-input/test-witness.cmd`, `test-composite.cmd` | pass; composite 2868 / 192 / 12 / 0 |

The relay's arm/copy/park/release chain has no offline host; it needs a real DLSS-G evaluate. In the game:

- Test build fgrelay9 (relay + GPU wait): the user reported that the smoothness and flicker problems were solved; the log shows every FG group armed and `flips=0`.
- Test build fgrelay12 (relay + throttle): the measurements above; the user reported that latency now feels good.
- The published build was run in the game before publishing (log in `evidence/raw/game-logs/published-build-check/`): Generic's page present, NR on (two passes), 4410 FG groups armed and written, no throttle timeouts, and after switching Generic's hook point Present → Upscaled → Present the relay reopened without a watchdog fallback. The only unarmed group was the first one after reopening (1 of 4411, one `flips` transition); FG used the game's own image for that one group. The user reported no stutter.

## Limitations

- One game, one GPU (Arknights: Endfield, RTX 5090). Other Vulkan DLSS-G games, HDR swapchains in a game, long sessions, alt-tab and swapchain recreation are not covered.
- Frame generation inputs must be 8-bit sRGB with a full-image subrect, and the game must supply `DLSSG.HUDLess`. Otherwise the frame is refused with a log line and the game keeps its own image.
- The add-on accepts only the unmodified Generic 8.5.0-rc10 (SHA-256 `DCD93881E976AD033D83C2BB01F4BC3E4DDC59C15FE0DD4CA165BC5FC7D1AC68`; the check is `PresentAdapterConsumer` in `src/present-adapter.inc`).
- Only `Pipeline=2` with `Import=1` is validated. `Pipeline=3` (copy at the game's present) and `Import=0` keep an older CPU gate and left many frames unarmed in the game.
- One effect runtime and one swapchain.
- Generic's layer count and per-layer settings are global, so the normal Vulkan path (Upscaled) and this path (Present) cannot use different layer settings.
- Latency: see the trade-off above.

## Other notes

- **Known issue, to be improved.** Moving Generic's "Source encoding", "Source primaries" or "Linear unit (nits)" away from Auto stops NR until the game is restarted, even after moving it back. The add-on re-reads the saved `ReShade.ini` on every FG evaluate, and a refusal flag is never cleared. The log shows `[present-adapter] refused: source encoding, primaries and linear-unit overrides must be Auto`. Because the picture then has no NR, this is easy to mistake for "that setting fixed an artefact". Only a forced source encoding actually misreads the input. A better rule is described in `docs/VULKAN-FG-INPUT-NR.md`.
- **About the word "Present".** Here it names Generic's hook-point setting that switches this path on. The processing itself happens before DLSS-G, on the real frame, not on the frames after frame generation.
- **Endfield setup.** Fixed 6x frame generation in this game was set up separately (newer Streamline plugins and a driver profile). That setup is not part of this change.
- **Why a contrib folder in PEQHUB/RenoDX-DLSS5-Generic.** The code belongs to the bridge and is also opened as a pull request on NIGos/dlss5-bridge (stacked on #49 and #51, which are still open). The contrib folder puts the material next to Generic for Generic users and maintainers. Nothing outside `contrib/` is changed and no Generic source is touched. Feel free to close it if you prefer not to host it.

## Install and rollback

See `docs/INSTALL.md`. In short: close the game, copy `dlss5-bridge.addon64` and `vk-present-adapter.ini` next to the game's `.exe`, keep Generic's `EnableHooks=1`, and choose **Present** in Generic's hook-point control. Rollback: put the previous `dlss5-bridge.addon64` back, or set `Enabled=0`.

## Merging

- Whole change: apply `0001`–`0003` on NIGos/dlss5-bridge `d1cc508`. If #49 and #51 are merged first, only `0003` is needed.
- Smaller steps, in order: the serial feed from #51; the relay with the GPU wait at arm time; the extension injection; the throttle at `slReflexSleep`. `ngx-module-scan.h` is independent of all of them. Details: "Minimal integration for maintainers" in `docs/VULKAN-FG-INPUT-NR.md`.
- Build: `src\build.cmd` (MSVC, `/W4 /WX`). Published DLL: SHA-256 `6239E138861B03F756D1794971E70B6F8E6EA7476D47CBE8D11445FA8C039221`.

## Reproducing

- Offline: `tests/vulkan-fg-relay/README.md` lists each test, what it needs and what passing looks like.
- In a game: PresentMon 2.5.1 with `--process_id <pid> --output_file presentmon.csv --track_pc_latency --track_app_timing --track_frame_type` for 40 s in the foreground, then `python tools/compare-presentmon.py --trim-s 3 label=presentmon.csv ...` for the same numbers as the table.
