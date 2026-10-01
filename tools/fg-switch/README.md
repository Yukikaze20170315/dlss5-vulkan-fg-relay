# EndfieldFGSwitch

A small Win32 tool for Arknights: Endfield. It switches the game folder between two sets of 8 NVIDIA files and sets the frame generation multiplier in the game's NVIDIA driver profile. How to use it is in `docs/INSTALL.md` ("Optional: EndfieldFGSwitch"); this file is for people who want to check or rebuild it.

| Set | Files | Source |
| --- | --- | --- |
| 0, original | `sl.common.dll`, `sl.deepdvc.dll`, `sl.dlss.dll`, `sl.dlss_d.dll`, `sl.dlss_g.dll`, `sl.pcl.dll`, `sl.reflex.dll` (Streamline 2.10.3), `nvngx_dlssg.dll` (310.5.2) | the game's own files |
| 1, upgraded | the same names, Streamline 2.14.1 and `nvngx_dlssg.dll` 310.9.1 | NVIDIA Streamline SDK v2.14.1, `bin/x64` |

The SHA-256 of all 16 files is in `core.h` (`kFiles`). The game's `sl.interposer.dll` is never touched; it exports `HGSetupCustomVulkan`, which NVIDIA's generic build does not have.

## What it does

- **Switch** (`SwitchFiles` in `core.h`). Refuses a folder without `Endfield.exe` and `sl.interposer.dll`, refuses while `Endfield.exe` runs, and refuses if any of the 8 files matches neither set. If the folder already has the target set, nothing is written. Otherwise the current 8 files are copied to `%LOCALAPPDATA%\EndfieldFGSwitch\backup-<time>`; each new file is written as `<name>.fgswitch-new`, read back and hashed, then moved over the original with `MoveFileExW(MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`; at the end the whole folder is hashed again. A folder with a mix of both sets is completed to the target set.
- **Multiplier** (`SetMultiplier`). Finds the existing driver profile of `Endfield.exe` with `NvAPI_DRS_FindApplicationByName` (no profile is created and the global profile is not changed). For multiplier N it sets `NGX_DLSSG_MODE_ID` (0x10308298) = 2 and `NGX_DLSSG_MULTI_FRAME_COUNT_ID` (0x104D6667) = N - 1; for "Restore" it deletes both settings so that they are inherited again. It then calls `NvAPI_DRS_SaveSettings` and reads the values back.
- **Cards** (`ReadGpu`). `NvAPI_GPU_GetArchInfo` >= `NV_GPU_ARCHITECTURE_GB200`: RTX 50, multipliers 2-6, or 2-4 with a driver older than 595.97. >= `NV_GPU_ARCHITECTURE_AD100`: RTX 40, files only (the multiplier stays with the game). Older or no NVIDIA GPU: only "Restore".
- **Log**: `%LOCALAPPDATA%\EndfieldFGSwitch\fg-switch.log`. The chosen game folder is remembered in `folder.txt` there.

## Build

`build.cmd` (MSVC 2022, `/W4`). It needs:

- NVAPI from https://github.com/NVIDIA/nvapi (built with commit `87dca62`); `build.cmd` links `..\..\upstream\NVIDIA-nvapi-87dca62\amd64\nvapi64.lib` and `core.h` includes `nvapi.h` from there. Adjust both paths to your checkout.
- The 16 files, which are not in this repository. `fg-switch.rc` reads them from `..\..\evidence\sl-compat-20261002\sl-2.10.3\` and `...\sl-2.14.1\`. Put the files from the sources in the table above there, or change the paths. The build does not check them; `EndfieldFGSwitch.exe --self-test` does.

The published `EndfieldFGSwitch.exe` (SHA-256 `2AEB7F4703F3391835EBBAE2CC209A2E5FC679B12478CDEDF1B90DC5451A31A4`) was built this way. MSVC writes a time stamp into the file, so a rebuild has a different hash.

## Test modes

The program has no console; each mode writes `%LOCALAPPDATA%\EndfieldFGSwitch\cli-output.txt`, which `cli.sh` prints.

| Command | What it does |
| --- | --- |
| `--self-test` | hashes the 16 embedded files against `core.h` |
| `--state DIR` | prints which set each file in DIR belongs to (0, 1, -1 missing, -2 unknown) |
| `--switch DIR 0\|1` | the file switch only, without the driver |
| `--multiplier N` | the driver setting only; 0 restores |

`test-files.sh NEW_DIR` runs 15 file-switch checks on a scratch folder (never the real game folder; it stops if the game is running). It uses a copy of `PING.EXE` named `Endfield.exe` to test the running-game refusal.
