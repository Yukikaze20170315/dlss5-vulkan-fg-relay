# Vulkan: neural rendering of the final frame at the Frame Generation input

Status: verified in one title (Arknights: Endfield, Vulkan, RTX 5090, Generic
8.5.0-rc10 with two NR passes). The 2026-09-29 measurements below used
5120x2160, native DLSS-G fixed 6x and DLAA; the 2026-10-01 update was verified
with the game's own 4x, every DLSS Super Resolution mode, several output
resolutions, fullscreen and windowed; the 2026-10-02 build was run with
Streamline 2.14.1 and with the game's own Streamline 2.10.3. Not yet tested
with other Vulkan games, HDR swapchains, or without native Frame Generation.

Two stages are described here:

* **Serial feed** (`Pipeline=0`, the first version): the real frame is copied,
  processed and copied back while Streamline's FG command buffer waits.
* **GPU relay** (`Pipeline=2`, `Import=1`, `Throttle=1`, the recommended
  setting): the same processing, but the copies are the add-on's own Vulkan
  batch and every wait between Vulkan and the private D3D12 session happens on
  the GPU; the game is held at the start of its frame so frames do not queue in
  front of the neural pass.

## Update 2026-10-01

Six problems found after the 2026-09-29 release, all in the bridge:

1. **DLSS modes other than DLAA.** With Quality, Balanced or Performance the FG
   depth and motion vectors are smaller than the back buffer (for example
   3414x1440 for a 5120x2160 output). The relay required equal sizes, so after
   120 refused frames it fell back to the serial feed for the rest of the
   process (`fell_back` was never cleared), and the serial feed then ran NR with
   `mv=zero depth=zero`. Turning the camera showed a flicker on surfaces, and
   the frame pacing stayed uneven even after switching back to DLAA. Now
   `fg-guides.inc` reads each guide's own sub-rectangle and scales it to the
   output grid with a nearest-neighbour compute shader on the FG queue
   (`fg-guide-compute.inc`, `fg-guide-nearest.comp`; raw 32-bit texels, so R32F
   depth and RG16F motion vectors are copied bit-exactly). The motion-vector
   scale is multiplied by output size / motion-vector size; the unit was
   confirmed by disassembling the game's Streamline 2.14.1 DLL. A compute shader
   is used because `vkCmdBlitImage` needs a graphics queue, and in this game FG
   runs on a compute-only queue family while the window is focused. No image is
   read back to the CPU and no CPU wait is added.
2. **Fullscreen at a resolution smaller than the monitor.** On a 5120x2160
   monitor, fullscreen 3840x2160 gives a 5120-wide back buffer whose real image
   is the sub-rectangle x=640, w=3840, while the HUD-less image is 3840 wide at
   x=0. The old check compared whole-image sizes and refused every frame. Each
   input now carries its own origin; only the valid regions must have equal size
   and format. Copies read and write each region at its own origin and leave the
   outside untouched. Size changes rebuild the private resources safely.
3. **NR stopping for good during normal play.** FG handles were kept in an
   8-entry table that was never cleaned on release. The game recreates its FG
   feature from time to time; from the ninth new handle on, the relay no longer
   recognised the evaluate and NR silently stopped until restart.
   `fg-handle-registry.h` now tracks create/release with generations, has no
   fixed capacity and survives address reuse. Transient input problems pause
   and retry instead of refusing for the rest of the process.
4. **Loss of GPU completion.** If the private D3D12 queue stops signalling for
   1.5 s, the relay releases every park (unchanged). New: once the CPU worker
   and recording have stopped, a marker on the D3D12 queue proves that earlier
   work has finished, and the Vulkan copy/release fences and parks are checked;
   only then is the session rebuilt automatically. If completion cannot be
   proven, resources stay alive and NR stays paused.
5. **Frame generation while the window is not focused.** Streamline stops
   DLSS-G when its internal `IKeyboard::hasFocus()` returns false. `focus-fg.inc`
   locates that one call site in `sl.dlss_g.dll` by an instruction/branch/log
   string contract (not by hash or fixed RVA) and returns true for that caller
   only, while NR is at the FG input and the window is visible, not minimised
   and not resizing. Every other caller, the OS focus, an explicit FG Off and
   resource release are untouched. `FocusKeepFG=0` turns it off. If the contract
   is not found, native behaviour is kept.
