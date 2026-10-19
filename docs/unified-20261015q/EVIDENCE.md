# Evidence for release unified-20261015q

The files are in the release asset `unified-20261015q-evidence.zip`; this index is also its `README.md`. Paths below are the paths inside the archive, which are the paths in the development project. `SHA256SUMS.txt` in the archive lists every file. Section numbers refer to the repository README.

PC Z is the development PC (RTX 5090, Windows 11). PC B is another user's PC (RTX 5090, Windows 10 22H2); its logs contain that user's Windows user name and folder names. Game logs (`ReShade.log`, `dlss5-bridge.log`, `dlss5-sl.log` and others) are copied unchanged. Intermediate builds are named unified1 to unified1q; the README says which build each result belongs to.

## unified1q in game (section 5.1)

| Folder | Content |
| --- | --- |
| `evidence/unified-20261011/run-20261016-0006-1q-z-fullmode/` | PC Z, three sessions of 2026-10-15/16 with unified1q: first start after a clean reinstall, then the sessions with the full anomaly log. `ReShade.log`, `dlss5_log/` (bridge, observe, Streamline and anomaly logs), `state/`, `ReShade.ini`, `dlss5-bridge.cfg`, `SHA256SUMS.txt` |
| `evidence/unified-20261011/b-20261016-1q-framegraph/` | PC B, unified1q: game and bridge logs of the sessions with the saw-tooth frame-time curve (`raw/`, `run2-pid31072/`), driver-profile dumps of PCs B and Z (`drs-*.txt`), PresentMon captures (section 5.4). The environment listing of PC B is left out because it lists that user's installed software. |

## Earlier builds in game

| Folder | Content | README section |
| --- | --- | --- |
| `evidence/mfg-blackwell-codex-20261011/bw3-six-x-20261011-105429-465/` | first 6x on RTX 5090 with the Blackwell multiplier path (MFG build bw3, bridge with the observer hook removed), with `analysis.json` | 3.3 |
| `evidence/unified-20261011/run-20261011-151829-054468/` | multiplier takeover on first start after a full reinstall, with the NGX feature-path fix (build 1h); both NGX contexts on DLSS-G 310.9.1; includes the recovery `result.json` of the preceding removal | 3.2, 3.3, 3.7 |
| `evidence/unified-20261011/friend-20261014-mfg-not-effective/` | PC B with build 1p: hook retry after `error=5`, and the unexplained fallbacks to 2x | 3.3, 6 |
| `evidence/unified-20261011/run-20261012-1538-1o-launch-fail/` | start-up crash `DEP at MFG+0x31183` (build 1o) | 3.3 |
| `evidence/unified-20261011/run-20261011-205058-458533/` | flicker after returning from a high layer count (before the wait fix) | 3.5 |
| `evidence/unified-20261011/root-cause-20261011/` | structured analyses: NR rollback timeline (`nr-rollback-audit.json`), the PQ wait failure, the cached `slDLSSGSetOptions` pointer in `UnityPlayer.dll` (`unity-cached-setoptions.json`), historical VRAM | 3.3, 3.5 |
| `evidence/unified-20261011/run-20261012-0125-fix2-verify/` | the wait fix in game: layer replay 1→4→2→3→1, waits that ran their full 900 ms and recovered | 3.5 |
| `evidence/unified-20261011/layers-20261011-132704-299349/`, `layers-20261011-133539-634381/` | layer confirmation: timeout with the menu closed (15.040 s), and "Keep" | 3.6 |
| `evidence/unified-20261011/run-20261011-122003-635237/`, `run-20261011-122232-037323/` | safe mode entered, and left by hand with all settings unchanged (`settings-comparison.json`) | 3.6 |
| `evidence/unified-20261011/startup-20261011-132843-821/`, `startup-20261011-133243-332/`, `run-20261011-133733-295355/` | Streamline switch to the game folder and back, start-up state sequence, `final-verification.json` | 3.2 |
| `evidence/unified-20261011/clean-after-repair-20261011/` | hashes of the game's Streamline/NGX files after removal and launcher repair (`verification.json`) | 3.7 |

