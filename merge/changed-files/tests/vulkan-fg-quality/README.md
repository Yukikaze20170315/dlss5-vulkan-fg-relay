# Offline tests for the 2026-10-01 and 2026-10-02 updates

Run from `cmd` or PowerShell on Windows with MSVC installed
(`tests/vulkan-fg-input/vcvars.cmd` finds it):

```
tests\vulkan-fg-quality\run.cmd SUITE <new-output-directory> [ARGUMENT]
```

Every run builds into a NEW directory and refuses an existing one. No game is
started and nothing is installed.

From Git Bash, start it as `env -u NoDefaultCurrentDirectoryInExePath cmd //c ...`:
Git Bash sets that variable, and `cmd` then runs Git's own `test.exe` instead of
the test it just built (empty `run.txt`, exit code 1).

| Suite | What it checks | Needs | Pass (last line of `run.txt`) |
| --- | --- | --- | --- |
| `registry` | FG/NGX handle table: more than 8/32 handles, 10000 address reuses, failed and late releases, cross-API addresses, module retirement, 12 threads | MSVC | `registry: 188780 checks, 0 failures` |
| `guides` | Guide sub-rectangles, motion-vector scale, recording and reset helpers of `fg-guides.inc` | MSVC | `PASS: 57 checks; ...` |
| `hooks` | Production NGX wrappers with CPU stubs: nested Create/Evaluate/Release handled once, 96 live FG instances, release while another thread holds the NGX lock, worker stop and event retirement | MSVC | `hooks: 535 checks, 0 failures` |
| `colour` | FG input sub-rectangles (fullscreen below monitor resolution, non-zero origins) and the recorded production copies | MSVC | `colour regions: 39 checks, 0 failures, ...` |
| `recovery` | Automatic rebuild after lost completion, with a real D3D12 queue marker (Vulkan completion is simulated) | MSVC, a D3D12 GPU | `recovery/history: 34 checks, 0 failures` |
| `compatibility` | No hash whitelist: an unknown Generic build is tried, configuration problems pause and resume, NR off is not reported as incompatible | MSVC | `PASS compatibility: checks=26 failures=0` |
| `carrier` | SR carrier retry: production carrier search and MinHook install against fixture DLLs (no SR module, Ray Reconstruction only, two SR modules at once, one again), retry interval and limit, immediate refusal for a non-transient MinHook status, no relay arm while pending, frames reach the retry instead of the relay while pending, session release clears the state | MSVC | `PASS carrier: checks=49 failures=0` |
| `carrier-range` | Fixed low-address SR fixture with its inner ±1.25 GiB reserved: ordinary allocation still fails, the opt-in carrier installs outside ±1 GiB, external SR forwards unchanged, disable/re-enable works, existing or intervening jumps are rejected without modifying the target | MSVC, x64 | `PASS crowded carrier: checks=19 failures=0` |
| `minhook-range` | Nearby allocation preference, four-argument forwarding, decommit/retire/address reuse without reusing published trampolines, complete rel32 address-space exhaustion, exact signed-32-bit RIP displacement boundaries and overflow refusal | MSVC, x64 | `PASS MinHook range safety: checks=34 failures=0` |
| `racehelper` | Builds only: `carrier-race-helper.addon64`, a test-only ReShade add-on that recreates the SR carrier race in a game (see its source header). Remove it from the game folder after the test | MSVC | `built carrier-race-helper.addon64 ...` |
| `guard` | Composition guard with real Win32 windows: styles, alpha 1/255, not excluded from capture by default and `HoldCompositionHideCapture` applied live, foreground unchanged, click-through, follows the monitor, hides on FG stop / minimise / NR off / `HoldComposition=0`. The two foreground checks fail if another program takes the foreground during the run; the log prints the foreground window at each stage | MSVC, a desktop session left alone for about 15 s | `guard: 30 checks, 0 failures` |
| `focus` | Focus gate: 512 guard combinations, exact return-address scope, explicit Off/free pass-through, and the instruction contract found in the given `sl.dlss_g.dll` (read from disk, not loaded) | MSVC, the game's `sl.dlss_g.dll` path as ARGUMENT | `focus FG tests PASS` |
| `gpu` | Guide scaling on a real Vulkan device, on graphics and compute-only queue families, bit-exact against a CPU reference, plus D3D12 shared-texture round trips | MSVC, `VULKAN_SDK`, NVIDIA GPU | `PASS GPU guide compute/readback checks=402 failures=0 ...` (`gpu <dir> colour-only` runs only the sub-rectangle copies) |

These tests cover the logic listed above. They do not show picture quality,
frame pacing in a game, or that a particular game's FG path behaves the same way.