6. **A flash of the unprocessed picture about 1-2 s after switching to another
   window.** With nothing else on the game's monitor, Windows promotes the game
   from composed flip to independent flip, and back when the window returns. At
   each switch Streamline changes DLSS-G pacing (`FlipMetering` 0 <-> 2) and its
   present queue, and for 9-15 real frames it presents the game's frames without
   calling DLSS-G. NR runs only inside the FG evaluate, so those frames have no
   NR. Evidence: in the `[fg-transition]` log the game's frame ID jumps by 9-15
   with no evaluate at every one of 29 switches, and PresentMon shows 12-15
   frames with one present per real frame after every present-mode change.
   `composition-guard.inc` keeps DWM composing the game's monitor: a 2x2, alpha
   1/255, click-through, non-activating, topmost tool window of the game process
   at the monitor's top-left corner, excluded from screen capture, shown while NR
   is at the FG input, FG was evaluated in the last 1.5 s and the window is
   shown. The game is already composed while focused, so focused behaviour does
   not change. `HoldComposition=0` removes it.

Also: the Generic hash whitelist is gone. Any Generic build is tried; a missing
module, disabled hooks, an unsupported hook point, source overrides, a missing
NR/SR entry point or a carrier mismatch are logged and shown in the panel with
the reason, together with the build that is known to work (8.5.0-rc10). The
three source-interpretation overrides (encoding, primaries, linear unit) now
pause NR and it resumes when they are back on Auto, instead of stopping it until
restart. `[fg-transition]` logs bounded CPU metadata (queue, reset, metering,
frame ID, handles) for the first 12 FG calls after a queue, focus or reset
change, at most 128 windows per thread.

## Update 2026-10-02: SR carrier retry

**Symptom.** In some game sessions NR never starts. Frame generation runs
normally on the game's own frames, and changing the resolution once brings NR
back. The bridge log shows:

```
[present-adapter] multiple SR modules found; refusing ambiguous carrier hook.
[fg-input] refused: private SR carrier hook unavailable. Frame generation continues on the game's own frames.
```

**Cause.** The private carrier is the D3D12 evaluate export of the one loaded
module whose version resource says DLSS Super Resolution. With a driver-side
DLSS override, that module is the driver's model (`...\NGX\models\dlss\...\*.bin`).
While the bridge creates its private NGX feature, NGX also maps the game
folder's `nvngx_dlss.dll` and releases it shortly afterwards. The bridge's own
NGX module scanner holds a reference to each NGX module while it installs its
hooks there, so the DLL stays mapped until that scan has finished. In 28 saved
sessions where NR started, the scan took 0.40-0.91 s and finished 120-441 ms
before the carrier search. In the failing session it took 1.26 s, the search
ran 190 ms before it finished, and both modules were visible. Refusing an ambiguous carrier is correct, but the refusal lasted
for the whole session. A resolution change recovered only because it rebuilds
the private session and searches again.

**Change.** A carrier failure that can clear by itself no longer refuses for the
session: no unique SR module (none yet, or two at once), an SR module that
could not be retained, and MinHook status 9 (no free memory within +-1 GB of the
target). The private session stays built and owned, no frame is armed or
processed, and the search is repeated once per second, up to 30 attempts.
While the carrier is pending, evaluates are not handed to the relay: the relay
ends every unarmed evaluate itself, so the retry step would never run.
Any other failure, and the 30th failed attempt, refuse as before. An ambiguous
carrier is still never hooked. The module list is logged on the first and last
attempt only. When the hook is installed, the log names the module and the
panel's SR/hook diagnostic is cleared. A size change or runtime loss while the
carrier is pending releases the session through the normal drained path,
because the session is owned from the moment it is imported. Sessions where
the first attempt succeeds behave exactly as before.