## Frame pacing captures (section 5.2)

Each `pacing-baseline-*` folder holds the PresentMon CSV, the foreground record, configuration snapshots before and after, a log snapshot, hashes, the conditions and `stable-foreground-summary.json`.

| Folder | Condition |
| --- | --- |
| `evidence/pacing-baseline-20261011-185902-a3721a08/` | 5120×2160, 6x, Throttle 2 |
| `evidence/pacing-baseline-20261011-190254-407eea86/` | 5120×2160, 6x, Throttle 0 |
| `evidence/pacing-baseline-20261011-190447-d7fe0833/` | 5120×2160, 2x, Throttle 0 |
| `evidence/pacing-baseline-20261011-190831-8bbd4a02/` | 5120×2160, 2x, Throttle 2 |
| `evidence/throttle-ab-20261011/` | the configuration changes of those four runs, `configuration-and-runtime-audit.json`, `analysis/comparison.json` and `comparison.csv` |
| `evidence/pacing-baseline-20261011-164510-9d1aaacd/` | 3840×2160, real 4x, Throttle 1 |
| `evidence/pacing-baseline-20261011-164719-c4483cd0/` | 3840×2160, real 2x, Throttle 1 (the stutter) |
| `evidence/pacing-baseline-20261011-165132-9cd46b84/` | 2x with the Present stage off (Independent Flip; user screenshot of the saw-tooth curve) |
| `evidence/pacing-baseline-20261011-165350-9b36c42e/` | 2x with the Present stage off and desktop composition held (user screenshot of the smooth curve) |
| `evidence/pacing-baseline-20261011-165938-c2e60bf1/` | 3840×2160, real 2x, Throttle 0 |
| `evidence/pacing-baseline-20261011-174209-c177cf6b/` | 5120×2160, 6x, Throttle 2 |

## Offline fixtures

| Path | Content | README section |
| --- | --- | --- |
| `offline-rerun/` | logs of the re-run on 2026-10-19 against the production source and the shipped bridge, `summary.json` with the exit codes, and `clean-recovery-ps51-elevated-entry.log` (the recovery fixture with the opt-in elevated entry-point check) | 5.3 |
| `builds/unified-20261011/package-1q-recovery-fix/` | the recovery fix of 2026-10-19: diffs of the recovery script, menu, usage document and fixture; the read-only plan of PC Z's real game folder with the fixed script (`live-plan.json`); the in-place package update check (`package-verification.json`) | 1, 3.7 |
| `builds/unified-20261011/cmd-flow-29871203a5ee445fbed4c321477a872e/` | the recovery menu flow test: the menu with a stub script, piped answers, outputs and call logs | 3.7 |
| `builds/unified-20261011/mfg-unload-pin1p/race-fixture/` | the unload race fixture, 5 runs each for the build before and after the fix, six-cycle runs, `race-summary.json` | 3.3 |
| `builds/unified-20261011/continuation1f/` | build, driver-profile, state/loader, deployment and recovery check logs of build 1f, Defender scan results | 3.6, 3.7 |
| `builds/unified-20261011/recovery/` | recovery script checks and Defender scan of build 1e | 3.7 |
| `builds/unified-20261011/hook-ui-1g/` | build 1g: hook-mode panel and one-click Streamline selection checks | 3.8 |
| `builds/unified-20261011/ngx-path-1h/` | build 1h: NGX feature-path regression | 3.2 |
| `builds/unified-20261011/package-1q/package-verification.json` | the packaging check of unified1q | 1 |
| `builds/unified-20261011/unified1q-generic-build.log`, `builds/unified-20261011/pq-wait-fix2-build.log` | build logs | 3.5 |

## Historical cross-PC results (section 7)

`evidence/machines-20261010/README.md` describes the data of PCs A and C. Their raw data (about 17 GB, mostly traces) is not included.
