# Merge kit for unified-20261015q

This folder lets you bring the changes of release unified-20261015q into an existing checkout of the bridge, of RenoDX DLSS 5 Generic or of MFG Unlock, without taking over the whole production tree. The complete tree is in the release asset `unified-20261015q-source.zip`; what each change does and how it was verified is in sections 3 and 5 of the repository README.

## Contents

| Path | Content |
| --- | --- |
| `bridge/bridge-since-vk-fgrelay-20261005.patch` | bridge changes on top of Yukikaze20170315/dlss5-bridge `835c5bf` (the 2026-10-05 release of this repository) |
| `bridge/bridge-since-NIGos-main-d1cc508.patch` | all bridge changes of this repository on top of NIGos/dlss5-bridge `main` at `d1cc508` (2026-10-05 release plus this release) |
| `bridge/changed-files/` | the current version of every file that the first bridge patch touches |
| `generic/generic-since-rc10-stages1-c3dc63c.patch` | Generic changes on top of `rc10-stages1` at `c3dc63c` (the Generic of the 2026-10-05 release) |
| `generic/generic-since-v8.5.0-rc10-9bb6c0f.patch` | all Generic changes of this repository on top of v8.5.0-rc10 at `9bb6c0f` (the `rc10-stages1` commits plus this release) |
| `generic/changed-files/` | the current version of every file that the first Generic patch touches |
| `mfg/mfg-since-MFGAmpereUnlock-0.11.patch` | MFG Unlock changes on top of nefh/MFGAmpereUnlock-RenoDx tag `0.11` (commit `53b3770e`) |
| `mfg/changed-files/` | the current version of every file that the MFG patch touches |
| `common/` | the shared headers, all new in this release |
| `kit-report.json` | machine-readable list of modified and added files per patch |

## Paths and line endings

- Bridge patches use the repository layout of dlss5-bridge (`src/…`, `tests/…`). Generic patches use the layout of the Generic folder (`dlssnr.hpp`, `ui/lang/…`), which is `src/addons/dlss5/` inside a RenoDX checkout. The MFG patch uses the production layout: the upstream folder `src/addons/mfgunlock/` is called `addon/`, and the new files `tests/native-policy.cpp` and `version.rc` sit next to it. Apply it with `git apply` in a working folder that contains a copy of the upstream `src/addons/mfgunlock/` renamed to `addon/` (this is what the check below does), then copy the results back.
- Both sides were normalised to LF line endings before the diff. The files in `changed-files/` and `common/` are exact copies of the production tree.
- The shared headers are included by relative path: from `bridge/src/` as `../../common/…`, from `generic/src/addons/dlss5/` as `../../../../common/…`. In the production tree all four components sit side by side (`bridge/`, `generic/`, `mfg/`, `common/`). In another layout, place `common/` so that these paths resolve, or change the include lines.

## Verification

`tools/release-unified-20261015q/check-patches.py` applied each patch with `git apply` to a fresh copy of its base (taken with `git archive` or from the official MFG Unlock 0.11 tarball) and compared every resulting file with the production tree:

| Patch | Files compared | Result |
| --- | ---: | --- |
| `bridge-since-vk-fgrelay-20261005.patch` | 74 | identical |
| `bridge-since-NIGos-main-d1cc508.patch` | 74 | identical |
| `generic-since-rc10-stages1-c3dc63c.patch` | 97 | identical |
| `generic-since-v8.5.0-rc10-9bb6c0f.patch` | 97 | identical |
| `mfg-since-MFGAmpereUnlock-0.11.patch` | 32 | identical |

## Files changed, by component

Bridge, relative to `835c5bf`:

- modified: `src/bridge.inc`, `src/build.cmd`, `src/dlss5-bridge.cpp`, `src/fg-input.inc`, `src/fg-relay.inc`, `src/fg-trace.inc`, `src/focus-fg.inc`, `src/present-adapter-config.h`, `src/present-adapter.inc`, `src/synth.inc`, `src/version.rc`, `src/vk-present-adapter.ini.example`, `src/vkmirror.inc`, `tests/vulkan-fg-quality/test-focus.cpp`
- added: `src/deploy-observer.inc` (module-load observer, Streamline `slInit` redirect and module checks), `src/deployment.inc` (state, health, safe mode, NR runtime selection, `DLSS5GetNrRuntimePathV2`), `src/deployment-panel.inc` (Deployment section, hook mode, driver maintenance, anomaly-log switch), `src/event-sink.h` (anomaly log writer, `DLSS5LogEventV1`), `src/fence-deadline-wait.h` (the fence wait of section 3.5), `src/generic-hook-mode.h` (client of Generic's `DLSS5HookModeControlV1`), `src/ngx-runtime-info.h` (NGX `FeatureCommonInfo` layout for the feature search path)

Generic, relative to `c3dc63c`:

- modified: `addon.cpp` (exports `DLSS5HookModeControlV1`), `dlssnr.hpp` (runtime path from the bridge, panel language and advanced section, layer sections, layer trial, character defaults, log forwarding, log folder), `edit_trace.hpp`, `gpu_timers.hpp`, `lastgasp.hpp`, `norm_trace.hpp`, `screenshot.hpp` (log folder), `stage_plan.hpp`, `ui/lang/zh-Hans.hpp` (new strings)
- added: `layer_trial.hpp` (the 5-or-more-layer confirmation and rollback state)

MFG Unlock, relative to `0.11`:

- modified: `addon/addon.cpp` (Blackwell multiplier-only path, Legacy option, no automatic `LoadFromDllMain`, module pinning and `AddonInit` rescan, panel language and advanced section, anomaly-log forwarding), `addon/architecture.hpp` (Blackwell profile), `addon/framecount.hpp` (entry discovery through `slGetFeatureFunction`, early `slDLSSGSetOptions` install, bounded retry, unapplied-decision records), `addon/ngx_hook.hpp` (exited-thread check during enlistment), `addon/provider.hpp`, `addon/reshade_compat.hpp`, `addon/streamline_bridge.hpp`
- added: `addon/panel-locale.hpp`, `tests/native-policy.cpp` (policy fixture), `version.rc`

Shared (`common/`): `deployment-state.hpp`, `drs.hpp`, `drs-fg.hpp`, `durable.hpp`, `event-log.hpp`, `hook-mode.hpp`, `maintenance-command.hpp`, `panel-locale.hpp`, `payload.hpp` (generated by `tools/unified-20261011/make-payload.py`; holds the runtime file table and the accepted `sl.interposer.dll` hashes).

## Order

Generic and the bridge can be merged independently: each detects a missing or older partner through its versioned exports and reports it in its panel. For the full behaviour of this release (Present-stage NR, runtime selection, hook-mode control, shared anomaly log) all three add-ons need the versions of this release. MFG Unlock does not depend on the other two.