Files: `src/present-carrier-retry.h` (new, policy), `src/present-adapter.inc`
(result classification), `src/fg-input.inc` (retry step; relay skipped while
pending), `src/fg-relay.inc` (no arm while pending). Tests:
`tests/vulkan-fg-quality/run.cmd carrier` (production search, MinHook install,
retry step and relay hand-off against fixture DLLs), and the test-only add-on
`carrier-race-helper.cpp` (`run.cmd racehelper`), which recreates the race in
the game: when the first D3D12 device is created (the bridge's private device),
it loads the game folder's `nvngx_dlss.dll` and holds it for 3 s, so the first
carrier search sees two SR modules.

## Problem

RenoDX's DLSS 5 Generic offers three hook points: Upscaled, Render and Present.
On Vulkan the add-on itself falls back: `NRHookPoint=Present is not served on
Vulkan; NR runs at the Upscaled hook point`. The Present point is what users
want when they need the neural pass to see the finished frame (post-processing,
colour grading, HUD) rather than the DLSS output.

Two facts constrain any Vulkan implementation of that point:

1. **Where the frame is read decides how many times NR runs.** A game with
   native Frame Generation presents 6 frames per real frame at 6x. Anything that
   processes the present stream (ReShade `begin_effects`, a swapchain hook)
   pays one full NR evaluate per presented frame. At 5120x2160 with two NR
   passes one evaluate costs ~20-40 ms, so a post-FG feed collapses to ~29
   presents/s and drags the game's simulation rate down with it.
2. **The private D3D12 session is only usable from a thread that may block.**
   Generic's NR runs inside `NVSDK_NGX_D3D12_EvaluateFeature` on the bridge's
   private device; the Vulkan side has to wait for that work before it can copy
   the result back. Doing that inside the FG evaluate (Streamline's present
   thread) with a device-wide wait deadlocked the game once during development,
   because the render thread's queue was waiting on present progress.

## Serial feed (Pipeline=0)

The feed hooks the game's own Frame Generation evaluate (NGX feature 11, which
the bridge already forwards) and processes **only the real frame, once**, before
the evaluate is forwarded:

```
FG evaluate (feature 11, MultiFrameIndex == 1)
  copy DLSSG.HUDLess   -> private COLOR   (shared D3D12 texture, imported VkImage)
  copy DLSSG.Backbuffer-> private OUT_B
  copy DLSSG.Depth     -> private DEPTH   (R32_SFLOAT, output resolution)
  copy DLSSG.MVecs     -> private MV      (R16G16_SFLOAT, DLSSG.MvecScaleX/Y)
  vkCmdSetEvent(in) ; vkCmdWaitEvents(out)         <- the mirror's existing park
  copy private OUTPUT  -> DLSSG.HUDLess
  copy private OUT_B   -> DLSSG.Backbuffer
forward the FG evaluate
```

The worker thread, woken by the `in` event:

```
sRGB decode COLOR -> linear FP16 working textures
NVSDK_NGX_D3D12_EvaluateFeature on the bridge's own 1:1 SR feature
   |- the SR runtime's public export is hooked for this thread only and answers
   |  with a same-size copy instead of running DLAA (identity carrier)
   |- Generic's after-upscale detour runs its NR passes on that call as usual
sRGB encode -> OUTPUT
composite: OUT_B = Backbuffer + (NR - HUDLess) * w,  w = saturate(1 - |Backbuffer - HUDLess| / 0.25)
set the out event
```

Consequences:

* NR cost is paid per **real** frame, and FG still generates its frames from the
  processed image.
* The UI is preserved: FG receives both the processed HUD-less image and a
  back buffer whose UI pixels are untouched, so real and interpolated frames
  agree. Translucent panels keep a faded part of the enhancement instead of a
  hard cut-out.
* Generic is used **unmodified** (8.5.0-rc10, SHA-256
  `DCD93881E976AD033D83C2BB01F4BC3E4DDC59C15FE0DD4CA165BC5FC7D1AC68`). Nothing
  reads its private layout; the identity-carrier hook is on the SR runtime's
  public `NVSDK_NGX_D3D12_EvaluateFeature`, scoped by thread and by resource
  identity, and tail-forwards every other caller unchanged.
* Depth and motion vectors at output resolution come from the FG parameter
  block, so the neural pass has real guides.

What the serial feed costs: the real frame is parked ~28 ms while NR runs, and
generated frames cannot present in that window. In the game only ~59 of ~107
presents per second reached the screen and 22 % of the on-screen frame
intervals were 25 ms or longer (see "Measurements"). It looks like a high frame
counter with a stutter.

## GPU relay (Pipeline=2, Import=1)

`fg-relay.inc`. The relay keeps the serial feed's processing and changes only
how the two APIs hand the frame to each other:

```
FG evaluate (feature 11, MultiFrameIndex == 1)
  arm: record the four copies into THIS add-on's own command buffer and submit
       it on DLSSG.CmdQueue, signalling an exported timeline semaphore (copy done)
  record into Streamline's command buffer: vkCmdWaitEvents(ev_out[slot]) (the park),
       vkCmdSetEvent(ev_mark[slot]), then the copy back
  submit the release batch: { vkCmdSetEvent(ev_out[slot]) }, waiting nr_fence
       imported as a timeline semaphore (value nr_val)
forward the FG evaluate                      <- the present thread never waits on NR

worker (private D3D12 queue, nothing waits on the CPU):
  queue->Wait(copy-done fence)               <- the exported semaphore, opened as a D3D12 fence
  NR + composite as in the serial feed
  queue->Signal(nr_fence, nr_val)            <- D3D12_FENCE_FLAG_SHARED
                                             -> the release batch runs, the park releases on the GPU
```

Details that matter:

* **Four park slots.** A slot is reused only when the batch that waited on it has
  executed (`ev_mark`) and the D3D12 signal that released it has completed. The
  copy and release command buffers and their fences are per slot, because a
  frame's release is still waiting on its NR when the next frame is armed.
* **When the previous frame's NR is still running at arm time** (the normal case
  when the GPU is full), the copy batch gets an extra timeline wait on the
  previous frame's `nr_val` instead of skipping the frame. An earlier build
  skipped such frames on the CPU; exactly half of the FG groups then had no NR,
  and NR / no-NR groups alternating at ~10 Hz was seen as heavy flicker. With the
  GPU wait every group is armed (`flips=0` over the whole session).
* **The relay owns every frame once open.** A frame it cannot arm is FG's own
  image for that one group (counted as `unarmed`), never a mix with the serial
  feed. The relay opens only after the serial feed has built the private
  session, the shared textures and the imported images.
* **Fallbacks.** 120 consecutive frames that cannot be armed, 8 consecutive
  handle mismatches, an unknown queue family, or a shared fence that makes no
  progress for 1.5 s: the relay host-signals everything it parked, drains, and
  the process continues on the serial feed. The 1.5 s clock restarts at the first
  release of each session, so switching Generic's hook point away and back does
  not read the closed time as a stall.
* `Pipeline=3` does the same copy at the game's `vkQueuePresentKHR` on the
  present queue (~5 ms earlier on average). It ran in the game but has known
  gaps (see "Known limits") and is not recommended.

### Device extension injection (Import=1)

The two GPU-side hops need `VK_KHR_external_semaphore_win32` on the game's
`VkDevice`. ReShade does not enable it, and an extension can only be enabled at
`vkCreateDevice`, before any add-on code runs on that device. With `Import=1` the
bridge hooks the ReShade layer's `vkCreateDevice` (address from the layer's own
`vkGetInstanceProcAddr(nullptr, "vkCreateDevice")`) and appends the name to a
**copy** of the create info. If the driver refuses, the call is retried with the
caller's own create info unchanged, so the bridge cannot be the reason a game's
device creation fails. The import is used only on devices that were shown to
accept the name (an 8-entry table); every other device keeps the host path.
`vkGetDeviceProcAddr` returning non-null for the KHR entry points is **not**
proof: the NVIDIA driver returns them for devices without the extension. The
game crashes during this work (`READ 0xc5` in `nvoglv64.dll`) were first blamed
on that, but the real cause was a wrong parameter count for
`vkGetSemaphoreWin32HandleKHR` (the standard signature has three parameters;
the driver read the semaphore handle as the info pointer).

### Latency: Throttle at the Reflex sleep point

Reflex sees only the game's own queue. NR runs on the bridge's private D3D12
queue, so with the GPU full the game keeps running ahead and frames queue in
front of NR. Measured: PC latency p50 246 ms with the relay alone.

`Throttle=N` (default 1, 0 = off, at most 3) holds the game until the shared
fence shows the NR of the newest armed frame, minus N-1 frames, has completed.
Where it waits decides what it buys:

* At the game's `vkQueuePresentKHR` the frame is already simulated; the wait
  only moves the queue into that frame's latency (246 -> 209 ms).
* At `slReflexSleep` (after Reflex's own sleep, before input sampling and
  simulation) the latency goes: 246 -> **158 ms**, with no loss of real frame
  rate (19.9 -> 20.9 /s). The bridge finds the function with
  `sl.reflex.dll!slGetPluginFunction("slReflexSleep")`, the same address
  `slGetFeatureFunction` hands to the game, and hooks it from the NGX worker.

