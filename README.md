# Vulkan + DLSS Frame Generation: Generic's NR on the finished frame — update 2026-10-02

## Short version

- **What it is.** An update of the DLSS 5 bridge add-on (`dlss5-bridge.addon64`) released here on 2026-09-29 (`vk-fgrelay-20260929`). In Vulkan games with native DLSS Frame Generation, it runs RenoDX DLSS 5 Generic's neural rendering (NR) on the game's finished real frame, right before the native DLSS-G evaluate, once per real frame; frame generation then interpolates the enhanced frames. Generic itself is not modified.
- **What was wrong.** After the first release, these problems showed up in normal play in Arknights: Endfield. Items 1-3, 6 and 7 were bridge bugs. Item 4 is how the game behaves without any mod; item 5 appeared once item 4 was changed.
  1. With any DLSS mode other than DLAA, surfaces flickered when the camera turned, and frame pacing became uneven and stayed uneven until the game was restarted.
  2. In fullscreen at a resolution below the monitor's (for example 3840x2160 on a 5120x2160 monitor), NR did not run.
  3. NR could stop by itself after a while of normal play and only came back after a restart.
  4. When the game window lost focus, frame generation (and with it NR) stopped.
  5. When frame generation was kept running while unfocused, switching windows showed a short flash of the unprocessed picture about 1-2 s later.
  6. Only one exact Generic build was accepted; with any other build NR did not run, and only the log said why.
  7. In some game sessions NR did not start at all. Frame generation ran normally, and changing the resolution once brought NR back.
- **What this update does, in one line each.**
  1. The smaller depth and motion-vector images of the other DLSS modes are scaled to the output size on the GPU, and the relay no longer falls back for the rest of the session.
  2. Each input image is read at its own valid region, so fullscreen below monitor resolution works.
  3. Frame generation handles are tracked properly (the old table had 8 entries and was never cleaned).
  4. Streamline's internal focus check is overridden for frame generation only, while the window is visible and NR is on. This needs Streamline 2.14.1; with the 2.10.3 files that Arknights: Endfield ships, the game's own behaviour stays (see the tool below).
  5. A 2x2, almost transparent, click-through window keeps Windows from switching the game's presentation mode; that switch made Streamline show 9-15 frames without calling frame generation, so without NR.
  6. The version whitelist is removed; real problems are named in the log and in the ReShade panel, together with the Generic build that is known to work.
  7. At game start the bridge attaches to the DLSS Super Resolution module. When this cannot work yet, because two such modules are loaded for a moment or there is no free address space next to the module for the hook (this has nothing to do with how much RAM the PC has), the bridge tries again once per second, up to 30 times, instead of giving up for the whole session.
  Also new: if the GPU stops confirming NR work, the bridge rebuilds its session automatically once it can prove the old work is finished.
- **New, optional, for Arknights: Endfield only: `EndfieldFGSwitch.exe`.** The game ships NVIDIA Streamline 2.10.3, which allows at most 4x frame generation and stops frame generation when the window is not focused. The tool swaps the game's 8 Streamline and frame generation files for NVIDIA's official Streamline 2.14.1 and frame generation 310.9.1 (and back), and on RTX 50 cards sets a fixed 2x-6x multiplier in the driver profile of this game only. It has two purposes: 6x frame generation on RTX 50 cards, and frame generation and NR that keep running behind other windows, without the flash of item 5. Users who need neither do not need it.
- **Graphics cards.** Supported: RTX 50 series. Maybe: RTX 40 series (frame generation 2x works there, but the NR runtime build used here only contains RTX 50 code; an RTX 40 build of the NR runtime from the community may work, untested). Needs own adaptation for now: RTX 20 and RTX 30 series (no DLSS Frame Generation, which this add-on depends on).
- **Tested** in Arknights: Endfield on an RTX 5090 (5120x2160 main monitor plus a 3840x2160 second monitor), Generic 8.5.0-rc10 with two NR passes. Fixes 1-5 were confirmed by the user in the game, with the game's 4x frame generation and Streamline 2.14.1 files in the game folder. Fix 6 is covered by offline tests only; no other Generic build was tried in the game. Fix 7 was checked in the game with a test-only add-on that recreates the condition on purpose: NR started in each of 4 launches without a resolution change. The published build was run with both Streamline versions, and the tool at 4x, 5x and 6x. Offline tests are listed below. Not tested in other games or on other cards.
- **Use it or merge it.** A ready-to-use build, the tool, patches on top of the 2026-09-29 patches, full copies of every changed file, the offline tests, and the logs and captures behind every number are linked in "Files" below.

A step-by-step install guide in English and Chinese follows. Everything after the guide is technical detail.

## Install guide / 安装教程

**DLSS 5 neural rendering for Arknights: Endfield (Vulkan), build `vk-fgrelay-20261002`**
**《明日方舟：终末地》Vulkan 版 DLSS 5 神经渲染，版本 `vk-fgrelay-20261002`**

### What this does / 这是什么

"Neural rendering" (NR) is an AI filter that redraws the game's picture with more detailed lighting and materials. "RenoDX DLSS 5 Generic" (Generic for short) is a free add-on that applies it. This package lets Generic work in Arknights: Endfield when the game runs on Vulkan with the game's own "DLSS Frame Generation" turned on. It needs three other free downloads, listed below. It does not change any game file.

“神经渲染”（NR）是一种 AI 画面滤镜，会用更细致的光影和材质重新绘制游戏画面。“RenoDX DLSS 5 Generic”（简称 Generic）是提供这种效果的免费插件。本插件包让 Generic 在《终末地》使用 Vulkan、并开启游戏自带的“DLSS 帧生成”时正常工作。它还需要下面列出的另外三个免费下载项，不会修改任何游戏文件。

There is also an optional tool, `EndfieldFGSwitch.exe`. It allows 6x frame generation on RTX 50 cards and keeps frame generation and NR running when you switch to another window. It does change game files. See "Optional: EndfieldFGSwitch" below; you do not need it to use NR.

另外还有一个可选工具 `EndfieldFGSwitch.exe`。它能让 RTX 50 显卡使用 6 倍帧生成，并在你切到其他窗口时让帧生成和神经渲染继续运行。它会改动游戏文件。见下文“可选：EndfieldFGSwitch”；不用它也能使用神经渲染。

### Please read first / 请先阅读

- **Account risk / 账号风险:** Arknights: Endfield is an online game with anti-cheat software, and this setup loads third-party software (ReShade with add-ons) into the game. We do not know whether this can affect your account. Decide for yourself; you use it at your own risk.
  《终末地》是带反作弊软件的联网游戏，而这套方案会把第三方软件（带插件的 ReShade）加载进游戏。我们不知道这是否会影响账号。请自行判断，风险自负。
- **Graphics card / 显卡:** NVIDIA GeForce RTX 50 series. RTX 40 series maybe; RTX 20 and RTX 30 series not without your own adaptation. See "Which graphics cards work" below.
  NVIDIA GeForce RTX 50 系列。RTX 40 系列可能可以；RTX 20、RTX 30 系列需要自行适配。见下文“支持哪些显卡”。
- **Performance / 性能:** NR is heavy. In our test (RTX 5090, 5120x2160, two NR passes) the game's real frame rate, before frame generation adds frames, dropped to about half.
  神经渲染很吃性能。我们的测试中（RTX 5090、5120×2160、两层神经渲染），游戏的真实帧率（帧生成插帧之前的帧率）降到了大约一半。
- **System / 系统:** Windows 10 or 11, 64-bit. / Windows 10 或 11，64 位。

### Which graphics cards work / 支持哪些显卡

- **Supported: NVIDIA GeForce RTX 50 series.** Tested on an RTX 5090 with NVIDIA driver 616.92. With the game's own files, frame generation goes up to 4x. With the optional tool below it goes up to 6x (this needs NVIDIA driver 595.97 or newer).
  **确定支持：NVIDIA GeForce RTX 50 系列。** 在 RTX 5090、NVIDIA 驱动 616.92 上测试过。使用游戏自带文件时，帧生成最高 4 倍；使用下文的可选工具最高 6 倍（需要 NVIDIA 驱动 595.97 或更新版本）。
