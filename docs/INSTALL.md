# dlss5-bridge 1.4.13-pre8-vk-fgrelay: install

This build runs RenoDX DLSS 5 Generic's neural rendering on the finished real
frame right before the game's native DLSS Frame Generation, in Vulkan games.
Source: https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay

## Requirements

* A Vulkan game with native DLSS Frame Generation (Streamline) that supplies a
  HUD-less image, with 8-bit sRGB frame generation inputs.
* ReShade 6.8 with add-on support.
* RenoDX DLSS 5 Generic **8.5.0-rc10**, unmodified
  (`renodx-dlss5.addon64`, SHA-256
  `DCD93881E976AD033D83C2BB01F4BC3E4DDC59C15FE0DD4CA165BC5FC7D1AC68`). Other
  Generic builds are refused with a log line.
* An NVIDIA RTX GPU.

## Install

1. Close the game.
2. Keep a copy of your current `dlss5-bridge.addon64` (and
   `vk-present-adapter.ini`, if you have one).
3. Copy `dlss5-bridge.addon64` and `vk-present-adapter.ini` from this package
   into the folder where your ReShade add-ons are (next to the game's `.exe`).
4. In Generic's settings (`ReShade.ini`, section `[RenoDX.DLSS5]`) keep
   `EnableHooks=1`, and keep "Source encoding", "Source primaries" and
   "Linear unit (nits)" at Auto. HDR mode "Auto" is recommended.
5. Start the game and choose **Present** in Generic's hook-point control.
   Choosing Upscaled or Render switches back to the normal Vulkan path.

Check `dlss5-bridge.log`: `[fg-relay] open.` followed by
`[fg-relay] rate/15.0s` lines with a non-zero `armed` while frame generation
runs.

## Rollback

Close the game and put your previous `dlss5-bridge.addon64` back, or set
`Enabled=0` in `vk-present-adapter.ini`.

## Settings in `vk-present-adapter.ini`

| Key | Value here | Meaning |
| --- | --- | --- |
| `Enabled` | 1 | Turns this path on (read at start). |
| `Source` | fg-input | Process the real frame at the frame generation input. |
| `Follow` | 1 | Generic's hook-point control switches between this path (Present) and the normal Vulkan path. |
| `Pipeline` | 2 | GPU relay. 0 is the older serial path. |
| `Import` | 1 | GPU-side hand-over between Vulkan and D3D12 (adds one Vulkan device extension at device creation). |
| `Throttle` | 1 | Holds the game after its Reflex sleep until the newest frame's NR is done, to keep latency down. 0 turns it off. |
| `Trace` | 0 | 1 adds diagnostic observers to the log. |

Moving "Source encoding", "Source primaries" or "Linear unit (nits)" away from
Auto stops NR until the game is restarted (known issue).