The present site is only the fallback for games that never call
`slReflexSleep`; it stands down while the sleep site is live. Each wait is
bounded (100 ms); five consecutive timeouts turn the throttle off for the
process. The wait loop checks the fence value after every wake: the event is
auto-reset and shared, so a registration left behind by an earlier timeout can
wake a later wait early.

## Generic's page disappearing (module scan)

Independent of the relay: on some starts RenoDX Generic's page was missing from
ReShade and NR did not run. The bridge's NGX module scan took a reference on
**every** loaded module before checking its exports, and held it until the scan
ended. When ReShade destroyed a temporary Vulkan instance during that window,
Generic could not unload; ReShade then treated it as an externally registered
add-on, and its late unregistration landed after the final instance was
created. `ngx-module-scan.h` now reads the export table first (under a
`TRY_ONLY` loader lock, no forwarded-export resolution, no hooks under the
lock) and references only modules that export an NGX Create/Evaluate pair.
Reproduced offline with the real ReShade 6.8 and the real Generic: the previous
bridge build unregisters Generic in the race; this build never references it.

## Colour and configuration

(The "known issue" below describes the 2026-09-29 build. Since 2026-10-01 the
three overrides pause NR and it resumes when they are back on Auto.)

* UNORM8 swapchains (sRGB, the only FG inputs handled): decode to normalized
  linear FP16 for the carrier, encode back after NR. Generic then sees
  `encoding=linear units=relative`.
* `NRSourceEncoding`, `NRSourcePrimaries` and `NRLinearUnitNits` must stay at
  Auto (0). A forced "SDR encoded" declaration over the already-linear input made
  Generic rebuild its workset every frame.
* **Known issue, to be improved:** the check reads the saved `ReShade.ini` on
  every FG evaluate, and a violation sets a refusal flag that nothing clears.
  Moving any of these three sliders stops NR until the game is restarted, even
  after the slider is moved back. The log shows one line, `[present-adapter]
  refused: source encoding, primaries and linear-unit overrides must be Auto`,
  and no further `[fg-relay] frame=` lines. Because the picture then has no NR,
  this is easy to mistake for "that setting fixed an artefact". Only
  `NRSourceEncoding` = 1 or 3 actually misreads the carrier;
  `NRLinearUnitNits > 0` only declares an absolute scale, and
  `NRSourcePrimaries` is only logged by Generic 8.5.0-rc10. A better rule would
  pause while the configuration is invalid, resume with a history reset when it
  is valid again, refuse only encodings other than 0 and 2, and keep the
  permanent refusal for real build, import or submit failures.
* **Setting note.** The carrier is relative-unit HDR. With Generic's HDR mode
  (`NRCodecMode`) on Classic (0), Generic removes the dark pedestal on every
  frame, per 32x32 block, wherever the block's brightest untouched pixel is under
  0.005; on large dark surfaces close to the camera this shows as dark blocks
  that move with the view. "Smooth Auto" pedestal removal only softens the gate.
  HDR mode **Auto** (2) uses the Display codec for this input and skips the
  removal; the blocks disappear.
* HDR10 (PQ) swapchains use the existing PQ decode/encode pass with FP32
  intermediates and exact BT.2020/BT.709 matrices. HDR is verified only in the
  offline host.

## Configuration (`vk-present-adapter.ini`, read at start)

```
[Adapter]
Enabled=1
Source=fg-input
Follow=1
Pipeline=2
Import=1
Trace=0
; Throttle=1 is the default
; FocusKeepFG=1 and HoldComposition=1 are the defaults (2026-10-01)
```

With `Follow=1`, Generic's own hook-point control is the switch: **Present** runs
this feed, **Upscaled/Render** keep the existing Vulkan mirror; the session is
handed over on the render thread.

## Files