- **Maybe: NVIDIA GeForce RTX 40 series.** RTX 40 cards have the game's DLSS Frame Generation (2x), which this add-on needs, and nothing in this add-on itself is made only for RTX 50. The problem is the NR runtime (download 3): it only contains code for RTX 50 cards and refuses to start on other cards. With the files listed below, NR therefore does not start on an RTX 40 card. It can work with a community-modified NR runtime that runs on RTX 40 cards, but nobody has tested this setup on an RTX 40 card yet. That is why it says "maybe". Expect a large drop in frame rate as well: NR is heavy even on an RTX 5090.
  **可能支持：NVIDIA GeForce RTX 40 系列。** RTX 40 显卡有本插件所需的游戏 DLSS 帧生成（2 倍），本插件本身也没有只为 RTX 50 设计的部分。问题出在神经渲染运行库（第 3 项）：它只包含 RTX 50 显卡用的代码，在其他显卡上会拒绝启动。所以照下面列出的文件安装，RTX 40 显卡上神经渲染不会启动。换用社区修改过、能在 RTX 40 上运行的神经渲染运行库，有可能可以用，但还没有人在 RTX 40 显卡上测试过这套方案，所以写作“可能”。另外帧率也会大幅下降：即使在 RTX 5090 上，神经渲染也很吃性能。
- **Needs your own adaptation for now: NVIDIA GeForce RTX 20 and RTX 30 series.** This add-on does its work at the moment when the game's DLSS Frame Generation makes new frames. RTX 20 and RTX 30 cards do not have DLSS Frame Generation, so the add-on has nothing to work with. The NR runtime does not run on these cards either. There are community tools that add frame generation to these cards, and NR runtimes modified for them, but they replace parts of the game's or the driver's DLSS files, and none of them has been tested with this add-on. If you try such a combination, you are on your own for now.
  **暂需自行适配：NVIDIA GeForce RTX 20、RTX 30 系列。** 本插件是在游戏的 DLSS 帧生成制作新画面的那一刻工作的。RTX 20、RTX 30 显卡没有 DLSS 帧生成，本插件就没有可以工作的地方。神经渲染运行库也不能在这些显卡上运行。社区里有给这些显卡加上帧生成的工具，也有为它们修改过的神经渲染运行库，但它们会替换游戏或驱动的部分 DLSS 文件，而且都没有和本插件一起测试过。如果你尝试这样的组合，目前只能靠自己。

#### Trying it on an RTX 40 card / 在 RTX 40 显卡上尝试

1. Install everything as described in the case below that fits you, but instead of `nvngx_dlssnr.dll` from download 3, use an `nvngx_dlssnr.dll` whose publisher says that it runs on RTX 40 cards. Such modified files are shared in the RenoDX community; its Discord server is linked at the top of https://github.com/clshortfuse/renodx. Only use a file from a source you trust: a `.dll` file runs inside the game.
   按下文适合你的情况安装，但不要用第 3 项的 `nvngx_dlssnr.dll`，而是换成发布者说明可以在 RTX 40 显卡上运行的 `nvngx_dlssnr.dll`。这类修改过的文件在 RenoDX 社区里分享；它的 Discord 服务器链接在 https://github.com/clshortfuse/renodx 页面顶部。只使用你信任的来源提供的文件：`.dll` 文件是在游戏里运行的。
2. In the game, set DLSS Frame Generation to 2x (RTX 40 cards only have 2x).
   在游戏里把 DLSS 帧生成设为 2 倍（RTX 40 显卡只有 2 倍）。
3. Check as in Case 1, step 8. If the picture changes, it works. If it does not change after about half a minute in the game, that NR runtime does not work on your card. Then close the game and move that `nvngx_dlssnr.dll` out of the game folder. The game still runs normally without it, only without NR.
   按情况一第 8 步检查。画面有变化就是可以用。如果进入游戏约半分钟后画面仍然没有变化，说明这个神经渲染运行库在你的显卡上不能用。这时关闭游戏，把这个 `nvngx_dlssnr.dll` 移出游戏文件夹。没有它游戏照常运行，只是没有神经渲染。
4. Please tell us the result, whether it works or not: your card model, where the NR runtime came from, and the two log files (see "Asking for help" below). Reports like this are how RTX 40 can move to "supported".
   无论成功与否，都请告诉我们结果：显卡型号、神经渲染运行库的来源，以及两个日志文件（见下文“求助时”）。有了这样的反馈，RTX 40 才能改为“确定支持”。

### Preparation / 准备工作

- **Show file extensions.** In File Explorer, Windows 11: "View" → "Show" → "File name extensions"; Windows 10: "View" tab → tick "File name extensions". Otherwise `ReShade.ini` and `ReShade.log` both look like "ReShade", and renaming a file can go wrong.
  **显示文件扩展名。** 在文件资源管理器中，Windows 11：“查看”→“显示”→“文件扩展名”；Windows 10：“查看”选项卡 → 勾选“文件扩展名”。否则 `ReShade.ini` 和 `ReShade.log` 看起来都叫“ReShade”，改名时也容易出错。
- **Turn on hardware-accelerated GPU scheduling.** DLSS Frame Generation needs it; without it the option is greyed out in the game. Windows Settings → System → Display → Graphics → "Change default graphics settings" (on Windows 10: "Graphics settings") → turn on "Hardware-accelerated GPU scheduling", then restart the PC.
  **打开“硬件加速 GPU 计划”。** DLSS 帧生成需要它，没打开时游戏里的帧生成选项是灰色的。Windows 设置 → 系统 → 屏幕 → 显示卡 →“更改默认图形设置”（Windows 10 为“图形设置”）→ 打开“硬件加速 GPU 计划”，然后重启电脑。
- **Find the game folder.** It is the folder that contains `Endfield.exe`. The usual location is `C:\Program Files\Hypergryph Launcher\games\Endfield Game`. If you are not sure: start the game, open Task Manager (Ctrl+Shift+Esc), right-click the game, and choose "Open file location".
  **找到游戏文件夹。** 就是 `Endfield.exe` 所在的文件夹，常见位置是 `C:\Program Files\Hypergryph Launcher\games\Endfield Game`。如果不确定：先启动游戏，打开任务管理器（Ctrl+Shift+Esc），右键游戏，选择“打开文件所在的位置”。
- **Administrator permission.** The game folder is usually protected by Windows. When Windows asks for permission while you copy, rename or replace files there, click "Continue".
  **管理员权限。** 游戏文件夹通常受 Windows 保护。在里面复制、改名或替换文件时，如果 Windows 要求权限，点“继续”。
- **Only change files while the game is closed.**
  **只在游戏关闭时改动文件。**

### Downloads / 需要下载的文件

On the GitHub pages (downloads 2 to 5), scroll down to "Assets" and click the file named below. Do not download "Source code"; that is not the add-on. After downloading, right-click each `.zip` file and choose "Extract All".

在 GitHub 页面（第 2～5 项）上，往下找到 “Assets” 一栏，点击下表写的那个文件。不要下载 “Source code”，那不是插件。下载后，右键每个 `.zip` 文件，选择“全部解压缩”。