| File | Role |
| --- | --- |
| `src/fg-input.inc` | FG-evaluate hook: parameter validation, copies, park, worker half, session hand-over, relay entry. |
| `src/fg-relay.inc` | GPU relay: park ring, own copy/release batches, shared fence and timeline semaphores, device-extension injection, throttle, watchdog and fallbacks, 15 s `rate` log lines. |
| `src/fg-trace.inc` | Hooks installed from the NGX worker: `sl.interposer!vkQueuePresentKHR`, `vkGetDeviceQueue`, `slReflexSleep`; `Trace=1` pass-through observers. |
| `src/ngx-module-scan.h` | Export-table pre-check before a module is referenced. |
| `src/fg-composite-shader.h` | UI-preserving composite (cs_5_0). |
| `src/present-adapter.inc` | Identity-carrier hook on the SR runtime, NR entry witness, configuration checks, the reference present-stream feed. |
| `src/present-adapter-config.h` | `vk-present-adapter.ini` keys: `Enabled`, `Source`, `Follow`, `AutoRun`, `Pipeline`, `Import`, `Throttle`, `Trace`. |
| `src/present-nr-witness.asm` | Tail-forwarding thunks that preserve the four arguments and the caller's return address. |
| `src/vkmirror.inc` | Worker dispatch (relay first, then `fg_arm`), FG handle tracking, mirror/feed arbitration. |
| `src/synth.inc`, `src/bridge.inc`, `src/bridge.h` | sRGB path in the colour pass, submission without a CPU completion wait for the relay, fence-completion proofs, resource retention when completion is unproven. |
| `src/dlss5-bridge.cpp` | Module-scan integration, NGX worker maintenance call. |
| `src/fg-guides.inc`, `src/fg-guide-compute.inc`, `src/fg-guide-math.h`, `src/fg-guide-nearest.comp`, `src/fg-guide-nearest-spv.h` | Guide sub-rectangles, motion-vector scale, GPU nearest-neighbour scaling (2026-10-01). |
| `src/fg-handle-registry.h` | FG/NGX handle lifetime with generations (2026-10-01). |
| `src/fg-history-policy.h` | History-reset decisions used by `fg-guides.inc`. |
| `src/focus-fg.inc`, `src/focus-fg-contract.h` | Scoped Streamline focus gate (2026-10-01). |
| `src/composition-guard.inc` | Keeps the game's monitor composed (2026-10-01). |
| `src/present-compatibility.h` | Compatibility codes and messages instead of a hash whitelist (2026-10-01). |
| `src/present-carrier-retry.h` | Which SR carrier failures are retried, how often and how many times (2026-10-02). |
| `src/vk-present-adapter.ini.example` | Documented configuration. |
| `tests/vulkan-fg-input/`, `tests/vulkan-fg-relay/`, `tests/vulkan-fg-quality/` | Offline tests (below). |

## Tests

| Test | Checks |
| --- | --- |
| `tests/vulkan-fg-input/test-witness.cmd` | The asm thunks preserve arguments, return address and result; the identity gate is thread-local. |
| `tests/vulkan-fg-input/test-composite.cmd` | Composite on a D3D12 device: 2868 scene pixels take the NR result, 192 opaque UI pixels are untouched, 12 translucent pixels blend. |
| `tests/vulkan-fg-input/run-roundtrip.py` | Off-screen Vulkan host through the present-stream feed (SDR, HDR10, with the RR module loaded): copy, colour round-trip, two NR requests; zero ROI error, one Generic workset. Needs third-party binaries in `--assets`. |
| `tests/vulkan-fg-relay/test-fg-trace.cmd` | 7382 CPU contract checks of the observers and hooks with the whole add-on source compiled in (real MinHook patches, five concurrent threads, bounded logging). |
| `tests/vulkan-fg-relay/scan/` | 20705 assertions of `ngx-module-scan.h` against the real Windows loader: non-candidates, near-miss names, every interface pairing, 20 unload/reload cycles, loader-lock contention, forwarded exports. |
| `tests/vulkan-fg-relay/lifetime/` | Real ReShade 6.8 + real Generic, two Vulkan instances: `race` reproduces the Generic unregistration with an older bridge, `stable` / `control` pass, `late-ref` must fail with exit 12. |
| `tests/vulkan-fg-relay/inject/` | A device created without the extension gets it through the hook; D3D12 Signal -> Vulkan timeline value, and D3D12 Signal -> `vkWaitSemaphores`, both pass. |
| `tests/vulkan-fg-quality/run.cmd SUITE <new-dir>` | 2026-10-01 suites: `registry` (handle table, 188780 checks), `guides` (57), `hooks` (production NGX wrappers, 535), `colour` (sub-rectangle copies, 39), `recovery` (real D3D12 marker, 34), `compatibility` (26), `guard` (real Win32 windows, 28), `focus <sl.dlss_g.dll>` (512 guard combinations and the disk contract), `gpu` (real Vulkan compute scaling; needs `VULKAN_SDK`). 2026-10-02: `carrier` (49), and `racehelper`, which only builds the test-only in-game race add-on. |

The relay's arm/copy/park/release chain has no offline host: it needs a real
DLSS-G evaluate. It was validated in the game (below).