| # | What / 内容 | Where / 下载位置 | Files you need / 需要的文件 |
| --- | --- | --- | --- |
| 1 | ReShade 6.8.0 with full add-on support / 带完整插件支持的 ReShade 6.8.0 | https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe (the same file as the button "Download ReShade 6.8.0 with full add-on support" on https://reshade.me / 与 https://reshade.me 上 “Download ReShade 6.8.0 with full add-on support” 按钮是同一个文件) | `ReShade_Setup_6.8.0_Addon.exe` (the installer / 安装程序) |
| 2 | Generic 8.5.0-rc10 | https://github.com/RankFTW/rhi-repo/releases/tag/renodx-dlss5-8.5.0-rc10 → `renodx-dlss5_8.5.0-rc10.zip` | `renodx-dlss5.addon64` |
| 3 | NR runtime 310.8.0 / 神经渲染运行库 310.8.0 | https://github.com/RankFTW/rhi-repo/releases/tag/dlssnr-310.8.0 → `nvngx_dlssnr_310.8.0.zip` | `nvngx_dlssnr.dll` |
| 4 | This add-on / 本插件 | https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/releases/tag/vk-fgrelay-20261002 → `dlss5-bridge-vk-fgrelay-20261002.zip` | `dlss5-bridge.addon64`, `vk-present-adapter.ini` |
| 5 | Optional: EndfieldFGSwitch / 可选：EndfieldFGSwitch | Same page as download 4 → `EndfieldFGSwitch.exe` / 与第 4 项同一页面 → `EndfieldFGSwitch.exe` | `EndfieldFGSwitch.exe` (one file, nothing to extract / 单个文件，无需解压) |

Downloads 2 and 3 are hosted by the RHI project; you do not need to install the RHI program. This package was tested with exactly these versions; the links above always give these versions, even after newer ones come out.

第 2、3 项由 RHI 项目托管，不需要安装 RHI 程序。本插件包只和上表这些版本一起测试过；即使以后出了新版本，上面的链接下载到的仍是这些版本。

### Case 1: you have never installed ReShade or any mod for this game / 情况一：从未给这个游戏装过 ReShade 或任何插件

1. **Switch the game to Vulkan.** Close the game. In the Hypergryph launcher, open the game's settings and choose Vulkan as the graphics mode. If you might want to go back later, write down the mode that was selected before.
   **把游戏切换到 Vulkan。** 关闭游戏。在鹰角启动器里打开游戏设置，把图形模式选为 Vulkan。如果以后可能想改回来，先记下原来选的是哪种模式。
2. **Install ReShade.** Run the installer from download 1. It shows a list of games; if Endfield is not in the list, click "Browse" and pick `Endfield.exe` in the game folder (not the launcher's `.exe`). When it asks which rendering API the game uses, choose **Vulkan**. If it offers effect packages, presets or add-ons, you do not need any of them for this; uncheck them all and finish.
   **安装 ReShade。** 运行第 1 项的安装程序。它会列出一些游戏；如果列表里没有终末地，点“Browse”（浏览），选择游戏文件夹里的 `Endfield.exe`（不要选启动器的 `.exe`）。问游戏使用哪种渲染 API 时，选择 **Vulkan**。如果它提供特效包、预设或插件让你勾选，这里一个都不需要，全部取消勾选，然后完成安装。
3. **Copy four files into the game folder** (next to `Endfield.exe`): `renodx-dlss5.addon64` (download 2), `nvngx_dlssnr.dll` (download 3), `dlss5-bridge.addon64` and `vk-present-adapter.ini` (download 4).
   **把四个文件复制到游戏文件夹**（和 `Endfield.exe` 放在一起）：`renodx-dlss5.addon64`（第 2 项）、`nvngx_dlssnr.dll`（第 3 项）、`dlss5-bridge.addon64` 和 `vk-present-adapter.ini`（第 4 项）。
4. **Start the game once, then close it.** At the top of the screen, a ReShade message should appear for a few seconds (if it does not, see "If something does not work" below). This first start also creates the settings file `ReShade.ini` in the game folder.
   **启动一次游戏，然后关闭。** 屏幕顶部应该会出现几秒钟的 ReShade 提示（如果没有出现，见下文“遇到问题时”）。这次启动还会在游戏文件夹里生成设置文件 `ReShade.ini`。
5. **Allow the add-on to work with frame generation.** Copy `ReShade.ini` from the game folder to your desktop and open the copy with Notepad. Press Ctrl+F and search for `RenoDX.DLSS5` to find the line `[RenoDX.DLSS5]`. Below it, find `EnableHooks=` and change the number after it to `1`. If that line is missing, add `EnableHooks=1` directly below `[RenoDX.DLSS5]`. If `[RenoDX.DLSS5]` is missing too, add these two lines at the end of the file:
   **允许插件配合帧生成工作。** 把游戏文件夹里的 `ReShade.ini` 复制到桌面，用记事本打开这个副本。按 Ctrl+F 搜索 `RenoDX.DLSS5`，找到 `[RenoDX.DLSS5]` 这一行，在它下面找到 `EnableHooks=`，把后面的数字改成 `1`。如果没有这一行，就在 `[RenoDX.DLSS5]` 正下方加一行 `EnableHooks=1`。如果连 `[RenoDX.DLSS5]` 也没有，就在文件末尾加上这两行：

   ```ini
   [RenoDX.DLSS5]
   EnableHooks=1
   ```

   Save, then copy the edited file back into the game folder and choose "Replace". (Editing a copy avoids "Access denied" when saving inside the protected game folder.)
   保存后，把改好的文件复制回游戏文件夹，选择“替换”。（先改副本，是为了避免直接在受保护的游戏文件夹里保存时提示“拒绝访问”。）
6. **Turn on frame generation in the game.** Start the game. In the game's graphics settings, set the upscaling option to DLSS (any mode: DLAA, Quality, Balanced or Performance) and turn on DLSS Frame Generation with any multiplier your card supports.
   **在游戏里打开帧生成。** 启动游戏，在游戏的画面设置中把超分辨率选项设为 DLSS（DLAA、质量、平衡、性能任一档位均可），并打开 DLSS 帧生成，倍数选显卡支持的任意一档。
7. **Choose "Present".** Press the **Home** key to open the ReShade menu (on some laptops: Fn+Home; the ReShade message at game start names the key if it is different). Open the tab **DLSS 5 Neural Rendering**. Make sure the switch **Neural Rendering** is on, and set **Hook point** to **Present**. We also recommend **HDR mode: Auto (recommended)**. Press Home again to close the menu. If your ReShade menu is in Chinese, the tab and the switch keep their English names; Hook point is called 介入点, Present is 呈现, HDR mode is HDR 模式, and Auto (recommended) is 自动（推荐）.
   **选择“呈现”。** 按 **Home** 键打开 ReShade 菜单（部分笔记本需要按 Fn+Home；如果按键不同，游戏启动时的 ReShade 提示会写出来）。打开 **DLSS 5 Neural Rendering** 选项卡，确认 **Neural Rendering** 开关已打开，把 **介入点**（Hook point）设为 **呈现**（Present）。同时建议把 **HDR 模式**（HDR mode）设为 **自动（推荐）**（Auto (recommended)）。再按一次 Home 关闭菜单。中文菜单里，这个选项卡和这个开关仍显示英文名。
8. **Check that it works.** Stand still in the game and switch **Neural Rendering** off and on a few times. With it on, lighting, shadows and material detail look clearly different from the game's own picture. Your settings are saved automatically.
   **确认已经生效。** 在游戏里站着不动，把 **Neural Rendering** 开关关、开几次。打开时，光影和材质细节应该和游戏原本的画面有明显不同。你的设置会自动保存。
9. **Optional:** if you want 6x frame generation or NR that keeps running behind other windows, see "Optional: EndfieldFGSwitch" below.
   **可选：** 如果想要 6 倍帧生成，或者想让切到其他窗口时神经渲染继续运行，见下文“可选：EndfieldFGSwitch”。

### Case 2: you used a DirectX 11 version of these mods before / 情况二：以前装过 DirectX 11 版本的这类插件

1. **Back up.** Close the game. Make a backup folder outside the game folder, for example on your desktop. Copy these from the game folder into it, if they exist: `d3d11.dll`, `d3d12.dll`, `dxgi.dll`, `nvngx_dlssnr.dll`, every file ending in `.addon64`, `ReShade.ini`, `ReShadePreset.ini`, and the folder `reshade-shaders`. Also write down which graphics mode the launcher uses now.
   **备份。** 关闭游戏。在游戏文件夹以外新建一个备份文件夹，例如放在桌面。把游戏文件夹里的这些东西（存在的话）复制进去：`d3d11.dll`、`d3d12.dll`、`dxgi.dll`、`nvngx_dlssnr.dll`、所有以 `.addon64` 结尾的文件、`ReShade.ini`、`ReShadePreset.ini`，以及 `reshade-shaders` 文件夹。同时记下启动器现在用的是哪种图形模式。
2. **Turn off the old DirectX installation.** In the game folder, rename `d3d11.dll` to `d3d11.dll.bak` and `d3d12.dll` to `d3d12.dll.bak`. Both were placed there by the old installation (Arknights: Endfield does not ship them). If there is also a `dxgi.dll`, right-click it → Properties → Details: if "Product name" says ReShade, rename it to `dxgi.dll.bak` in the same way. If a file does not exist, skip it. If a `.bak` file with the same name already exists, move the old `.bak` into your backup folder first. Never touch files with these names in `C:\Windows`.
   **停用旧的 DirectX 安装。** 在游戏文件夹里，把 `d3d11.dll` 改名为 `d3d11.dll.bak`，把 `d3d12.dll` 改名为 `d3d12.dll.bak`。这两个文件都是旧安装放进去的（《终末地》本身不带这两个文件）。如果还有 `dxgi.dll`，右键它 → 属性 → 详细信息：如果“产品名称”是 ReShade，也同样改名为 `dxgi.dll.bak`。某个文件不存在就跳过。如果已经有同名的 `.bak` 文件，先把旧的 `.bak` 移到备份文件夹。千万不要动 `C:\Windows` 里的同名文件。
3. **Remove the old bridge.** Move every old bridge add-on (a file ending in `.addon64` with "bridge" in its name, for example an older `dlss5-bridge.addon64`) out of the game folder into your backup folder. Only the new one from download 4 may be in the game folder. If you are not sure what an `.addon64` file is, move it to the backup folder as well; the four files from Case 1 step 3 are all you need.
   **移除旧的 bridge 插件。** 把所有旧的 bridge 插件（名字里带 bridge、以 `.addon64` 结尾的文件，例如旧版 `dlss5-bridge.addon64`）从游戏文件夹移到备份文件夹。游戏文件夹里只能有第 4 项下载的新文件。如果不确定某个 `.addon64` 文件是什么，也一起移到备份文件夹；需要的只有情况一第 3 步的那四个文件。
4. **Continue with Case 1 from step 1.** In step 2, the installer normally no longer finds the old installation, because you renamed its file; just choose Vulkan. If it still offers to uninstall or update, choose uninstall, then run the installer again and choose Vulkan; if `ReShade.ini` is gone afterwards, copy it back from your backup folder. In step 3, choose "Replace" for files that are already there. Step 5 only changes one line of `ReShade.ini`. Effects you used before stay in the ReShade menu; you can switch them off there if you do not want them.
   **从情况一的第 1 步继续。** 第 2 步中，由于旧文件已经改名，安装程序通常不会再发现旧安装，直接选择 Vulkan 即可。如果它仍然提供卸载或更新，选择卸载，然后重新运行安装程序并选择 Vulkan；如果之后 `ReShade.ini` 不见了，从备份文件夹复制回来。第 3 步中，遇到已经存在的文件时选择“替换”。第 5 步只改 `ReShade.ini` 中的一行。以前用过的特效仍会出现在 ReShade 菜单里，不需要的话可以在菜单里关掉。
5. **To go back to your old DirectX 11 setup later,** close the game, remove the four files from Case 1 step 3, rename the `.bak` files back to `.dll`, put your backed-up files back, and switch the launcher back to its previous graphics mode. If you used EndfieldFGSwitch, click "Restore original 2.10.3" in it first.
   **以后想退回旧的 DirectX 11 方案时，** 关闭游戏，移走情况一第 3 步的四个文件，把 `.bak` 文件改回 `.dll`，放回备份的文件，再在启动器里把图形模式改回原来的设置。如果用过 EndfieldFGSwitch，先在工具里点“恢复原装 2.10.3”。

### Case 3: you already use a Vulkan setup, but not this build / 情况三：已经在用 Vulkan 版本，但不是这个版本

1. **Back up.** Close the game. Copy your current `dlss5-bridge.addon64`, `renodx-dlss5.addon64`, `vk-present-adapter.ini` (if you have it) and `ReShade.ini` into a backup folder outside the game folder.
   **备份。** 关闭游戏。把现在的 `dlss5-bridge.addon64`、`renodx-dlss5.addon64`、`vk-present-adapter.ini`（如果有）和 `ReShade.ini` 复制到游戏文件夹以外的备份文件夹。
2. **Keep ReShade** if it is the "full add-on support" version installed for Vulkan.
   **保留 ReShade**，前提是它是为 Vulkan 安装的“完整插件支持”版本。
3. **Replace the bridge.** Copy `dlss5-bridge.addon64` and `vk-present-adapter.ini` from download 4 into the game folder and choose "Replace". Make sure no other bridge add-on (another file ending in `.addon64` with "bridge" in its name) is left in the game folder.
   **替换 bridge。** 把第 4 项下载里的 `dlss5-bridge.addon64` 和 `vk-present-adapter.ini` 复制到游戏文件夹，选择“替换”。确认游戏文件夹里没有留下其他 bridge 插件（名字里带 bridge、以 `.addon64` 结尾的其他文件）。
4. **Generic version.** We recommend Generic 8.5.0-rc10 (download 2), because this build was tested together with that version. Other versions are allowed. If one does not work with this build, the tab **DLSS 5 Bridge** in the ReShade menu shows a section "NR compatibility" that names the problem.
   **Generic 版本。** 建议使用 Generic 8.5.0-rc10（第 2 项），因为本版本是和它一起测试的。其他版本也允许使用。如果某个版本和本版本配合不正常，ReShade 菜单的 **DLSS 5 Bridge** 选项卡会出现 “NR compatibility” 一栏，写出问题所在。
5. **Check `EnableHooks=1`** in `ReShade.ini` as in Case 1 step 5. Keep your own picture settings.
   **检查 `EnableHooks=1`**，方法同情况一第 5 步。你自己的画面参数保持不变。
6. **Start the game and do Case 1, steps 6 to 9.**
   **启动游戏，完成情况一的第 6～9 步。**

### Optional: EndfieldFGSwitch (6x frame generation, NR behind other windows) / 可选：EndfieldFGSwitch（6 倍帧生成、切到其他窗口时神经渲染不停）

`EndfieldFGSwitch.exe` (download 5) is a small separate tool. You only need it for one or both of these two things:

`EndfieldFGSwitch.exe`（第 5 项）是一个单独的小工具。只有需要下面两点中的一点或两点时才需要它：

1. **6x frame generation on RTX 50 cards.** With the game's own files, frame generation stops at 4x (one real frame plus three generated frames). With the tool you can choose 2x to 6x. This works with or without NR.
   **在 RTX 50 显卡上使用 6 倍帧生成。** 使用游戏自带文件时，帧生成最高 4 倍（一帧真实画面加三帧生成画面）。用这个工具可以选择 2～6 倍。这一点有没有神经渲染都可以用。
2. **Frame generation and NR keep running when you switch to another window** (except while the game is minimised). With the game's own files, both stop as soon as another window is in front, and the game shows its plain picture until you click back into it. With the tool's files and this add-on (Hook point Present and Neural Rendering on, as in Case 1 step 7), both keep running while the game window is not minimised, and the picture does not briefly flash without NR when you switch windows. This works on RTX 40 cards too, as long as NR works there.
   **切到其他窗口时，帧生成和神经渲染继续运行**（游戏最小化时除外）。使用游戏自带文件时，只要其他窗口到了前台，两者都会停下，游戏显示原本的画面，直到你点回游戏。使用工具的文件并配合本插件（按情况一第 7 步设置介入点为呈现、打开 Neural Rendering）时，只要游戏窗口没有最小化，两者都会继续运行，切换窗口时画面也不会短暂闪一下没有神经渲染的画面。只要神经渲染能在 RTX 40 显卡上运行，这一点在 RTX 40 上同样有效。

If you need neither, you do not need this tool; NR works without it.

如果两点都不需要，就不需要这个工具；不用它也能使用神经渲染。

**What it changes.** The game ships with an older version of NVIDIA's frame generation files (Streamline 2.10.3). The tool contains NVIDIA's newer official versions (Streamline 2.14.1 and frame generation 310.9.1) and also the game's original files. It swaps 8 of these files in the game folder and, on RTX 50 cards, tells the NVIDIA driver which multiplier to use for this game only. It does not change other files, other games or the driver's global settings. Before it replaces a file it keeps a copy in `%LOCALAPPDATA%\EndfieldFGSwitch` and writes a log there (`fg-switch.log`); to open that folder, type `%LOCALAPPDATA%\EndfieldFGSwitch` into the address bar of File Explorer and press Enter. Because it changes game files, the account risk described at the top applies to it as well.

**它会改动什么。** 游戏自带的是旧版的 NVIDIA 帧生成文件（Streamline 2.10.3）。工具里装着 NVIDIA 官方的新版本（Streamline 2.14.1 和帧生成 310.9.1），也装着游戏的原装文件。它会替换游戏文件夹里的 8 个这类文件；在 RTX 50 显卡上，还会告诉 NVIDIA 驱动这个游戏使用哪个倍率（只对这个游戏生效）。它不改其他文件、其他游戏或驱动的全局设置。替换文件之前，它会把原文件复制一份到 `%LOCALAPPDATA%\EndfieldFGSwitch`，并在那里写日志（`fg-switch.log`）；要打开这个文件夹，在文件资源管理器的地址栏输入 `%LOCALAPPDATA%\EndfieldFGSwitch` 后按回车。因为它会改动游戏文件，开头所说的账号风险同样适用于它。

**How to use it / 使用方法**

The tool shows every label in Chinese and English, for example "切换到 2.14.1 并应用倍率 / Switch to 2.14.1".

工具里的每个文字都同时用中文和英文显示，例如“切换到 2.14.1 并应用倍率 / Switch to 2.14.1”。

1. Close the game.
   关闭游戏。
2. Double-click `EndfieldFGSwitch.exe`. Windows asks "Do you want to allow this app to make changes to your device?": click "Yes". If a blue window "Windows protected your PC" appears, click "More info" and then "Run anyway". Windows shows this window because the tool has no digital signature.
   双击 `EndfieldFGSwitch.exe`。Windows 会问“你要允许此应用对你的设备进行更改吗？”，点“是”。如果出现蓝色的“Windows 已保护你的电脑”窗口，点“更多信息”，再点“仍要运行”。出现这个窗口是因为工具没有数字签名。
3. Check that "Game folder" shows the folder that contains `Endfield.exe`. If it does not, click "Browse…" and choose that folder.
   确认“游戏文件夹”一栏显示的是 `Endfield.exe` 所在的文件夹。如果不是，点“浏览…”选择这个文件夹。
4. Below the folder, the tool shows your graphics card, your driver version and the multipliers you can use. On an RTX 50 card, choose the multiplier; 6x is recommended. (On an RTX 40 card there is no choice: the tool only swaps the files, and frame generation stays 2x, set in the game.)
   文件夹下方会显示你的显卡、驱动版本和可用的倍率。RTX 50 显卡选择倍率，推荐 6 倍。（RTX 40 显卡没有这个选项：工具只替换文件，帧生成保持 2 倍，在游戏里设置。）
5. Click "Switch to 2.14.1". When the box below shows "Done", close the tool.
   点“切换到 2.14.1 并应用倍率”。下方框里显示“完成”后，关闭工具。
6. Start the game and keep DLSS Frame Generation on in the game's settings. The game's menu still offers only its own choices (up to 4x); any of them is fine, because the multiplier you chose in the tool is used anyway.
   启动游戏，在游戏设置里保持 DLSS 帧生成开启。游戏菜单里仍只有它自己的选项（最高 4 倍），选哪一档都可以，因为实际使用的是你在工具里选的倍率。

**To change the multiplier**, close the game, open the tool, choose another multiplier and click "Switch to 2.14.1" again. **To undo everything the tool did**, close the game, open the tool and click "Restore original 2.10.3". The original files come back and the game's menu decides the multiplier again.

**想换倍率时，** 关闭游戏，打开工具，选另一个倍率，再点一次“切换到 2.14.1 并应用倍率”。**想撤销工具做的全部改动时，** 关闭游戏，打开工具，点“恢复原装 2.10.3，倍率交还游戏”。原装文件会放回去，倍率重新由游戏菜单决定。

**Good to know about the tool / 关于工具的注意事项**

- It refuses to do anything while the game is running.
  游戏运行时，它不会做任何改动。
- It only works with the two file versions it knows. If it shows "unknown version", the game has different files (for example after a game update), and its buttons stay greyed out so that nothing is overwritten. In that case wait for an updated tool. The multiplier you chose earlier stays set for this game in the NVIDIA driver until you click "Restore original 2.10.3" in a tool version that knows the new files.
  它只认识两套文件版本。如果显示“未知版本”，说明游戏里的文件不同（例如游戏更新之后），这时按钮保持灰色，不会覆盖任何文件。这种情况下请等待工具更新。你之前选的倍率会继续保存在 NVIDIA 驱动里、只对这个游戏生效，直到你在认识新文件的工具版本里点“恢复原装 2.10.3，倍率交还游戏”。
- If it shows "mixed", an earlier switch did not finish. Click the button you want again.
  如果显示“两种版本混合”，说明上次切换没有完成。再点一次你想要的按钮即可。
- When the launcher repairs or updates the game, it may put the original files back. Then run the tool again.
  启动器修复或更新游戏时，可能会把原装文件放回去。这时再运行一次工具即可。
- On cards older than RTX 40, only "Restore original 2.10.3" is available.
  比 RTX 40 更早的显卡只能使用“恢复原装 2.10.3，倍率交还游戏”。

### If something does not work / 遇到问题时

- **No ReShade message at game start, and Home does nothing:** ReShade is not loaded. Check that the launcher is set to Vulkan, and run the ReShade installer again for `Endfield.exe` with Vulkan.
  **游戏启动时没有 ReShade 提示、按 Home 也没反应：** 说明 ReShade 没有加载。检查启动器是否设为 Vulkan，再对 `Endfield.exe` 重新运行一次 ReShade 安装程序并选择 Vulkan。
- **The tab DLSS 5 Neural Rendering or DLSS 5 Bridge is missing:** the file `renodx-dlss5.addon64` or `dlss5-bridge.addon64` is not in the game folder, or you installed ReShade without "full add-on support".
  **没有 DLSS 5 Neural Rendering 或 DLSS 5 Bridge 选项卡：** 说明游戏文件夹里缺少 `renodx-dlss5.addon64` 或 `dlss5-bridge.addon64`，或者装的 ReShade 不是“完整插件支持”版本。
- **The picture does not change when you switch Neural Rendering:** check that frame generation is on in the game, that Hook point is Present, and that `EnableHooks=1` is saved in `ReShade.ini` in the game folder. Then look at the tab **DLSS 5 Bridge** for a section "NR compatibility". If all of this is in order and the picture still does not change after you have been in the game for about half a minute, restart the game once. If that does not help, see "Asking for help" below. On an RTX 40 card, see "Which graphics cards work" above first.
  **切换 Neural Rendering 时画面没有变化：** 检查游戏里帧生成是否打开、介入点是否为呈现、游戏文件夹里的 `ReShade.ini` 是否已保存 `EnableHooks=1`。然后查看 **DLSS 5 Bridge** 选项卡里有没有 “NR compatibility” 一栏。如果以上都没问题，进入游戏约半分钟后画面仍然没有变化，就重新启动一次游戏。如果还是不行，见下文“求助时”。RTX 40 显卡请先看上文“支持哪些显卡”。
- **NR and frame generation stop when you click another window:** this is normal with the game's own files. If you want them to keep running, use EndfieldFGSwitch (see above).
  **点到其他窗口时，神经渲染和帧生成停了：** 使用游戏自带文件时这是正常的。如果想让它们继续运行，使用 EndfieldFGSwitch（见上文）。
- **The game does not start, crashes, or shows an anti-cheat message:** move the four files from Case 1 step 3 out of the game folder and start the game again. If you used EndfieldFGSwitch, also click "Restore original 2.10.3" in it. If it still fails, uninstall ReShade (see "To remove everything" below).
  **游戏无法启动、闪退或出现反作弊提示：** 把情况一第 3 步的四个文件移出游戏文件夹，再启动游戏。如果用过 EndfieldFGSwitch，也在工具里点“恢复原装 2.10.3，倍率交还游戏”。如果仍然不行，卸载 ReShade（见下文“想全部卸载时”）。
- **Asking for help:** close the game and open an issue at https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/issues. Attach `dlss5-bridge.log` and `ReShade.log` from the game folder. If the problem is about EndfieldFGSwitch, also attach `fg-switch.log` from `%LOCALAPPDATA%\EndfieldFGSwitch`.
  **求助时：** 关闭游戏，到 https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/issues 提交 issue，附上游戏文件夹里的 `dlss5-bridge.log` 和 `ReShade.log`。如果问题与 EndfieldFGSwitch 有关，再附上 `%LOCALAPPDATA%\EndfieldFGSwitch` 里的 `fg-switch.log`。

### Good to know / 注意事项

- **Frame generation must stay on.** With the game's frame generation off, this add-on does nothing and the picture has no NR.
  **帧生成必须保持开启。** 游戏帧生成关闭时，本插件不工作，画面没有神经渲染。
- **Leave "Source interpretation" on Auto.** In the tab DLSS 5 Neural Rendering, keep Encoding, Primaries and Linear unit (nits) under "Source interpretation" on Auto (Chinese menu: 来源解读 → 编码, 原色, 线性单位). Other values pause NR until they are set back to Auto.
  **“来源解读”保持“自动”。** 在 DLSS 5 Neural Rendering 选项卡中，“来源解读”下的“编码”“原色”“线性单位”都保持“自动”。改成其他值会暂停神经渲染，改回“自动”后恢复。
- **Switching windows.** With the game's own files, NR and frame generation stop while another window is in front and start again when you click back into the game. With EndfieldFGSwitch's files, they keep running while the game window is not minimised. In both cases they pause while the game is minimised and start again when you bring the game back.
  **切换窗口。** 使用游戏自带文件时，其他窗口在前台期间，神经渲染和帧生成会停下，点回游戏后重新开始。使用 EndfieldFGSwitch 的文件时，只要游戏窗口没有最小化，它们就会继续运行。两种情况下，游戏最小化时它们都会暂停，恢复游戏窗口后自动继续。
- **Two text files appear in the game folder:** `dlss5-bridge.cfg` (the add-on's settings) and `dlss5-bridge.log` (its log). This is normal.
  **游戏文件夹里会多出两个文本文件：** `dlss5-bridge.cfg`（插件设置）和 `dlss5-bridge.log`（日志）。这是正常的。
- **To turn this add-on off,** close the game, open `vk-present-adapter.ini` with Notepad (edit a copy on the desktop as in Case 1 step 5), change the line `Enabled=1` to `Enabled=0`, and start the game again. Generic then keeps working on its own, but not at the "Present" hook point. Change it back to `Enabled=1` to use this add-on again.
  **想关闭本插件时，** 关闭游戏，用记事本打开 `vk-present-adapter.ini`（和情况一第 5 步一样先改桌面上的副本），把 `Enabled=1` 这一行改成 `Enabled=0`，再启动游戏。之后 Generic 仍会单独工作，但不再使用“呈现”介入点。改回 `Enabled=1` 即可重新使用本插件。
- **To remove everything,** close the game. If you used EndfieldFGSwitch, click "Restore original 2.10.3" in it first. Then move the four files from Case 1 step 3 out of the game folder, together with `dlss5-bridge.cfg` and `dlss5-bridge.log`. To remove ReShade too, run the ReShade installer again, choose `Endfield.exe` and choose uninstall.
  **想全部卸载时，** 关闭游戏。如果用过 EndfieldFGSwitch，先在工具里点“恢复原装 2.10.3，倍率交还游戏”。然后把情况一第 3 步的四个文件，连同 `dlss5-bridge.cfg` 和 `dlss5-bridge.log` 一起移出游戏文件夹。如果还想卸载 ReShade，再次运行 ReShade 安装程序，选择 `Endfield.exe`，然后选择卸载。
- **After a game update,** first check that the game still starts normally. If the add-on stops working, keep the two log files and report the problem.
  **游戏更新后，** 先确认游戏能正常启动。如果插件不再生效，保留那两个日志文件并反馈问题。
- This is an unofficial community project. It is not made or supported by NVIDIA or Hypergryph.
  这是非官方的社区项目，与 NVIDIA 和鹰角网络无关，也不受其官方支持。

## Files

All paths below are inside https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay. The pull request to PEQHUB/RenoDX-DLSS5-Generic carries the same tree under `contrib/dlss5-bridge-vulkan-fg-relay/`, without the `raw/` folders. Files of the 2026-09-29 release stay where they were.

| What | Where |
| --- | --- |
| Ready-to-use build: DLL, `vk-present-adapter.ini`, install guide, licenses, SHA-256 sums | Release [`vk-fgrelay-20261002`](https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/releases/tag/vk-fgrelay-20261002), file `dlss5-bridge-vk-fgrelay-20261002.zip`. The DLL alone is attached too. |
| The tool for Arknights: Endfield | Same release, `EndfieldFGSwitch.exe`. Source: `tools/fg-switch/` (its `README.md` explains what it changes and how to build it). |
| Full source of the bridge build | Same release, `dlss5-bridge-vk-fgrelay-source-0a4af91.zip`. The commits themselves: `merge/dlss5-bridge.bundle` (a git bundle; `git clone merge/dlss5-bridge.bundle` gives branch `feature/vulkan-fg-relay`). |
| Patches for merging | `merge/patches/`: `0001` = NIGos/dlss5-bridge#49, `0002` = #51, `0003` = the 2026-09-29 release, `0004`-`0010` = this update. The same update as one diff: `merge/update-since-20260929.diff`. `merge/CHANGED-SINCE-20260929.txt` lists what it touches. |
| Full copies of every file changed since `d1cc508` | `merge/changed-files/` (source, tests, docs), now at `0a4af91`. |
| Design document | `docs/VULKAN-FG-INPUT-NR.md` ("Update 2026-10-01" and "Update 2026-10-02" at the top) |
| Install guide | `docs/INSTALL.md` (same file as in the zip and as the guide above) |
| Offline test results of this build | `evidence/2026-10-02/summary/offline/` |
| Counted window-switch gaps, tool tests | `evidence/2026-10-02/summary/` |
| Game logs and PresentMon captures behind the numbers below | `evidence/2026-10-02/raw/` |
| Scripts | `tools/fg-transition-gaps.py` (frames presented without an FG evaluate, from a bridge log), `tools/compare-presentmon.py` |

## What changed since 2026-09-29

Bridge commits `a88fec9`, `80628aa`, `d6bdb97`, `9751b15`, `54ca269`, `82908fb`, `0a4af91` on top of `d1ef16f`. File version 1.4.13.20, version string `1.4.13-pre8-vk-fgrelay-20261002`. The published DLL was built from `82908fb`; `0a4af91` only changes documentation.

1. **DLSS modes other than DLAA** (`src/fg-guides.inc`, `src/fg-guide-compute.inc`, `src/fg-guide-nearest.comp`). With Quality, Balanced or Performance, the FG depth and motion vectors are smaller than the back buffer (3414x1440 for a 5120x2160 output). The relay required equal sizes, so after 120 refused frames it fell back to the serial feed for the rest of the process, and the serial feed then ran NR with `mv=zero depth=zero`. Now each guide's own sub-rectangle is scaled to the output grid with a nearest-neighbour compute shader on the FG queue. It copies raw 32-bit texels, so R32F depth and RG16F motion vectors stay bit-exact, and the motion-vector scale is multiplied by output size / motion-vector size (unit confirmed by disassembling Streamline 2.14.1). A compute shader is used because `vkCmdBlitImage` needs a graphics queue, and in this game FG runs on a compute-only queue family while the window is focused. No image is read back to the CPU and no CPU wait is added.
2. **Fullscreen below monitor resolution** (`src/fg-input.inc`, `src/fg-relay.inc`, `src/vkmirror.inc`). Fullscreen 3840x2160 on a 5120x2160 monitor gives a 5120-wide back buffer whose real image is the sub-rectangle x=640, w=3840, while the HUD-less image is 3840 wide at x=0. The old check compared whole-image sizes and refused every frame. Each input now carries its own origin; only the valid regions must have the same size and format. Copies read and write each region at its own origin and leave the outside untouched.
3. **NR stopping during normal play** (`src/fg-handle-registry.h`). FG handles were kept in an 8-entry table that was never cleaned on release. The game recreates its FG feature from time to time; from the ninth new handle on, the relay no longer recognised the evaluate. The registry now tracks create/release with generations, has no fixed capacity and survives address reuse. Transient input problems pause and retry instead of refusing for the rest of the process.
4. **Loss of GPU completion** (`src/fg-relay.inc`, `src/synth.inc`). If the private D3D12 queue stops signalling for 1.5 s, the relay releases every park (unchanged). New: once the CPU worker and recording have stopped, a marker on the D3D12 queue proves that earlier work has finished, and the Vulkan copy/release fences and parks are checked; only then is the session rebuilt. If completion cannot be proven, resources stay alive and NR stays paused.
5. **Frame generation while the window is not focused** (`src/focus-fg.inc`, `src/focus-fg-contract.h`). Streamline stops DLSS-G when its internal `IKeyboard::hasFocus()` returns false. The bridge finds that one call site in `sl.dlss_g.dll` by an instruction, branch and log-string contract (not by hash or fixed RVA) and returns true for that caller only, while NR is at the FG input and the window is visible, not minimised and not resizing. Every other caller, the OS focus, an explicit FG Off and resource release are untouched. `FocusKeepFG=0` turns it off. If the contract is not found, native behaviour is kept. The contract is found in `sl.dlss_g.dll` 2.14.1, not in the 2.10.3 build that Arknights: Endfield ships (see "Streamline version" below).
6. **Flash of the unprocessed picture after switching windows** (`src/composition-guard.inc`). With nothing else on the game's monitor, Windows promotes the game from composed flip to independent flip, and back when the window returns. At each switch Streamline changes DLSS-G pacing (`FlipMetering` 0 <-> 2) and its present queue, and for 9-15 real frames it presents the game's frames without calling DLSS-G. NR runs only inside the FG evaluate, so those frames have no NR. The bridge keeps DWM composing the game's monitor with a 2x2, alpha 1/255, click-through, non-activating, topmost tool window of the game process at the monitor's top-left corner, excluded from screen capture. It is shown while NR is at the FG input, FG was evaluated in the last 1.5 s and the window is not minimised. The game is already composed while focused, so focused behaviour does not change. `HoldComposition=0` removes it.
7. **NR not starting in some sessions** (`src/present-carrier-retry.h`, `src/present-adapter.inc`, `src/fg-input.inc`, `src/fg-relay.inc`). The private carrier is the D3D12 evaluate export of the one loaded module whose version resource says DLSS Super Resolution; with a driver-side DLSS override, that is the driver's model (`...\NGX\models\dlss\...\*.bin`). While the bridge creates its private NGX feature, NGX also maps the game folder's `nvngx_dlss.dll` for a moment, and the bridge's own NGX module scanner keeps it mapped until the scan has finished. In 28 saved sessions where NR started, the scan took 0.40-0.91 s and finished 120-441 ms before the carrier search; in the failing session it took 1.26 s, the search ran 190 ms before it finished, and both modules were visible. The old code refused the ambiguous carrier for the whole session. Now three failures that can clear by themselves are retried once per second, up to 30 attempts: no unique SR module, an SR module that could not be retained, and MinHook status 9 (no free memory within +-1 GB of the target, seen once in the game). While the carrier is pending, the session stays built and owned, no frame is armed, and evaluates are not handed to the relay (the relay ends every unarmed evaluate itself, so the retry step would otherwise never run). An ambiguous carrier is still never hooked. Sessions where the first attempt succeeds behave as before.

Also changed:

- **No Generic whitelist** (`src/present-compatibility.h`). Any Generic build is tried. A missing module, disabled hooks, an unsupported hook point, source overrides, a missing NR/SR entry point or a carrier mismatch are logged and shown in the bridge's ReShade panel with the reason, together with the build known to work (8.5.0-rc10).
- **Source-interpretation overrides** (encoding, primaries, linear unit) now pause NR and it resumes when they are back on Auto, instead of stopping it until restart.
- **Diagnostics.** `[fg-transition]` logs bounded CPU metadata (queue, reset, metering, frame ID, handles) for the first 12 FG calls after a queue, focus or reset change, at most 128 windows per thread. Each carrier attempt is logged; the module list only on the first and last attempt.
- **Configuration** (`vk-present-adapter.ini`, `[Adapter]`): new `FocusKeepFG` and `HoldComposition` (both default 1), `HoldCompositionAlpha`, `HoldCompositionHideCapture`. The shipped ini is unchanged from 2026-09-29.

## Streamline version (Arknights: Endfield)

The game ships Streamline 2.10.3 with frame generation runtime `nvngx_dlssg.dll` 310.5.2. The published bridge was run in the game with three file sets:

| | Streamline 2.10.3 + FG 310.5.2 (game files) | Streamline 2.14.1 + FG 310.5.2 | Streamline 2.14.1 + FG 310.9.1 (the tool's set) |
| --- | --- | --- | --- |
| NR at the FG input while focused | yes (1383 frames armed and written, 0 unarmed) | yes (8668 armed and written) | yes (1386 armed and written; 18328 in a longer run) |
| Focus contract (item 5) | not found: `[focus-fg] unavailable: unique Streamline focus-gate contract was not found; native focus behaviour retained` | found | found |
| Window not focused | FG stops inside Streamline (the game makes no `slDLSSGSetOptions` call), so NR stops; the composition guard hides with it | not tested | FG and NR keep running |
| Driver profile fixed at 6x, driver FG model override off | not tested | runtime reports `DLSSG.MultiFrameCountMax=3`; 3 FG evaluates per real frame = 4x | runtime reports 5; 5 FG evaluates per real frame = 6x (19.4 real frames/s) |

So 6x needs both newer files; Streamline 2.14.1 alone still stops at 4x with the game's FG runtime. Streamline 2.10.3's `sl.dlss_g.dll` also limits generated frames to 3 by itself (found by disassembly on 2026-09-27). When the NVIDIA driver's frame generation model override is on, NGX uses the driver's FG model instead of the game's `nvngx_dlssg.dll`; on the test PC that model also reported `MultiFrameCountMax=5`, even with the game's files.

## EndfieldFGSwitch

A Win32 tool (one `.exe`, about 20 MB) that switches Arknights: Endfield between the two file sets and sets the driver multiplier. Source in `tools/fg-switch/`.

- **Files.** The 8 files `sl.common.dll`, `sl.dlss.dll`, `sl.dlss_d.dll`, `sl.dlss_g.dll`, `sl.deepdvc.dll`, `sl.pcl.dll`, `sl.reflex.dll` and `nvngx_dlssg.dll` of both sets are embedded as resources (NVIDIA's official Streamline 2.14.1 and DLSS 310.9.1 files; the game's original files). The game's own `sl.interposer.dll` is never touched: it exports `HGSetupCustomVulkan`, which NVIDIA's generic build does not have.
- **Safety.** It refuses a folder without `Endfield.exe` and `sl.interposer.dll`, refuses while `Endfield.exe` runs, and only acts when every one of the 8 files matches one of the two known SHA-256 sets (a mix of both is completed). Unknown files, for example after a game update, are never overwritten. Before replacing, it copies the current files to `%LOCALAPPDATA%\EndfieldFGSwitch\backup-<time>`. Each file is written to a temporary name, checked, then moved over the original (`MoveFileEx` with write-through), and the whole folder is checked again.
- **Driver.** On the existing "Arknights: Endfield" driver profile only (found by `NvAPI_DRS_FindApplicationByName`; no profile is created, global settings are not touched) it sets `NGX_DLSSG_MODE_ID` (0x10308298) = 2 (fixed) and `NGX_DLSSG_MULTI_FRAME_COUNT_ID` (0x104D6667) = multiplier - 1, saves and reads back. "Restore" deletes both settings, so they are inherited again and the game's menu decides.
- **Cards.** RTX 50 (`NvAPI_GPU_GetArchInfo` >= Blackwell): 2x-6x; 2x-4x if the driver is older than 595.97. RTX 40: files only (2x stays in the game's menu). Older: restore only.
- **Tests.** Embedded-file self-test 16/16; file switching on a scratch copy of the game folder, 15 checks (unknown version refused and left unchanged, running game refused, mixed set completed, interposer untouched, no temporary files left); a driver write of 6x followed by restore, with the whole driver settings database (7986 profiles, 28364 settings) exported each time: at 6x only the two settings on the game's profile were added, and after restore there were 0 semantic differences from before (`evidence/2026-10-02/summary/fg-switch/`). In the game: the user switched to 2.14.1 at 6x, then 4x and 5x; each multiplier took effect with NR on.

## Measurements

All in Arknights: Endfield on the RTX 5090 test PC, Generic 8.5.0-rc10 with two NR passes. Logs and captures are in `evidence/2026-10-02/raw/`.

- **Window switches before item 6** (2026-10-01 01:34-01:37, a test build with the `[fg-transition]` log, game window on the 5120x2160 monitor, switching between the game and a window on the 3840x2160 monitor). `tools/fg-transition-gaps.py` finds 29 FG queue/`FlipMetering` switches and 366 frames presented without an FG evaluate, 9-15 frames per switch (`summary/window-switch-gaps-before.txt`).
- **The same with the composition guard** (2026-10-01 02:53:04-02:54:19, the same 2x2 window opened by a separate script before it was built into the bridge). While the guard was open: 3442 presents in the PresentMon capture, all `Composed: Flip`, 857 real frames each with 4 presents, and no queue/`FlipMetering` switch in the bridge log. In the 12 s after the guard was closed: 3 switches with 11-13 frames each without an FG evaluate (`summary/window-switch-gaps-guard.txt`). The user reported that the flash was gone while the guard was open; after the guard moved into the bridge, the user confirmed again that switching windows no longer flashes and the frame-time graph stays smooth.
- **6x** (2026-10-02, published build, Streamline 2.14.1 + FG 310.9.1, driver profile fixed at 6x): 96.9 FG evaluates/s for 19.4 real frames/s, i.e. 5 generated frames per real frame; NR armed and written for every real frame (1386, 0 unarmed). The user saw about 115 frames per second. With FG 310.5.2 under the same settings: 60.7 evaluates/s for 20.2 real frames/s, i.e. 4x.
- **SR carrier timing** (item 7): `raw/carrier/carrier-timing.csv` has the module-scan duration and the time from the end of the scan to the carrier search for 31 saved sessions (2026-09-28 to 2026-10-01): 30 found the carrier, 1 saw two SR modules and refused. Two of the 30 are left out of the ranges given in item 7 because their search ran 40 s and 140 s after the scan.
- The latency and frame-pacing table of 2026-09-29 (`evidence/summary/presentmon-comparison.json`) was not measured again; nothing in the relay's pacing or throttle changed.

## Tests

Offline, on the source of `0a4af91` (the add-on code is that of `82908fb`) and, where a DLL is loaded, on the published DLL. All passed; outputs in `evidence/2026-10-02/summary/offline/`.

| Test | Result |
| --- | --- |
| `tests/vulkan-fg-quality/run.cmd registry`: FG/NGX handle registry | 188780 checks, 0 failures |
| `run.cmd guides`: rectangles, motion-vector scale, record/reset helpers | 57 checks |
| `run.cmd hooks`: production NGX wrappers | 535 checks, 0 failures |
| `run.cmd colour`: sub-rectangle copies | 39 checks, 0 failures, 8 recorded production copies |
| `run.cmd recovery`: automatic rebuild with a real D3D12 queue marker | 34 checks, 0 failures |
| `run.cmd compatibility`: no whitelist, pause/resume | 26 checks, 0 failures |
| `run.cmd carrier`: carrier retry against fixture DLLs, including the relay hand-off while pending | 49 checks, 0 failures |
| `run.cmd guard`: composition guard with real Win32 windows | 28 checks, 0 failures |
| `run.cmd gpu`: GPU nearest scaling on real Vulkan queues, read back bit by bit | 402 checks, 0 failures |
| `run.cmd focus` with Streamline 2.14.1 `sl.dlss_g.dll` | 512 guard combinations; unique call site found (return RVA `0x4c0a7`); changed or ambiguous code rejected |
| `focus` test with Streamline 2.10.3 `sl.dlss_g.dll`, `--report-only` | `matches=0` (expected: the gate stays native) |
| `tests/vulkan-fg-relay/test-fg-trace.cmd` | 7382 checks, 0 failures |
| `tests/vulkan-fg-relay/scan/` | 20705 assertions |
| `tests/vulkan-fg-relay/lifetime/`, real ReShade 6.8 + real Generic 8.5.0-rc10 | `stable` pass, `control` pass, `late-ref` exit 12 as expected |
| `tests/vulkan-fg-relay/inject/` | extension appended; both shared-fence directions pass |
| `tests/vulkan-fg-input/run-roundtrip.py`, SDR with the RR module loaded, and HDR10 | 22 of 22 checks each |
| `tests/vulkan-fg-input/test-witness.cmd`, `test-composite.cmd` | pass; composite 2868 / 192 / 12 / 0 |
| EndfieldFGSwitch `--self-test`; `tools/fg-switch/test-files.sh` on a scratch folder | 16 embedded files, 0 mismatches; 15 checks, 0 failures |

In the game (Arknights: Endfield, RTX 5090, user's own play):

- Items 1-3, 2026-09-30 and 2026-10-01: with the game's DLSS Quality mode, no flicker when turning the camera and even pacing; NR kept running through every DLSS mode switch, FG off/on, resolution changes, fullscreen 3840x2160 on the 5120x2160 monitor and windowed mode, and over long sessions.
- Items 5 and 6, 2026-10-01: frame generation and NR kept running behind other windows; no flash when switching windows.
- Item 7, 2026-10-02: the first test build (`a6f6e93a`) did not recover in the game. Its log shows `attempt 1 of 30` and no second attempt, because the relay ended every unarmed evaluate before the retry step; this is the ordering defect fixed in `82908fb`. With the published build and the test-only add-on `carrier-race-helper.addon64` (holds the game folder's `nvngx_dlss.dll` for 3 s when the bridge's private D3D12 device is created), NR started in each of 4 launches without a resolution change; the helper's log shows that it fired in all 4. Without the helper, the carrier was found on the first attempt and 18328 frames were armed and written.
- Streamline 2.10.3 and 2.14.1 runs and the 6x runs: see the table above. EndfieldFGSwitch: switched to 2.14.1 at 6x, then 4x and 5x, each with NR on.

## Limitations

- One game, one GPU (Arknights: Endfield, RTX 5090, driver 616.92). Other Vulkan DLSS-G games are not covered.
- **RTX 40 is untested.** The NR runtime that Generic loads (`nvngx_dlssnr.dll` 310.8.0 from the RHI repository) contains only `sm_120` (Blackwell) GPU code and refuses other cards (`DLSSNR: Unsupported GPU architecture 0x%x, minimum required 0x%x`). Nothing in the bridge is specific to RTX 50, but the whole chain has not run on an RTX 40 card, which also only has 2x frame generation. RTX 20 and RTX 30 have no DLSS Frame Generation, which this path depends on.
- FG inputs must be 8-bit sRGB, the valid regions of the HUD-less image and the back buffer must have the same size, and the game must supply `DLSSG.HUDLess`. Otherwise frames are paused with a log line and the game keeps its own image.
- Any Generic build is tried; only 8.5.0-rc10 has been verified.
- Items 5 and 6 need Streamline 2.14.1 in this game (the tool installs it); with the game's 2.10.3, FG and NR stop while the window is not focused.
- The composition guard is a real 2x2 window on the game's monitor (alpha 1/255, excluded from screen capture).
- The automatic rebuild after a lost GPU completion (item 4) has not been triggered in the game yet.
- The carrier retry gives up after 30 attempts (30 s), as before. In the game it was checked with a helper that forces the condition; the natural two-module race was seen in 1 of 31 saved sessions, and MinHook status 9 in one other session.
- In one launch during the 6x tests (published build, Streamline 2.14.1 + FG 310.9.1), NR did not start and a resolution change brought it back. That launch's bridge log was overwritten by the next start, so the cause is not known; the two following launches were normal.
- EndfieldFGSwitch only knows the two file sets above and only Arknights: Endfield. After a game update that changes these files it changes nothing until it is updated; a multiplier set earlier stays in the driver profile until then.
- Only `Pipeline=2` with `Import=1` is validated. One effect runtime, one swapchain. Generic's layer count and per-layer settings are global, so Upscaled and Present cannot use different layer settings.

## Other notes

- **About the word "Present".** Here it names Generic's hook-point setting that switches this path on. The processing itself happens before DLSS-G, on the real frame, not on the frames after frame generation.
- **Why a contrib folder in PEQHUB/RenoDX-DLSS5-Generic.** The code belongs to the bridge (NIGos/dlss5-bridge, where #49, #51 and #52 are still open). The contrib folder puts the material next to Generic for Generic users and maintainers. Nothing outside `contrib/` is changed and no Generic source is touched.

## Install and rollback

See the guide above or `docs/INSTALL.md`. Rollback of the add-on: put the previous `dlss5-bridge.addon64` back, or set `Enabled=0` in `vk-present-adapter.ini`. Rollback of the tool: its "Restore original 2.10.3" button.

## Merging

- From the 2026-09-29 release (`d1ef16f`): `git am merge/patches/0004-*.patch ... 0010-*.patch`, or apply `merge/update-since-20260929.diff`. From NIGos/dlss5-bridge `d1cc508`: `0001`-`0010`. Or fetch the exact commits from `merge/dlss5-bridge.bundle`.
- Smaller pieces: the handle registry (item 3) is independent and small; the guide scaling (item 1) and the sub-rectangles (item 2) touch the same copy code; the automatic rebuild (item 4); the focus gate (item 5) and the composition guard (item 6) belong together; the compatibility messages; the carrier retry (item 7), which needs the relay hand-off change of `82908fb` together with `d6bdb97`.
- Build: `src\build.cmd` (MSVC, `/W4 /WX`). Published DLL: SHA-256 `4B0389444F3EA0F564DFF5C8A6EAAF5B4D902D796088D65F2F507306641EC0BC`.

## Reproducing

- Offline: `tests/vulkan-fg-quality/README.md` and `tests/vulkan-fg-relay/README.md` list each test, what it needs and what passing looks like.
- Window-switch gaps: `python tools/fg-transition-gaps.py dlss5-bridge.log` on a log with `[fg-transition]` lines.
- Carrier race: `tests\vulkan-fg-quality\run.cmd racehelper <new-dir>` builds the test-only add-on. Put it next to the game's `.exe` with the bridge, start the game, and remove it afterwards.