## Measurements (Arknights: Endfield, 5120x2160, two NR passes, fixed 6x, 120 Hz)

PresentMon 2.5.1 with `--track_pc_latency --track_app_timing --track_frame_type`,
game in the foreground, 40 s per run, first 3 s dropped. "Real frames" are
PresentMon simulation marks. This is not a controlled benchmark: the serial run
is from 2026-09-28, the others from one play session on 2026-09-29, and the
scenes are not guaranteed to be identical.

| | serial feed | relay, no throttle | **relay + throttle** | NR off |
| --- | --- | --- | --- | --- |
| presents / s | 106.8 | 119.3 | **125.0** | 269.0 |
| displayed / s | 59.5 | 111.4 | **112.7** | 114.9 |
| real frames / s | 21.7 | 19.9 | **20.9** | 44.8 |
| display interval p50 / p95 (ms) | 16.66 / 33.33 | 8.34 / 16.66 | **8.33 / 16.66** | 8.33 / 16.65 |
| display intervals >= 25 ms | 22.4 % | 0 % | **0.02 %** | 0.02 % |
| PC latency p50 / p95 (ms) | 120.8 / 202.0 | 245.8 / 268.5 | **157.9 / 181.4** | 81.1 / 101.9 |

* The relay removes the stutter: nearly every present reaches the 120 Hz screen.
* Without the throttle the median latency is ~125 ms above the serial feed. A
  driver frame cap below the GPU's throughput brought it down to 132 ms (at
  16.7 real frames/s), which showed that it is a queue in front of NR.
* With the throttle the median latency is still ~37 ms above the serial feed,
  while its p95 is ~21 ms lower. The serial feed's lower median comes from the
  game itself waiting on the parked frame; the price was the stutter.
* The GPU is full: NR costs 26.5-27.9 ms per real frame (two passes), about the
  difference between 20.9 and 44.8 real frames per second.

## Known limits

* UNORM8 sRGB FG inputs only; other formats are refused with a log line and the
  game keeps its own image. The valid regions of the HUD-less image and the back
  buffer must have the same size. The game must supply `DLSSG.HUDLess`.
* Any Generic build is tried; only 8.5.0-rc10 has been verified.
* Only `Pipeline=2` with `Import=1` is validated in the game. `Pipeline=3` keeps
  the older CPU gate and ran with about two thirds of the FG groups unarmed and
  occasional release-submit failures (host fallback). `Import=0` keeps the CPU
  gate as well; in the game it left roughly a quarter to a half of the frames
  unarmed.
* The focus gate (update item 5) depends on the Streamline build. Its contract
  was found in `sl.dlss_g.dll` 2.14.1. The 2.10.3 build that Arknights: Endfield
  ships has no matching call site (`run.cmd focus` reports `matches=0`), so the
  bridge logs `[focus-fg] unavailable: unique Streamline focus-gate contract was
  not found; native focus behaviour retained` and Streamline stops DLSS-G, and
  NR with it, while the window is not focused. The composition guard (item 6)
  only shows while FG is evaluated, so it stays hidden then. Both builds were
  run in the game on 2026-10-02; NR worked with both while focused.
* One effect runtime / one swapchain.
* Generic's layer count and per-layer parameters are global, so the mirror
  (Upscaled) and this feed (Present) cannot run different layer configurations.
* Other Vulkan games are not covered. The automatic rebuild after a lost GPU
  completion (update item 4) has not been triggered in the game yet. One 1.5 s
  completion stall was seen once in the game during repeated resolution
  switches, before that rebuild existed; its cause is not known.

## Minimal integration for maintainers

If you prefer not to take the whole change, the essential pieces are, in order:

1. The serial feed: `fg-input.inc`, the feature-11 hook in `ForwardVkEvaluate`,
   the `fg_arm` branch in `VkmWorkerProc`, the identity carrier
   (`present-adapter.inc`, `present-nr-witness.asm`), and the two threading
   rules (no device wait on the FG thread; a frame without an NR entry is not a
   failure).
2. The relay, for the stutter: `fg-relay.inc` with the GPU wait on the previous
   `nr_val` at arm time, and `Synth12EvaluateShared(..., wait_completion=false)`.
3. The extension injection, for `Import=1`.
4. The throttle at `slReflexSleep`, for the latency.
5. Independently of all of the above: `ngx-module-scan.h`.
