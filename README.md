# DLSS 5 neural rendering with native frame generation in Arknights: Endfield (Vulkan) — release unified-20261015q

Release date: 2026-10-19. This release replaces `vk-fgrelay-20261005` (2026-10-05). The previous README is kept at [docs/history/README-vk-fgrelay-20261005.md](https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/blob/main/docs/history/README-vk-fgrelay-20261005.md).

发布日期：2026-10-19。本次发布取代 2026-10-05 的 `vk-fgrelay-20261005`。上一版 README 保留在 [docs/history/README-vk-fgrelay-20261005.md](https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/blob/main/docs/history/README-vk-fgrelay-20261005.md)。

## In short / 简要说明

**What this is.** A ready-to-use package that adds NVIDIA's DLSS 5 neural rendering ("NR", an AI filter that redraws the game's picture with more detailed lighting and materials) to Arknights: Endfield running on Vulkan, together with the game's own DLSS Frame Generation. NVIDIA GeForce RTX 30, 40 and 50 series cards use the same files. You copy one folder into the game folder, change a few settings once, and restart the game.

**这是什么。** 一个开箱即用的插件包，让运行在 Vulkan 下的《明日方舟：终末地》在使用游戏自带 DLSS 帧生成的同时，用上 NVIDIA 的 DLSS 5 神经渲染（NR，一种用 AI 重新绘制画面、让光影和材质更细致的画面滤镜）。NVIDIA GeForce RTX 30、40、50 系列显卡使用同一套文件。只需把一个文件夹复制进游戏目录，做一次设置，再重启游戏即可。

**How it works.** The game draws a picture and passes it to its own DLSS Frame Generation, which then creates the extra in-between frames. Our add-on runs neural rendering on that finished real picture just before frame generation takes it. Frame generation then builds its extra frames from the improved picture, so the expensive AI step runs once per real frame instead of once per displayed frame.

**怎么做到的。** 游戏先画出一张画面，再交给它自带的 DLSS 帧生成去插出中间的画面。我们的插件在帧生成接手之前，对这张已经画好的真实画面做神经渲染；帧生成随后用这张改进过的画面去生成插帧。这样，最耗性能的 AI 步骤只需每张真实画面做一次，而不是屏幕上每显示一帧都做一次。

**What is new since 2026-10-05.**

1. **One package for RTX 30, 40 and 50.** The add-on detects the graphics card and loads the matching neural rendering runtime by itself.
2. **Streamline 2.14.1 is chosen inside the game.** You pick `2.14.1 + DLSS-G 310.9.1` in the add-on panel; it is loaded from the package's own folder at the next start. No game file is replaced, and `EndfieldFGSwitch.exe` is no longer needed.
3. **Frame generation up to 6x on every supported card**, set in the included MFG Unlock panel. RTX 50 cards keep NVIDIA's own frame generation; only the multiplier is changed.
4. **Fixed:** stutter at 2x frame generation; Present-stage NR switching itself off after a high layer count; the frame-generation multiplier not taking effect; a crash during game start; the Streamline option becoming unselectable after the 2026-10-15 game update; and the recovery file refusing to uninstall after the game had been moved to another folder.
5. **Protection and recovery:** a start-up health check that undoes a setting which kept the game from starting, a safe mode, a 15-second confirmation for 5 or more NR layers, automatic adjustment of NVIDIA driver frame-generation settings with a restore record, and a recovery file that removes everything from outside the game.
6. **All logs in one folder** (`dlss5\dlss5_log`), with an optional complete anomaly log for hard-to-reproduce bugs.

**相比 2026-10-05 的新内容。**

1. **RTX 30、40、50 共用一个包。** 插件自动识别显卡，并自行加载对应的神经渲染运行库。
2. **在游戏里选择 Streamline 2.14.1。** 在插件面板中点选 `2.14.1 + DLSS-G 310.9.1`，下次启动时会从插件包自己的文件夹加载。不替换任何游戏文件，也不再需要 `EndfieldFGSwitch.exe`。
3. **所有支持的显卡都能用最高 6 倍帧生成**，在随包附带的 MFG Unlock 面板里设置。RTX 50 显卡保留 NVIDIA 自己的帧生成，只改倍率。
4. **已修复：** 2 倍帧生成卡顿；高 NR 层数后“呈现”阶段 NR 自行停用；帧生成倍率不生效；游戏启动时崩溃；2026-10-15 游戏更新后 Streamline 选项无法选择；游戏换过安装目录后，恢复文件拒绝卸载。
5. **保护与恢复：** 启动健康检查（某项设置导致游戏无法启动时自动撤回）、安全模式、NR 总层数达到 5 层及以上时需在 15 秒内确认、自动调整 NVIDIA 驱动中与帧生成有关的设置并保留恢复记录，以及一个能在游戏外撤除全部组件的恢复文件。
6. **所有日志集中在一个文件夹**（`dlss5\dlss5_log`），并可选开启完整异常日志，用于难以复现的 Bug。

**Tested.** On two RTX 5090 PCs (Windows 11 with driver 616.92; Windows 10 22H2 with driver 616.56), in game. RTX 30 and RTX 40 cards: confirmed working by internal testers on this build; their logs are not part of this release. Details, measurements and known limits are in the technical part below the install guide.

**测试情况。** 在两台 RTX 5090 电脑上做过游戏内实测（Windows 11、驱动 616.92；Windows 10 22H2、驱动 616.56）。RTX 30、RTX 40 显卡：已由内测用户在本版本上确认可用；他们的日志不包含在本次发布中。详细说明、测试数据和已知限制见安装教程之后的技术部分。

---

# Install guide / 使用教程

This guide assumes that you have never installed ReShade or any add-on for this game. If you used the 2026-10-05 version of this add-on, read "Already using the 2026-10-05 version" first.

本教程假设你从未给这个游戏安装过 ReShade 或任何插件。如果你用过本插件 2026-10-05 的版本，请先看“已经在用 2026-10-05 版本”一节。

## Personal statement from the author / 作者的个人声明

I published this file on 18 October 2026. Up to that date, every case I could find in which a player received a warning or a ban involved a first-person-view add-on called "Enhancer". I have not seen a case in which a warning or ban came only from using injected DLSS 5. That does not mean I can guarantee that this add-on is completely safe. If you value your account very much and do not want to take any risk at all, I do not recommend using this add-on.

我发布这个文件的日期是 2026 年 10 月 18 日。截止目前这个时间点为止，我能够找到的、出现了警告和封号的所有案例中，都只出现在使用了一款叫做“Enhancer”的第一人称视角插件上。我目前还没有见过只开了注入式 DLSS5 导致被警告或封号的案例，但这不代表我能够保证本插件是绝对安全的。如果你非常爱惜你的账号，完全不想冒任何风险，那么我不推荐你使用本插件。

I clearly understand and support the Fair Operation Statement (《公平运营声明》) published by Hypergryph. I promise that this add-on never creates or changes any of the game's gameplay or features and never harms the game's fairness in any way; all of my development is about DLSS 5 and the graphics pipeline.

我明确理解并支持鹰角网络发布的《公平运营声明》，承诺本插件绝不创造、修改任何游戏原有的玩法和功能，绝不破坏游戏的任何公平性，我的一切开发都基于 DLSS5 和图形管线。

I even hope that Hypergryph sees these optimizations and that they help reduce development costs, and I hope that Hypergryph lets more people use an officially fine-tuned DLSS 5 as soon as possible.

我甚至希望鹰角官方能够看到我做的这些优化，并且帮助他们降低研发成本，也希望鹰角官方能够尽早让更多的人都能用上官方精调优化过的 DLSS5。

This statement, my "disclaimer" above, does not mean that I support or oppose any other third-party add-on. Please do not use it to start arguments, compare, or label me. I respect both the official developers and the authors of every third-party add-on at the same time; the two do not conflict.

我的此次声明、也就是上面叠的甲的含义，并不代表我支持或不支持任何其他的第三方插件，请不要因此拉踩引战，也不要给我扣帽子。我同时尊重官方和所有第三方插件的作者，这并不冲突。

## What you need / 需要的条件

**Graphics card / 显卡**

- Supported: NVIDIA GeForce RTX 30, 40 and 50 series.
  确定支持：NVIDIA 30 系、40 系、50 系。
- Not known yet: NVIDIA RTX Spark, GTX 16 series, RTX 20 series, Turing RTX 3000–8000, Ampere RTX A500–A6000, Ada RTX 500–6000, Blackwell RTX PRO 500–6000, TITAN RTX, TITAN V.
  尚未确定是否支持：NVIDIA RTX Spark、16 系、20 系、Turing RTX 3000～8000、Ampere RTX A500～A6000、Ada RTX 500～6000、Blackwell RTX PRO 500～6000、TITAN RTX、TITAN V。
- Not supported: NVIDIA GTX 10 series, Pascal P400–GP100 and every GPU older than Pascal; all AMD GPUs; all Intel GPUs.
  确定不支持：NVIDIA 10 系、Pascal P400～GP100、Pascal 架构之前的全部 GPU；AMD 全部 GPU；Intel 全部 GPU。

**NVIDIA driver / NVIDIA 驱动：** version 615.00 or newer; Game Ready or Studio both work.
最低 615.00；Game Ready 或 Studio 均可。

**Windows / 系统：** Windows 10 version 2004 (build 19041) or newer, or Windows 11; 64-bit.
最低 Win10 2004（19041），或 Win11；64 位。

**The game must run with Vulkan.** If you ever switched the game's graphics mode to DirectX 11 in the launcher's game settings, switch it back to Vulkan.
**游戏必须以 Vulkan 运行。** 如果你曾在启动器的游戏设置里把图形模式改成 DirectX 11，请改回 Vulkan。

## Downloads / 下载

1. **ReShade 6.8.0 with full add-on support:** https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe (this is the same file as the button "Download ReShade 6.8.0 with full add-on support" on https://reshade.me).
   **带完整插件支持的 ReShade 6.8.0：** https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe （与 https://reshade.me 上“Download ReShade 6.8.0 with full add-on support”按钮下载的是同一个文件）。
2. **This package:** on the release page https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/releases/tag/unified-20261015q, scroll down to "Assets" and click `unified-20261015q-candidate.zip` (about 280 MB). Do not download "Source code"; that is not the add-on. After downloading, right-click the zip file and choose "Extract All". This creates a folder named `unified-20261015q-candidate`.
   **本插件包：** 打开发布页 https://github.com/Yukikaze20170315/dlss5-vulkan-fg-relay/releases/tag/unified-20261015q ，往下找到“Assets”，点击 `unified-20261015q-candidate.zip`（约 280 MB）。不要下载“Source code”，那不是插件。下载后右键这个 zip 文件，选择“全部解压缩”，会得到一个名为 `unified-20261015q-candidate` 的文件夹。

The name contains "candidate" because the add-ons are exactly the ones that were tested under that name; only the uninstall files and their description were corrected afterwards. This is the release version.
文件名里带“candidate”，是因为插件本体与以这个名字测试过的完全相同，之后只修正了卸载用的恢复文件及其说明。这就是正式发布版。

The package also contains a technical document, `DLSS5-使用与恢复 Usage and Recovery.md`. You do not need to read it to follow this guide.
包里还有一份技术文档 `DLSS5-使用与恢复 Usage and Recovery.md`，按本教程操作不需要阅读它。

## Install / 安装

**1.** Run `ReShade_Setup_6.8.0_Addon.exe`.
**1.** 打开 `ReShade_Setup_6.8.0_Addon.exe`。

**2.** Click "Browse...". In the window that opens, go to the Arknights: Endfield game folder. If you kept the launcher's and the game's default install locations, it is usually `C:\Program Files\Hypergryph Launcher\games\Arknights Endfield`. If you changed it or do not know where it is: open the Hypergryph launcher, and on the Endfield page click the button with three lines to the right of the start button ("开始游戏"). In the menu, choose the game settings ("游戏设置"). The window shows the game installation path ("游戏安装路径"). Remember this folder; you will need it again later.
**2.** 点击“Browse...”，在打开的窗口中找到终末地的安装目录。如果启动器和游戏一直按默认位置安装，这个目录通常是 `C:\Program Files\Hypergryph Launcher\games\Arknights Endfield`。如果改过、不知道目录在哪：打开启动器，点击终末地页面“开始游戏”右侧的三道横杠，在出现的菜单中选择“游戏设置”，页面会显示“游戏安装路径：……”，这就是要在“Browse...”窗口中找到的地方。记住这个目录，等会儿还要用。

**3.** Select `Endfield.exe` in that folder and click "Open".
**3.** 选中该目录下的 `Endfield.exe`，然后点“打开”。

**4.** Click "Next". You may now see "Downloading compatibility information ..." with a spinning circle. It does not matter whether this succeeds or fails: it downloads something this add-on does not need, and in some regions the download can fail because of network restrictions. When it stops, click "Next".
**4.** 点“Next”，之后可能会出现一直转圈的“Downloading compatibility information ...”。无论成功还是失败都没关系：它下载的不是本插件需要的东西，在中国大陆等地区可能因网络限制而失败。转圈结束后，点击“Next”。

**5.** Tick the fourth icon from the top, the red "Vulkan", then click "Next".
**5.** 勾选从上往下数第四个图标，红色的“Vulkan”，然后点“Next”。

**6.** "Downloading compatibility information ..." may appear again. As before, success or failure does not matter. When it stops, you may see a red cross (failure): click "Next". Or you may see the page "Select effects to install" with many checkboxes: do not tick or untick anything, just click "Next".
**6.** 一直转圈的“Downloading compatibility information ...”可能会再次出现。和上面一样，成功还是失败都没关系。转圈结束后，如果提示红叉失败，直接点“Next”；如果进入“Select effects to install”页面、下方出现大量可勾选项，什么都不要勾选、也不要取消已经勾选的项目，直接点“Next”。

**7.** You may now see a spinning circle with "Downloading Standard effects from https://github.com/......". As before, success or failure does not matter. When it stops, you may see a red cross: click "Next". Or you may see the page "Select add-ons to install" with many checkboxes: do not tick or untick anything, just click "Next".
**7.** 这时可能会出现“Downloading Standard effects from https://github.com/......”等字样的转圈。和上面一样，成功还是失败都没关系。转圈结束后，如果提示红叉失败，直接点“Next”；如果进入“Select add-ons to install”页面、下方出现大量可勾选项，什么都不要勾选、也不要取消已经勾选的项目，直接点“Next”。

**8.** A green tick and "Successfully installed ReShade ......" means ReShade is installed. Click "Finish"; the installer closes. You can delete `ReShade_Setup_6.8.0_Addon.exe` now, or keep it in case you need to reinstall.
**8.** 页面出现绿色对勾，提示“Successfully installed ReShade ......”，说明安装成功。点“Finish”，安装程序会自动关闭。之后可以删除 `ReShade_Setup_6.8.0_Addon.exe`，也可以留着以备重装。

**9.** Open the `unified-20261015q-candidate` folder. Select everything inside it (all files and the `dlss5` folder; Ctrl+A works) and copy it into the game folder from step 2. Copy what is inside the folder, not the folder itself. If Windows asks for administrator permission, click "Continue". You can delete the `unified-20261015q-candidate` folder afterwards, or keep it in case you need to reinstall.
**9.** 打开 `unified-20261015q-candidate` 文件夹，全选里面的所有文件和文件夹（包括 `dlss5` 文件夹，可以按 Ctrl+A），全部复制到第 2 步记住的游戏目录中。要复制的是文件夹里面的内容，不是这个文件夹本身。如果 Windows 要求管理员权限，点“继续”。之后可以删除 `unified-20261015q-candidate` 文件夹，也可以留着以备重装。

**10.** Turn on Windows "Hardware-accelerated GPU scheduling". Frame generation does not work without it. Please note: part of this add-on works through frame generation. Even if you dislike frame generation, you need at least 2x frame generation to use this add-on normally. If you do not want frame generation at all, this add-on is not for you.
**10.** 打开 Windows 的“硬件加速 GPU 计划”。不开启它就不能用帧生成。提前强调一下：本插件有一部分功能运行在帧生成上。即使你再不喜欢帧生成，想正常使用本插件也至少要开 2 倍帧生成。如果你坚持不想开帧生成，那么后面的内容可以不用看了。

- Windows 10: Settings → System → Display → Graphics settings.
  Win10：设置 → 系统 → 显示 → 图形设置。
- Windows 11: Settings → System → Display → Graphics → Advanced graphics settings (called "Change default graphics settings" in older versions).
  Win11：设置 → 系统 → 显示 → 图形 → 高级图形设置（较旧版本里叫“更改默认图形设置”）。
- If you cannot find it: press Win+R, type `ms-settings:display-advancedgraphics`, and click OK. The page with the switch opens.
  如果找不到：按 Win+R，在“运行”窗口中输入 `ms-settings:display-advancedgraphics`，点确定，开关所在的设置页就会打开。

If you had to turn the switch on, restart the PC. If it was already on (true for most people), you do not need to restart.
如果是你刚刚打开的，需要重启电脑；如果它本来就是打开的（大多数人如此），就不需要重启。

## First start and settings / 首次启动与设置

The first start is only for settings; they take effect after a restart. This is needed only once, right after installing.
第一次启动只用来做设置，设置要重启游戏后才生效。只有刚安装后的第一次需要这样做。

**11.** Start the game. When it is running, press the **Home** key. The ReShade menu opens with a tutorial. Click "Skip tutorial" ("跳过教程" in a Chinese menu); do not follow ReShade's own tutorial. ReShade was not designed for DLSS 5, and some of its settings can stop this add-on from working. In particular, do not change the game's color space (HDR): keep the game's original 8-bit sRGB format, or the Present-stage DLSS 5 cannot run.
**11.** 启动终末地。游戏启动后，按下 **Home** 键，会出现 ReShade 的操作面板和教程指引。请点击“跳过教程”，不要按照它的教程操作。ReShade 原本不是为 DLSS5 设计的，它的部分设置反而可能让本插件无法正常运行。特别注意 HDR：请不要修改游戏的色彩空间，保持终末地原生的 8 位 sRGB 格式，否则“呈现”层的 DLSS5 将无法正常运行。

**12.** At the top of the menu you see a row of tabs, including "DLSS 5 Bridge", "DLSS 5 Neural Rendering" and "MFG Unlock". Each of these tabs has a language choice at the top (English or Chinese).
**12.** 跳过教程后，上方可以看到一行菜单，里面有“DLSS 5 Bridge”、“DLSS 5 Neural Rendering”、“MFG Unlock”这三个选项。每个选项页的顶部都可以选择语言（英文或中文）。

**13.** Open "DLSS 5 Bridge". Under "Deployment" ("统一部署"), choose `2.14.1 + DLSS-G 310.9.1`. Under "NR hook mode (EnableHooks)" ("NR 钩子模式"), choose "1: NGX + Streamline (suite Present NR)" ("1：NGX + Streamline（本套件呈现 NR）").
**13.** 点进“DLSS 5 Bridge”，把“统一部署”下方的选项改成 `2.14.1 + DLSS-G 310.9.1`，再把“NR 钩子模式”下方的选项改成“1：NGX + Streamline（本套件呈现 NR）”。

**14.** Close the game and start it again. Wait about 5 to 10 seconds after closing before you start it again; otherwise a window from ACE (the game's anti-cheat) may block the start. If that window appears, close it, wait a few seconds and start the game again.
**14.** 关闭游戏，重新启动终末地。关闭游戏后请不要立刻再启动，过 5～10 秒左右再启动，否则可能会弹出被 ACE（游戏的反作弊）拦截的窗口。如果弹了也别担心，关掉弹窗，过几秒再启动游戏就行了。

**15.** In the game's own settings, set the upscaling option ("画质提升") to "DLSS". Any DLSS Super Resolution mode ("DLSS 超分辨率模式") works; we recommend "Quality" ("质量"). Turn on DLSS Frame Generation and choose a multiplier from 2x to 4x; any of them is fine, because MFG Unlock can take over the multiplier later. Do not choose "Auto" ("自动"), and do not leave frame generation off. Part of DLSS 5 (the "Present" stage) runs at the entrance of frame generation: with frame generation off it stops, and with "Auto" MFG Unlock cannot take over and frame generation behaves erratically. This is the main difference from most other DLSS 5 solutions, and the most effective optimization in this one. With the current cost of DLSS 5, playing smoothly is almost impossible without frame generation; use at least 2x.
**15.** 进入终末地的游戏设置，把“画质提升”改成“DLSS”。“DLSS 超分辨率模式”可以选任何档位，推荐“质量”。打开 DLSS 帧生成，倍率选 2～4x 均可，因为之后 MFG Unlock 可以接管倍率。但不要选“自动”，也不要不开帧生成。DLSS5 的一部分（“呈现”层）运行在帧生成的入口上：关闭帧生成会使它失效；选择“自动”会让 MFG Unlock 的接管失效，帧生成的判定也会乱掉。这是本方案和大多数 DLSS5 方案的核心差别，也是最有效的一步优化。就现阶段 DLSS5 的负载来看，想流畅地玩，帧生成几乎不可能不开，至少也要 2x。

**16.** Press Home and open "DLSS 5 Neural Rendering". Find "Hook point" ("介入点"). Below it are three rows of buttons from 0 to 4: "Render layers" ("渲染层数"), "Upscaled layers" ("超分后层数") and "Present layers" ("呈现层数"). Each number is how many times NR runs at that stage.
**16.** 按 Home 打开 ReShade 面板，点进“DLSS 5 Neural Rendering”页面，找到“介入点”。下面有三行 0～4 的按钮：第一行“渲染层数”，第二行“超分后层数”，第三行“呈现层数”。每个数字表示 NR 在该阶段运行几次。

**17.** Choose the layers according to your graphics card:
**17.** 按显卡情况选择层数：

- 12 GB of video memory or more: you can try Upscaled 1 and Present 1.
  显存大于等于 12G：可以尝试“超分后层数”选 1，“呈现层数”选 1。
- 8 GB or less: we recommend Upscaled 0 and Present 1. With 8 GB you can try 2 layers in total, but only at 1080p or lower; the frame rate will be low, so use it for photos rather than for normal play.
  显存只有 8G 或更小：建议“超分后层数”选 0，“呈现层数”选 1。8G 显存也可以尝试总共 2 层，但只建议在 1080P 及以下分辨率尝试，帧数较低，不建议日常使用，仅建议用于拍照。
- For this game, in any case, do not use more than 2 layers in total, and not more than 1 in any single row. For example, do not choose Upscaled 2 or Present 2.
  对于终末地，无论如何不建议总层数大于 2，也不建议某一行的层数大于 1。例如不推荐“超分后层数”选 2 或“呈现层数”选 2。
- If you use only one layer, choose Present 1. With one layer in Render or Upscaled there is still an unsolved problem: the frame counter is high, but the motion looks less smooth. With Present 1 motion looks smoother, and in most cases the picture also looks better (this is based on feedback from some users, may not be objective, and is not my personal opinion).
  如果只开 1 层，更建议选“呈现层数”1 层。“渲染层数”或“超分后层数”只开 1 层时，还有一个尚未解决的问题：实时帧数高、但肉眼看起来帧数低。选“呈现层数”1 层不仅肉眼更流畅，大多数情况下画面也更好（该评价基于部分用户反馈，可能不够客观，并非我个人意见）。

If you choose 5 or more layers in total, the add-on asks you to confirm within 15 seconds; without confirmation it goes back to your previous layers.
如果总层数选到 5 层及以上，插件会要求你在 15 秒内确认；不确认就会自动回到之前的层数。

**18.** Adjust the other settings on the "DLSS 5 Neural Rendering" page as you like. For this game we recommend "Style" ("风格") = "Cinematic" ("电影感") in most cases. In "Advanced settings" ("高级设置"), do not change anything except the hotkeys and the screenshot settings.
**18.** “DLSS 5 Neural Rendering”页面的其他参数按自己的喜好调整。对于终末地，大多数情况下“风格”建议选“电影感”。“高级设置”中，除了快捷键和截图相关功能以外，其他设置都不建议乱改。

**19.** The default hotkey that turns DLSS 5 on and off is F6. In this game F6 also opens the pass page, so it is easy to press by accident. To change it: on the "DLSS 5 Neural Rendering" page, tick "Advanced settings" at the top; the section "Hotkeys & screenshots" ("快捷键与截图") appears below, where you can change it.
**19.** 用来开关 DLSS5 的默认快捷键是 F6，它也是终末地通行证页面的按键，容易误触。修改方法：进入“DLSS 5 Neural Rendering”页面，勾选上方的“高级设置”，下面会出现“快捷键与截图”，在里面修改即可。

**20.** If the frame rate is too low, open "MFG Unlock". Its multiplier is set to "Game default" ("由游戏决定"). In the drop-down list you can choose up to "6x", which is 6x frame generation. After you choose a multiplier here, the frame generation multiplier in the game's settings no longer has any effect; the add-on controls it. To give control back to the game's settings, choose "Game default" again.
**20.** 如果觉得帧数太低，可以进入“MFG Unlock”页面。这里默认的帧生成倍率是“由游戏决定”，点击下拉菜单最高可选“6x”，即 6 倍帧生成。在这个页面改了倍率后，游戏设置里的帧生成倍率就不再起作用，由本插件接管。想交还给游戏设置时，选回“由游戏决定”即可。

**21.** Done. Play for a moment and see how smooth it is. Press Home and open ReShade's "Statistics" tab ("统计"). You should see a fairly flat frame-time curve with small, gentle bumps, and the game should feel smooth.
**21.** 主要设置已经完成，可以进游戏感受一下流畅度。按 Home 打开 ReShade 面板，点进上面的“统计”页，你应该看到一条比较平滑、略有起伏但并不明显的帧曲线，玩的时候也应该感觉比较丝滑。

If the curve looks like a saw blade or another irregular shape, the game is not as smooth as this add-on is designed to be. First check the steps above for mistakes. If you followed every step exactly, open "MFG Unlock", tick "Advanced settings", tick "Legacy software flip pacing (restart required)" ("Legacy 软件呈现节奏（需重启）"), and restart the game. This fixes the smoothness on most graphics cards.
如果看到的是锯齿状或其他不规则形状的曲线，说明目前的体验没有达到本插件设计的流畅度。请先检查上面的步骤有没有误操作。如果确定都严格照做了，进入“MFG Unlock”页面，勾选“高级设置”，勾选出现的“Legacy 软件呈现节奏（需重启）”，然后重启游戏。这样应该能解决大部分显卡的流畅度问题。

## If it still stutters or DLSS 5 does not work / 如果仍然卡顿或 DLSS5 不生效

**22.** Known problems that you can fix yourself:
**22.** 目前已知、可以自己解决的问题：

1. **Turn off "Smooth Motion" in the NVIDIA App** (shown as "AI 插帧" in the Chinese NVIDIA App) for this game. With it on, the frame rate becomes very unstable. This is not a bug: this add-on deliberately does not support Smooth Motion, because its latency is too high; even if it worked, I would not recommend it.
   **不要开启 NVIDIA App 里的“AI 插帧”**（Smooth Motion），否则游戏帧数会非常不稳定。这不是 Bug：本插件本来就没打算兼容 Smooth Motion，因为它的延迟实在太高，就算能用也不推荐。
2. **Turn off the NVIDIA game filters and RTX HDR** for this game. They can stop the add-on from working. This is a known bug that may be fixed later, but it is difficult.
   **不要开启 NVIDIA 的游戏滤镜效果和 RTX HDR**，否则可能使插件失效。这是已知 Bug，之后可能会修，但难度较高。
3. **Safe mode is still on.** If an earlier mistake caused this, then even after every setting is correct DLSS 5 still does not work, the top-left corner of the game shows "NR runtime not installed" ("未安装 NR 运行时"), and the "DLSS 5 Neural Rendering" page says "NR runtime not installed" and "NVIDIA's signed nvngx_dlssnr.dll is not next to the addon or the game's executable" ("插件或游戏可执行文件旁没有 NVIDIA 签名的 nvngx_dlssnr.dll"). This means safe mode was switched on and must be switched off by hand. Open "DLSS 5 Bridge"; it shows a ticked "Safe mode next launch" ("下次启动安全模式（手动退出）"). Untick it and restart the game. DLSS 5 should then work again.
   **安全模式仍在开启。** 如果之前的误操作导致即使现在所有设置都改对了、严格按教程做完了，DLSS5 仍不生效，游戏左上角提示“未安装 NR 运行时”，“DLSS 5 Neural Rendering”页面也提示“未安装 NR 运行时”、“插件或游戏可执行文件旁没有 NVIDIA 签名的 nvngx_dlssnr.dll……”，说明安全模式被启用了，需要手动关掉。点进“DLSS 5 Bridge”页面，下面会有一个已被勾选的“下次启动安全模式（手动退出）”，取消勾选，然后重启游戏，DLSS5 应该就会恢复正常工作。

**23.** Enjoy your new DLSS 5 experience!
**23.** 享受 DLSS5 带来的全新游戏体验吧！

## Already using the 2026-10-05 version / 已经在用 2026-10-05 版本

Do these three things once, with the game closed, before step 9:
在第 9 步之前、游戏关闭的状态下，做一次下面三件事：

1. If you used `EndfieldFGSwitch.exe`: in the launcher, run the game's file verification/repair, so that the game's own files are original again. This package does not need that tool and does not replace game files.
   如果用过 `EndfieldFGSwitch.exe`：在启动器里对游戏执行文件校验/修复，让游戏自带的文件恢复原样。本插件包不需要这个工具，也不替换游戏文件。
2. Move `vk-present-adapter.ini` out of the game folder (for example to your desktop). The package no longer uses this file, and an old copy keeps an older setting that caused stutter at 2x.
   把游戏目录里的 `vk-present-adapter.ini` 移出游戏目录（例如移到桌面）。本插件包不再使用这个文件，旧文件里保留着一个会导致 2 倍帧生成卡顿的旧设置。
3. Move `nvngx_dlssnr.dll` out of the game folder (not out of the `dlss5` folder). The package brings its own copies in `dlss5\nr\`.
   把游戏目录里的 `nvngx_dlssnr.dll` 移出游戏目录（不是 `dlss5` 文件夹里的）。本插件包在 `dlss5\nr\` 里自带了运行库。

Then continue with step 9 and choose "Replace" for files that already exist. Your NR settings in `ReShade.ini` stay as they are. ReShade is already installed, so you can skip steps 1 to 8, and steps 11 to 15 are still needed once.
然后从第 9 步继续，已存在的文件选择“替换”。`ReShade.ini` 里的 NR 设置会保留。ReShade 已经装过，可以跳过第 1～8 步；第 11～15 步仍需做一次。

## Updates, uninstalling, known issues and bug reports / 更新、卸载、已知问题与反馈

You only need this part if something goes wrong.
能正常玩就不用看这部分。

**24. After a game update.** Some features may stop working for a while after a game update. Such updates *usually* do not prevent the game from running normally with DLSS 5 turned off, even with the old add-on version. I will release a new version as soon as possible for features that an update broke; until then you can turn off DLSS 5 or the affected feature and play without it. If an *unusually* large update changes how DLSS 5 or frame generation work at a low level, I cannot guarantee that the new game still runs with the old add-on (honestly, nobody can guarantee that for an injected add-on); in that case the game might not even start. If that happens, uninstall the add-on as in step 25 and play the original game until a new version is out.
**24. 游戏更新后。** 游戏更新后，本插件可能会有部分功能暂时失效。这些更新*通常*不会影响“新版游戏 + 旧版插件”在不开 DLSS5 时正常运行原生画面；受更新影响的功能我会尽快更新版本，所以可以先关闭 DLSS5 或受影响的功能，暂时原生游玩。但如果游戏进行了不*通常*的大型更新，或大改了 DLSS5 和帧生成相关渲染管线的底层逻辑，我无法保证（实话说，对注入式插件也不可能有人能保证）“新版游戏 + 旧版插件”还能正常运行，甚至完全无法启动游戏都很正常。如果真的遇到这种情况，在新版出来之前，请按第 25 条卸载，暂时恢复原生游玩。

**25. Uninstall.** Close the game. In the game folder, run `DLSS5-Recovery 故障恢复.cmd` and type the number **9** in the window that opens. Allow the administrator request if Windows asks. If the window lists other programs that use the same ReShade and asks whether to continue, type Y to continue (ReShade then also stops loading for those programs) or N to cancel without changing anything; Windows may ask for administrator rights once more after Y. After a few seconds, or a little over ten seconds, the window says that removal is finished; then close the window. A few files may remain in the game folder; they do not affect the game in any way and are not detected by the anti-cheat, so you can leave them. The window also reminds you to verify/repair the game files in the launcher. This package does not replace game files, so that step only matters if other tools changed game files before; doing it does no harm.
**25. 卸载。** 关闭游戏，运行游戏根目录下的 `DLSS5-Recovery 故障恢复.cmd`，在弹出的窗口中输入数字 **9**。如果 Windows 请求管理员权限，选择允许。如果窗口列出了其他也在使用同一个 ReShade 的程序并询问是否继续，输入 Y 继续（这些程序也会不再加载 ReShade），输入 N 则取消、不做任何改动；输入 Y 后 Windows 可能会再请求一次管理员权限。约几秒或十几秒后，窗口会提示撤除完成，然后关闭窗口即可。根目录可能会残留几个文件，它们不影响游戏的任何正常运行，也不会触发反作弊或被识别到，可以不管它们。窗口最后还会提醒你在启动器里校验/修复游戏文件；本插件包不替换游戏文件，所以只有以前用其他工具改过游戏文件时这一步才有意义，做了也没有坏处。

**26. Known issues that do not stop you from playing.**
**26. 已知但不影响使用的问题。**

1. The game's built-in photo camera takes its picture before the Present-stage DLSS 5, so its photos do not show Present-stage DLSS 5. I will not change how the game's camera works: that would break my promise never to change the game's features, and it would be risky with the anti-cheat. Use the add-on's own screenshot function instead; it captures DLSS 5 at every stage. To use the game camera's filters: set up the shot and filter, press X to hide the UI, then press the add-on's screenshot key.
   游戏内置相机的拍照发生在“呈现”层 DLSS5 之前，所以拍不到“呈现”层的 DLSS5。我不会去改游戏内置相机的逻辑，这违背了“本插件绝不创造、修改任何游戏原有的玩法和功能”的承诺，对反作弊来说也是相当危险的操作。请使用本插件自带的截图功能，它可以拍到所有阶段的 DLSS5。想用游戏相机的滤镜时，可以调好位置和滤镜后按 X 隐藏 UI，再按本插件的截图键。
2. Right after a character revives, right after a new map has loaded, and on some UI screens such as the backpack, part of a model's shadow behind may show through. It hardly affects normal play. The cause is not found yet; I will fix it if it can be done without risk.
   角色死亡刚复活、切地图刚加载完，以及背包等部分 UI 下，有可能映出背后的一部分模型影子，基本不影响正常游戏。目前还没找到原因，能在无风险的前提下修好我就修。
3. Switching UI screens or scenes very quickly (for example when visiting friends' bases for dailies) can make DLSS 5 turn off and on again for an instant, which looks like a flash. This is hard to fix without risking overall stability; I will try, but cannot promise.
   高频切 UI、切场景时（比如清每日逛好友家），可能出现 DLSS5 被快速关闭又开启一瞬间的闪烁。这个不太好修，动了可能影响整体稳定性，不保证能解决，但会试试看。
4. If you open the ReShade menu while controlling a character in first- or third-person view, the mouse is no longer locked to the game window and may leave it, and the game stutters until the window loses focus and gets it back. Switch windows once or press the Win key twice to fix it. Also, with DLSS 5 on, dragging in the ReShade menu feels a little sluggish, regardless of performance, even on top-end PCs. Both are existing ReShade problems, not caused by this add-on.
   在控制角色的第一或第三人称视角时打开 ReShade 菜单，鼠标会脱离游戏窗口锁定、可能划到窗口外，同时游戏卡顿，直到窗口失焦再回到焦点。切一下窗口或连按两下 Win 键就能解决。另外 DLSS5 开启后，在 ReShade 面板里拖动鼠标会有些卡顿，与性能无关，顶配电脑也会出现。这两个都是 ReShade 原有的问题，不是本插件导致的。
5. In rare cases DLSS 5 rapidly turns off and on several times in a row (flashing). It is very hard to reproduce: on my four test PCs and among several internal testers it appeared only twice in about half a month, even when we tried to trigger it. Restarting the game fixes it. Reports are very welcome.
   在某些极端情况下，DLSS5 会快速连续关闭又开启（闪烁）。这个 Bug 非常难复现：我的 4 台测试机和几名内测群友想故意触发都很难，半个月左右只出现了 2 次。出现后重启游戏即可解决。欢迎大家多多反馈。

**27. Reporting a bug.** After the bug happens, exit the game and do not start it again until you have sent the report. Send `ReShade.log` from the game folder, and put every file in the folder `dlss5\dlss5_log` (inside the game folder) into a zip file and send that too.
**27. 反馈 Bug。** 触发 Bug 后退出游戏（在完成反馈之前不要重启游戏），把游戏根目录的 `ReShade.log` 发给我，再进入游戏根目录下的 `dlss5` 文件夹里的 `dlss5_log` 文件夹，把里面的所有文件打包成压缩包发给我。

**28. Bugs that are very hard to solve** (usually after you already reported one and it is not fixed, and I suggest this step): start the game, press Home, open "DLSS 5 Bridge", tick "Advanced settings", scroll to the bottom, and in the "Anomaly log" ("异常日志") section tick "Full anomaly log (one full line per occurrence)" ("完整异常日志（每次发生都完整记录一行）"). In the window that opens, tick "I understand the disk-use risk above" and click "Turn on". Then try to trigger the bug a few times, exit the game, and send the same files as in step 27.
**28. 非常难解决的疑难问题**（通常是你已经反馈过、但 Bug 还没修好，我建议你做这一步）：进入游戏后按 Home，进入“DLSS 5 Bridge”页面，勾选“高级设置”，滑到页面最下方，在“异常日志”区勾选“完整异常日志（每次发生都完整记录一行）”，在弹出的窗口中勾选“我已了解上述容量占用风险”并点“确认开启”。然后去尝试触发几次 Bug，退出游戏，按第 27 条发送同样的文件。

This mode records every anomaly the add-on can record. Depending on the bug it can use a lot of disk space; in the worst case several GB per day. After reporting, start the game, untick the option again, and check how much space the logs use. Everything in `dlss5_log` is a log file and not needed for the add-on to run, so you can delete large files there by hand.
请注意，该功能会完整记录本插件理论上能记录到的所有异常值。视 Bug 类型，存储空间占用可能非常大，最极端情况下一天（24 小时）生成几个 G 的日志都有可能。反馈完毕后，请重启游戏，关闭这个功能，并检查这些日志占用了多少空间。`dlss5_log` 文件夹内都是日志文件，不是插件运行所必需的，如果占用空间很多，可以手动删除其中较大的文件。

---

# Technical details

Everything below is for developers and maintainers: what changed, why, how it was verified, what is still open, and which files to use for merging or reproducing.

## 1. Components and versions

The package `unified-20261015q-candidate.zip` (SHA-256 `f13a454e799ce8cee52a0cbcbe980ce5a0e923113b6dbcc2762794cd6177a18a`, 280,294,193 bytes) contains 23 files. The three add-ons, the NR runtimes and the Streamline files are byte-identical to the build that was tested on the two RTX 5090 PCs described in section 4; that is why the folder keeps its candidate name. On 2026-10-19 three non-binary files were corrected in place without changing the version: `DLSS5-Clean-Recovery.ps1` and `DLSS5-Recovery 故障恢复.cmd` (section 3.7) and two sentences of `DLSS5-使用与恢复 Usage and Recovery.md` that describe them. `SHA256SUMS.txt` and `release-manifest.json` inside the package list every file.

| File | Version | SHA-256 | Source |
| --- | --- | --- | --- |
| `dlss5-bridge.addon64` | `1.4.13-pre8-unified1q-20261015` | `68cedaf0a233b1c0b04baac1537b8eb434f667a7a7d8112c15ba175768d4c267` | [NIGos/dlss5-bridge](https://github.com/NIGos/dlss5-bridge) with the changes of the earlier releases of this repository and of this release |
| `renodx-dlss5.addon64` | Generic `v8.5.0-rc10-unified1q` | `7de606225acb49075f4f03b1125e89f46109fe5013fa9a3c6e121578dc7ce80b` | RenoDX DLSS 5 Generic 8.5.0-rc10, with the `rc10-stages1` changes of the 2026-10-05 release and the changes of this release |
| `renodx-mfgunlock.addon64` | MFG Unlock `0.11+unified1q-20261015` | `e160b7e60687374a4a5d878e17cb37edb3a9a4a55542181025d5b8ab6c77df1a` | [nefh/MFGAmpereUnlock-RenoDx](https://github.com/nefh/MFGAmpereUnlock-RenoDx) tag `0.11`, with the changes of this release |
| `dlss5/nr/rtx50/nvngx_dlssnr.dll` | 310.8.0.0, NVIDIA-signed, `sm_120` only | `e16bcf15e16e13f527491cdf7845b2fe6521a738d8f7c9c721866a8496e1fc8e` | NVIDIA NR runtime |
| `dlss5/nr/rtx20-40/nvngx_dlssnr.dll` | 310.8.SF.0, not signed | `6eb209e764f39872625debd6abaf45e2bb6322f6f270f781f70c059ae30b3927` | community-modified runtime, [RHI release dlssnr-310.8.SF-v2](https://github.com/RankFTW/rhi-repo/releases/tag/dlssnr-310.8.SF-v2) |
| `dlss5/sl-2.14.1/` (11 files) | Streamline 2.14.1 plugins, DLSS-G 310.9.1, DLSS/DLSS-D/DeepDVC 310.5.2 | see `SHA256SUMS.txt` | NVIDIA Streamline / NGX |
| `DLSS5-Recovery 故障恢复.cmd`, `DLSS5-Clean-Recovery.ps1`, `DLSS5-RestoreDriver.exe` | — | see `SHA256SUMS.txt` | this release |
| `DLSS5-使用与恢复 Usage and Recovery.md`, `payload-manifest.json`, `release-manifest.json`, `SHA256SUMS.txt` | — | — | this release |

The package no longer contains `vk-present-adapter.ini` (its defaults are built into the bridge) or `EndfieldFGSwitch.exe` (replaced by the Streamline selection in the bridge).

## 2. How the pieces fit together

- **Generic** runs NVIDIA's NR runtime at up to three stages per frame: Render (before the game's upscaler), Upscaled (after it) and Present (on the finished frame), 0 to 4 layers each. On Vulkan it runs Render and Upscaled itself on the game's own DLSS evaluate; the Present layers are served by the bridge.
- **The bridge** runs the Present layers on the game's finished real frame at the input of the native DLSS Frame Generation (DLSS-G), once per real frame, and hands the result to DLSS-G through a GPU relay. This part is unchanged in principle since the 2026-10-05 release. New in this release, the bridge also owns everything that is specific to this game and this machine: which Streamline build is loaded, which NR runtime file is used for the GPU, the NVIDIA driver-profile frame-generation settings, the start-up health record, safe mode, the log folder and the anomaly log.
- **MFG Unlock** stays a separate add-on and is the only component that changes the frame-generation multiplier. On RTX 50 it only changes the multiplier and keeps NVIDIA's native frame-generation kernels. On RTX 20/30/40 it keeps the compatibility path of MFG Unlock 0.11.

The three add-ons talk only through versioned exports of the bridge (`DLSS5GetNrRuntimePathV2`, `DLSS5LogEventV1`, `DLSS5LogFullModeV1`) and of Generic (`DLSS5HookModeControlV1`, the stage protocol `DLSS5StageProtocolVersion`/`DLSS5GetStagePlan`). A missing or older partner is detected and reported; it does not lead to guessed behaviour.

## 3. Changes since vk-fgrelay-20261005

Each item gives the problem, the cause, the change and how it was verified. "In game" means a run of Arknights: Endfield with the logs kept as evidence (section 6). Several items were developed and verified in intermediate builds (named unified1 to unified1p in the evidence); their code is unchanged in unified1q unless stated.

### 3.1 One package for RTX 30/40/50; NR runtime chosen per GPU

- **Problem.** The NVIDIA-signed NR runtime 310.8.0.0 contains GPU code only for RTX 50 (`sm_120`) and refuses to start on older cards, so RTX 20/30/40 users had to find and install a different runtime themselves.
- **Change.** The package carries two runtimes: the signed 310.8.0.0 in `dlss5/nr/rtx50/` and the community-modified 310.8.SF.0 in `dlss5/nr/rtx20-40/`. The latter contains `sm_75`, `sm_86`, `sm_89` and `sm_120` code, and its host-side minimum-architecture check is lowered (static comparison with 310.8.0.0: `.rdata` identical, 25 bytes of `.text` differ: the immediate `0x1b0`→`0x160` and the per-architecture lower bounds). Generic asks the bridge for the runtime path through `DLSS5GetNrRuntimePathV2` with the LUID of the actual D3D12 device; the bridge maps the LUID to the physical NVIDIA GPU with NVAPI, picks the file by architecture (`0x1b0` → rtx50; `0x160`/`0x170`/`0x190` → rtx20-40), and checks only that file's SHA-256. An unknown device, a linked adapter that mixes architectures, a missing file or a wrong hash suspends NR for the session; no unverified file is used instead. The advanced section of the bridge panel shows the selected file and the check result.
- **Also:** on RTX 20/30 the bridge calls `cuInit(0)` before the first Vulkan instance is created (advanced setting, default on for RTX 20/30 and off for RTX 40/50). On an RTX 3070 Ti, the driver failed the game's first `vkCreateDevice` with -3 because CUDA could not initialise after the address space below 1 TB was used up by earlier instances; the game then retried without the NGX extensions and hid DLSS. Calling `cuInit` first made DLSS appear (verified on that PC with a diagnostic Vulkan layer before this was built into the bridge). After a first `vkCreateDevice` of -3 the bridge no longer reuses the consumed layer call chain for a retry; it suspends NR and records that early CUDA initialisation is needed on the next start.
- **Verified.** Offline: the selection fixture `test-nr-selection` passes. In game on RTX 5090: the rtx50 file is selected and NR initialises. RTX 30/40: confirmed by internal testers; no logs in this release.

### 3.2 Streamline 2.14.1 chosen in the panel, without replacing game files

- **Problem.** RTX 30 cards need the Streamline 2.14.1 plugins with DLSS-G 310.9.1 for MFG Unlock to accept the provider, and on RTX 50 the native multiplier change needs DLSS-G 310.9.x. The earlier tool `EndfieldFGSwitch.exe` replaced eight Streamline/NGX files in the game folder and wrote a fixed multiplier into the driver profile.
- **Change.** The bridge hooks `slInit` of the game's own `sl.interposer.dll` and, when selected, points `Preferences.pathsToPlugins` to `dlss5\sl-2.14.1\`. The game's interposer stays in place (it has the export `HGSetupCustomVulkan`). The folder must contain all 11 files with matching hashes, including the game's NGX snippets `nvngx_dlss.dll`, `nvngx_dlssd.dll` and `nvngx_deepdvc.dll`: without them the game's DLSS Super Resolution is unavailable and the Render/Upscaled NR stops (found in game during development). After a successful redirect, the bridge's own NGX sessions use the same folder as their feature search path (`NVSDK_NGX_FeatureCommonInfo`); before this fix the bridge's NGX context loaded DLSS-G 310.5.2 from the game folder next to the game's 310.9.1, and MFG Unlock's version check refused to change the multiplier. The choice is stored in an atomic state file and applies at the next start; one click saves it (no second confirmation), a pending change can be cancelled, and it is independent of the frame-generation switch and multiplier.
- **2026-10-15 game update.** The update replaced only `sl.interposer.dll` (still Streamline 2.10.3: old build `39925bb5…`, 581,096 bytes, with `HGSetupCustomVulkan`; new build `fabb19bb6495d743182c1bc51bcbbdc4481acc3dcaf445403f0b304a31a56ccc`, 569,472 bytes, NVIDIA-signed, without that export). The previous bridge accepted only the old hash, so `2.14.1 + DLSS-G 310.9.1` could not be selected; the game then used its own DLSS-G 310.5.2, MFG Unlock logged `provider not confirmed` on RTX 50, and the focus retention could not find its call site in the 2.10.3 `sl.dlss_g.dll`. unified1q accepts both interposer builds (list generated by `make-payload.py` into `common/payload.hpp`) and the panel says separately whether the interposer is unknown or a package file is missing or changed.
- **Verified.** In game on RTX 5090: both directions of the switch, including with frame generation off; the bridge and game NGX contexts both loading DLSS-G 310.9.1 from the subfolder; with unified1q and the updated interposer `[deployment] SL check: interposer=fabb19bb… known=1 package=verified` and `sl_valid=1 current=1`, Streamline plugins loaded from `dlss5\sl-2.14.1`, `[focus-fg] scoped focus gate ready`. Offline: 2,000 concurrent calls of the entry hooks, NGX ABI layout checks.

### 3.3 MFG Unlock: one build for all generations, reliable multiplier takeover

- **RTX 50 support.** MFG Unlock 0.11 identifies RTX 50 (`0x1b0`) as Unknown and passes the game's request through, so on RTX 50 it did not change the multiplier. A Blackwell profile now allows only the multiplier override; architecture gates, PTX/kernel replacement and timing patches stay disabled on Blackwell. The structural ceiling and the `slDLSSGSetOptions`/`slDLSSGGetState` entries are found from a successful `slGetFeatureFunction` query of the plugin module, because the host may cache `slGetPluginFunction` before MFG Unlock loads. On RTX 20/30/40 the 0.11 paths are kept. "Legacy software flip pacing" is kept as an option (default on for RTX 20/30/40 and off for RTX 50 when no choice is saved; restart required).
- **Start-up crash (two causes).** (1) MFG Unlock 0.11 writes `LoadFromDllMain=renodx-mfgunlock.addon64` into `ReShade.ini`; on the next start the game crashed in `UnityPlayer.dll+0x57D123` (same address with the unmodified 0.11). Endfield no longer gets this setting. (2) ReShade on Vulkan unloads add-ons at each `vkDestroyInstance`, Endfield creates and destroys about six instances while starting, and MFG Unlock was the only add-on that was really unloaded. When the game's Direct3D 9 probe thread was inside MFG Unlock's `LoadLibraryExW` detour at that moment, it returned into unmapped memory (`DEP at MFG+0x31183`). MFG Unlock now pins itself and the ReShade module for the process lifetime before installing its first hook, and repeats its module scan in an exported `AddonInit` on later ReShade cycles.
- **Multiplier not taking effect.** (1) The bridge's read-only hook on `slDLSSGSetOptions` (from the focus feature) conflicted with MFG Unlock's hook on the same function; it was removed, the actual focus entry is kept. (2) Hook installation enlists every other thread; a thread that exits between enumeration and `SuspendThread` made the whole transaction fail (`stage=enlist-thread error=5`; in one case the thread, started by `ACE-Base64.dll`, had exited 34.5 ms before enlistment). A thread whose exit is confirmed by its exit signal is now skipped; a live thread that cannot be suspended still aborts. (3) `UnityPlayer.dll` asks for the `slDLSSGSetOptions` pointer once and caches it, so one failed install lasted the whole session. The entry is now installed as soon as any DLSS-G entry is resolved from the plugin module, and the present path retries a failed install up to 6 times with growing intervals (60 to 480 frames), checking that the target module is still the same one.
- **Verified.** Offline: 271 policy checks and 10 start-up checks; the hook fixture (41 checks: exited thread skipped, live unsuspendable thread still aborts, exit code 259 treated as unknown, bounded retry); the unload race fixture: the build before the fix crashed 5 of 5 times at `+0x31183`, the fixed build returned normally 5 of 5 times; in six load/unload cycles the hooks were installed once and rescanned five times. In game on RTX 5090 with intermediate builds: 6x accepted with five generated frames per real frame (`idx=1/5..5/5`); switching between 2x, 3x, 4x, 5x and 6x, frame generation off/on and back to "Game default" worked; after a full reinstall the multiplier took over on first start once the NGX folder fix (3.2) was in. On PC B (intermediate build 1p) the first present-path retry installed the entry after an `error=5` failure. With unified1q: section 5.1.

### 3.4 Stutter at 2x: CPU throttle default changed to 2

- **Problem.** At 3840×2160, real 2x and NR Render 0 / Upscaled 1 / Present 1 on an RTX 5090, the game stuttered visibly although the average frame rate was about 65.
- **Cause.** The throttle (`Throttle=1`, introduced with the GPU relay to stop frames from queueing in front of the private NR queue) holds the game after `slReflexSleep` until the NR of the newest queued frame has completed. At 2x this alternated the simulation interval between about 14–17 ms and 57–60 ms. With `Throttle=0` the alternation disappeared (table in section 5.2).
- **Change.** The built-in default is now `Throttle=2` (wait for the NR of the frame before the newest queued one). This keeps a bounded wait for a real backlog. An explicit `Throttle` value in an existing `vk-present-adapter.ini` still wins; that is why users of the 2026-10-05 version should remove that file.
- **Verified.** Throttle 2 against 0 at 6x and 2x (section 5.2): no stable advantage of 0, and no difference the user could notice. The throttle did not actually wait in those runs (`waits=0`), so the benefit of keeping the bounded wait under backlog is not measured.

### 3.5 Present-stage NR switching itself off after a high layer count

- **Problem.** After about 7–8 NR layers in total and the automatic return to Upscaled 1 / Present 1, the Present stage either flickered continuously or switched itself off for the rest of the session (`feature creation did not complete within 4 s`, printed only 40 ms after the creation started).
- **Cause.** `ID3D12Fence::SetEventOnCompletion` cannot be cancelled. A wait that timed out left its registration on the fence; when the GPU later reached that value it signalled the auto-reset event while nobody waited. The next wait on the same event woke at once, found the fence not yet at its own target, and returned failure. In the PQ wait this produced failed output frames, a reset of the private upscaler history and a restarting stabiliser (the flicker); in the session rebuild it produced the false "4 s" failure and the refusal state. The pattern dates from the Vulkan integration of September; no device removal or TDR was logged.
- **Change.** `FenceDeadlineWait()` (`bridge/src/fence-deadline-wait.h`) uses one deadline for the whole call, treats every event wake only as a reason to read `GetCompletedValue()` again, and fails only when the deadline passes. It replaces the bodies of `Synth12WaitFence` and `FgCompositeWait`; `PqPassWait` follows the same rule. The return value of `FgIdentityCopy` is now checked and logged. No timeout, guard threshold, refusal policy or GPU-completion proof was changed.
- **Verified.** In game on RTX 5090 with the layer stack replayed 1→4→2→3→1: 11 waits really ran their full 900 ms and all recovered, 0 "4 s" failures, the refusal state was never entered, NR kept presenting; the flicker did not appear. Real D3D12 fence on WARP: the old logic gave 8 of 8 false failures at 0 ms after three timeouts, the new one 0; a fence that really does not complete still fails at the deadline (100 ms budget, 110 ms measured).
- **Packaging note.** This fix was verified in the intermediate builds 1m/1n, but the bridge of 1o/1p was built from a source tree that did not contain it. unified1q merges it into the production tree; its binary contains the fix's log strings.

### 3.6 Start-up protection, safe mode and layer confirmation

- **State and health.** Deployment state lives in `dlss5/state` (atomic write, generation number, checksum; fallback `%LOCALAPPDATA%/dlss5-bridge/<id>`). A start counts as healthy after 60 s of continuous presents, and with the Streamline subfolder selected also a successful redirect, the required modules from that folder and a successful game DLSS Super Resolution evaluate. A trial setting is undone after 1 recorded crash or 2 unexplained interrupted starts; a confirmed configuration enters safe mode after 2 crashes or 3 unexplained interrupted starts. Safe mode skips all NR/Streamline interventions without clearing settings and is left only by hand.
- **Layer confirmation.** A combination with 5 or more layers in total is saved as a pending trial before it is applied; the DLSS 5 Neural Rendering page then shows a warning and a 15-second confirmation, with "Keep" disabled for the first 2 s. The deadline keeps running with the menu closed, and every NR plan read rejects an expired combination; a restart restores the combination before an unconfirmed trial.
- **Verified in game (RTX 5090).** Timeout measured at 15.040 s and 14.785 s (0.25 s sampling) with the menu closed; "Keep" held the combination; safe mode entered (NR evaluations 0, Streamline from the game folder) and left by hand with all 125 Generic and 5 MFG Unlock settings unchanged. Offline: 474 state/plan checks including 125 stage combinations, storage failures and failure thresholds.

### 3.7 NVIDIA driver profile and recovery outside the game

- **Driver profile.** At start and when the Streamline selection changes, the bridge reads 8 public DLSS-G related driver settings (fixed and dynamic multiplier, target frame rate, FG model overrides) for `Endfield.exe`, no matter which tool wrote them, and where a local or inherited global override would interfere it writes a default into the Endfield profile only. The previous local value or its absence goes into two journals with generation numbers; restore only resets values that still equal what the bridge wrote. If the process lacks rights, a maintenance process started through the system `rundll32` asks for UAC for the same Windows account. Read or write errors never block the game. Undocumented private settings are not touched.
- **External removal.** `DLSS5-Recovery 故障恢复.cmd` option 9 runs `DLSS5-Clean-Recovery.ps1` and `DLSS5-RestoreDriver.exe` without loading ReShade or any add-on: it disables the ReShade Vulkan layer registration (if other existing programs are registered with the same ReShade, it lists them and continues only after the user answers Y), restores the journalled driver values, and moves the three add-ons, the `dlss5` folder and the related settings into `DLSS5-Recovery-Backups/<time-id>/` next to the game folder with SHA-256 checks. The result is reported as `render-components-removed; launcher-repair-required`. Other options: disable/enable the add-ons by renaming, enter/leave safe mode, restore driver changes, export diagnostics, reset the layer confirmation.
- **Verified.** In game on RTX 5090: the first start synchronised two settings (FG override and FG render preset selection got Endfield-local defaults; no local values existed before); the journal did not change on later starts. Option 9 was run by the user with intermediate build 1f1 (the recovery script was changed afterwards, in build 1o, for the new NR runtime folders; that change is covered by the offline checks only): the game then started without ReShade, the tool returned 0, no enabled ReShade registration or component remained, and the 82-file backup passed its hash check; after launcher repair and a new install (intermediate build 1h), NR and the multiplier worked again. Offline: 43 + 80 driver transaction checks, 15 maintenance-entry checks, 82 recovery checks each in PowerShell 7 and Windows PowerShell 5.1. A real UAC prompt and concurrent edits by another driver tool were not tested.
- **Fix of 2026-10-19: option 9 refused after the game was moved.** ReShade lists the programs it was installed for in `C:\ProgramData\ReShade\ReShadeApps.ini`. After the game had been reinstalled from `...\games\Endfield Game` to `...\games\Arknights Endfield`, that list still held the old path. The script counted every listed program other than the current game as another game sharing ReShade and stopped. The menu did not show which entry blocked it, offered no way to continue, and still printed the reminder to repair the game in the launcher. Now a listed program whose file no longer exists is reported as ignored (`stale_apps` in the plan) and does not block. Programs that exist are listed, and the script exits with code 3 before it backs up or changes anything. The menu then asks Y/N; Y runs the removal again with `-AllowSharedReShade`. The completion message appears only after success, and a failure says that files are retained. A missing or empty list still counts as unknown and needs the confirmation.
- **Verified (fix of 2026-10-19).** Offline: the recovery fixture passes 89 checks, including a nonexistent listed program, the current game written with `/` and different letter case, and a missing list. In an elevated shell with `DLSS5_TEST_ELEVATED_ENTRY=1` it passes 96: the real `-File -Action Restore` entry point listed the other program and exited with 3 before any change, and the HKLM ReShade registrations were unchanged. The menu flow, with a stub in place of the PowerShell script, passed 8 checks: Y reruns with `-AllowSharedReShade`, N cancels after one call, and a failure shows the failure message. The fixed script's read-only plan on PC Z's real game folder gave an empty `shared_apps` and the old `Endfield Game\Endfield.exe` path in `stale_apps`. Option 9 itself was not run with the fixed script.

### 3.8 Panels and defaults

- Each of the three panels has its language choice in the first row and shows one language at a time. Secondary settings are hidden behind "Advanced settings"; hiding them does not reset them.
- The bridge's Deployment section has the Streamline choice and the NR hook mode (`EnableHooks` 0/1/2). The value is written through Generic's own ReShade configuration interface (`DLSS5HookModeControlV1`), and the panel shows the current and the next-start value. The Present stage and the focus retention need mode 1; mode 2 (Generic's default, NGX only) was not accepted for the Present path.
- Generic's layer controls are grouped by stage and show the global layer number, because per-layer settings are stored by global number.
- New configurations enable the character mask and set Character / skin to 0 on every layer (independent skin stays on). On PC Z these two values largely removed dark patches on faces. Explicit saved values, including mask off and skin -1, are kept.
- Without `vk-present-adapter.ini` the bridge uses `Pipeline=2`, `Import=1`, `Throttle=2`.

### 3.9 Logs

- All logs of the suite are in `dlss5\dlss5_log\`: `dlss5-bridge.log` (first 8 MB of a session, then two rotating 4 MB overflow files; previous session kept as `.prev`), `dlss5-observe.log`, `dlss5-sl.log` (Streamline), `RenoDX-DLSS5-crash.log`, diagnostics exports, NR trace CSVs and the anomaly log. Logs left in the game root by older versions are moved there on first start. `ReShade.log` stays next to `ReShade.ini`, where ReShade writes it.
- The anomaly log collects bridge errors and failure-signature lines, all warnings and errors of Generic and MFG Unlock, every MFG Unlock decision that leaves the panel multiplier unapplied, the previous session's crash record, presents 100 ms or more apart, and 2 s without a present while the game is in the foreground. Bounded mode (default): first occurrence in full, repeats merged per 10 s, 16 MB per session, 64 MB and 30 files kept. Full mode (opt-in with a disk-use warning and confirmation): one line per occurrence, 64 MB parts, never deleted automatically, paused below 4 GB free disk space and resumed above 6 GB; stored in `dlss5\state\event-log-mode.txt`.
- Fixed: lines with non-ASCII paths were written empty (`%ls` is now formatted with a UTF-8 locale).
- Verified offline: 67 event-log checks; a fixture that loads the real bridge DLL in a temporary game folder (log migration, crash-record transfer, merged repeats, `GetLastError` preserved). In game on RTX 5090: full mode over 26 minutes wrote 301 KB.

## 4. Test machines

| Name | GPU | System | Driver | Build tested | Result |
| --- | --- | --- | --- | --- | --- |
| Z (development PC) | RTX 5090 | Windows 11 25H2 (build 26200), Ryzen 9 9950X3D, 5120×2160 | 616.92 | unified1q and all intermediate builds | section 3 and 5 |
| B (another user's PC) | RTX 5090 | Windows 10 22H2 (build 19045.6456), Ryzen 9 9950X3D, 3840×2160 at 240 Hz | 616.56 | unified1q; earlier 1p | section 5.4 |
| A | RTX 4080 | Windows 10 22H2 (19045), Core i7-13700K, 3840×2160 at 60 Hz | 616.92 | only the 2026-10-05 bridge and Generic with MFGAda 1.4.1/1.4.2 | historical, section 7 |
| C | RTX 3070 Ti | Windows 10, Ryzen 5 9600X | — | only the 2026-10-05 bridge and Generic with MFGAmpere 0.11 | historical, section 3.1 and 7 |
| internal testers | RTX 30 and RTX 40 | — | — | unified1q | confirmed working; logs not included |

## 5. Measurements and test results

All measurements are from PC Z (RTX 5090) unless stated. Frame data were taken with PresentMon 2.5.1 while the game was in the foreground and the user turned the camera at the same spot; the first second after gaining focus and the last 0.25 s were excluded. "Simulation interval" is `MsBetweenSimulationStart`, "display interval" `MsBetweenDisplayChange`, "software PC latency" `MsPCLatency` (not a photon-latency measurement).

### 5.1 unified1q in game

| Item | PC Z, 2026-10-16 (session of 26 min) | PC B, 2026-10-16 |
| --- | --- | --- |
| Streamline check | `interposer=fabb19bb… known=1 package=verified`, `sl_valid=1 current=1` | same |
| Streamline plugins | from `dlss5\sl-2.14.1` | same |
| NR runtime | 310.8.0.0, declared minimum driver 615.00, driver 616.92, initialised on the game's Vulkan device | 310.8.0.0, driver 616.56, initialised |
| MFG Unlock | `Blackwell native multiplier 6x accepted`; 0 lines `provider not confirmed` or `multiplier not applied` | `6x accepted` |
| DLSS-G per real frame | 5.00 generated frames (6x), also while the window was in the background with Present NR on | 5.00 |
| Frame rate | about 122–125 presents/s at 6x, p99 about 9 ms (Generic telemetry), about 21.3 real frames/s, NR Upscaled 1 / Present 1 | see 5.4 |
| Start-up | `startup health confirmed`; no crash marker | — |
| Anomaly log (full mode) | 301 KB in 26 min (about 0.7 MB/h); 1,950 of 2,014 lines were recovered `fg-composite` waits | — |

### 5.2 Throttle 2 against Throttle 0 (5120×2160, NR 0/1/1 at 67 %, one run each)

| Measure | 6x, Throttle 2 | 6x, Throttle 0 | 2x, Throttle 2 | 2x, Throttle 0 |
| --- | ---: | ---: | ---: | ---: |
| Foreground present records | 3974 | 3946 | 1531 | 1533 |
| Presents per second | 139.26 | 138.41 | 53.69 | 53.85 |
| Displayed frames per second | 116.84 | 109.58 | 53.65 | 53.61 |
| Simulation rate (per second) | 23.22 | 23.05 | 26.87 | 26.94 |
| Simulation interval P95 / P99 (ms) | 46.69 / 48.31 | 46.96 / 49.96 | 40.80 / 43.44 | 40.55 / 42.55 |
| Simulation intervals over 50 ms | 1/662 | 7/657 | 0/766 | 1/767 |
| Display interval P95 / P99 (ms) | 8.40 / 16.67 | 16.67 / 16.70 | 25.02 / 33.32 | 25.03 / 31.84 |
| Software PC latency median / P95 (ms) | 152.20 / 176.47 | 155.48 / 180.90 | 98.33 / 115.05 | 98.51 / 116.51 |

The user reported no noticeable difference between 2 and 0. Throttle 2 did not wait in these runs (`waits=0`).

The 2x stutter that led to this change (3840×2160, real 2x, NR 0/1/1):

| Measure | Throttle 1 (old default) | Throttle 0 |
| --- | ---: | ---: |
| Present interval mean / P95 (ms) | 15.654 / 23.418 | 14.265 / 15.645 |
| Simulation interval mean / P95 / max (ms) | 31.309 / 58.935 / 62.743 | 28.528 / 31.494 / 37.317 |
| Simulation intervals over 50 ms | 209/911 (22.94 %) | 0/1000 |
| Display interval P95 / max (ms) | 25.016 / 41.671 | 24.991 / 41.644 |
| Software PC latency median / P95 (ms) | 98.594 / 114.427 | 81.716 / 96.348 |

The user confirmed that the stutter almost disappeared with 0. A later run with Throttle 2 at 5120×2160, 6x, NR 0/1/1 had a simulation interval of 45.785 / 48.475 / 54.946 ms (mean / P95 / max), 9 of 625 intervals over 50 ms, and a software PC latency median of 164.673 ms; the throttle did not wait in that run either.

### 5.3 Offline fixtures, re-run on 2026-10-19 against the production source and the shipped bridge

| Fixture | Covers | Result |
| --- | --- | --- |
| `test.cmd` (state and loader) | failure thresholds, safe mode, atomic saves, layer trial and timeout, 125 stage plans, NGX init path, panel callbacks; concurrent resolver and Streamline entry calls | 474 checks; 2,000 concurrent calls, 0 errors |
| `test-nr-selection` | NR runtime choice per GPU architecture | all checks passed |
| `test-event-log`, `test-bridge-logdir` | anomaly log; the shipped bridge DLL loaded in a temporary game folder | 67 checks; log-folder fixture completed |
| MFG Unlock `native-policy` | Blackwell multiplier ABI, fallback, no-patch limits, legacy profiles; start-up policy | 271 + 10 assertions |
| `test-drs` | driver-profile transactions, FG settings, maintenance entry | 43 + 80 + 15 checks |
| `test-fence-deadline-wait` | real D3D12 fence on WARP | old logic 8 of 8 false failures after three timeouts, new logic 0; a fence that never completes fails at 109 ms for a 100 ms budget |
| `test-deploy.ps1` (Windows PowerShell 5.1) | deployment and bridge-free disable/enable | 30 checks |
| `test-clean-recovery.ps1` (Windows PowerShell 5.1) | external removal, including the fix of 2026-10-19 | 89 checks; 96 with the opt-in elevated entry-point check |

Every fixture exited with 0 (`offline-rerun/summary.json` in the evidence archive). PowerShell 7 is not installed on PC Z, so the PowerShell 7 runs of the two PowerShell fixtures were not repeated. The fixtures work in temporary folders with mocked registry, driver and process interfaces and do not touch the game folder, the driver profile or ReShade's registration; the only exception is the opt-in elevated recovery check, which writes one temporary HKCU value and removes it.

### 5.4 Frame-time curve case on PC B

On PC B the ReShade frame-time curve was saw-toothed with every build. The bridge and Generic counters showed about 11.7–11.8 presents per real frame at 6x (PC Z: 5.85), while DLSS-G still generated 5 frames per real frame. The cause was NVIDIA App "Smooth Motion" enabled for `Endfield.exe`, which adds driver-level frame generation on top of DLSS-G; after the owner turned it off the curve became flat at once (reported by the owner; the logs of that run were not collected). Smooth Motion was not found in the driver profile (setting `0xB0D384C0` was absent on both PCs), so the add-on cannot detect it yet. A ratio of presents to real frames clearly above the DLSS-G multiplier indicates extra frame generation.

## 6. Limitations and open issues

- **Hardware coverage.** In-game logs exist only for RTX 5090. RTX 30 and RTX 40 are confirmed by internal testers without logs in this release, and no measurement in this document comes from those cards. RTX 20 was not tested. Only Arknights: Endfield on Vulkan was tested.
- **NR runtime for RTX 20/30/40** is a community-modified, unsigned file; Generic logs it as a named non-reference build. NVIDIA may block or change this path in future drivers.
- **Driver minimum.** 615.00 is the minimum the NR runtime declares. Generic calls the runtime directly and does not enforce it; the real lower limit is unknown. Tested: 616.56 and 616.92.
- **Windows minimum.** Windows 10 2004 (19041) is Streamline 2.14.1's requirement for frame generation (with hardware-accelerated GPU scheduling). Tested: Windows 10 build 19045 and Windows 11 build 26200.
- **Future game updates.** Only the two known `sl.interposer.dll` builds are accepted. With another build the bridge keeps the game's Streamline 2.10.3; then MFG Unlock cannot change the multiplier on RTX 50 and frame generation stops while the window is unfocused, until a new release adds the build. A redirect that fails at start is undone at the next start by the health policy.
- **MFG Unlock multiplier.** On PC B with intermediate build 1p (2026-10-14) the multiplier fell back to 2x three times without any MFG Unlock log line; the path is not identified. Build 1q writes every unapplied decision to the anomaly log; in the logged 1q sessions on PC Z and PC B the multiplier stayed at 6x. Calls that bypass MFG Unlock's hooks are not recorded. The thread-exit and retry fixes are verified by fixtures and by one in-game retry on PC B. Historical rate before those fixes: 3 failed sessions out of 21 on PC Z.
- **Wait chain.** The same single-wake wait pattern on the Vulkan side (`SynthOfaVkFrame`, `FgRelayRecoverNrWaits`, two calls in `synth.inc`) was not reviewed one by one. The cause of the first real 900 ms PQ timeouts at high layer counts (memory or queue) is not found. Higher VRAM use after high layer counts and a lost NR hotkey after the earlier failure were reported and are not resolved.
- **EnableHooks 2** is not accepted for the Present path; mode 1 is required.
- **Full anomaly log** can grow to several GB per day if an anomaly repeats every frame.
- **Not tested:** a real UAC prompt for the driver maintenance, another driver tool writing at the same time, external removal on a PC other than Z, a real removal with the recovery script of 2026-10-19 (section 3.7), removal after an update that breaks ReShade itself, multi-GPU PCs.
- **Starting the game again within a few seconds after closing it** can be blocked by the anti-cheat; waiting 5–10 s avoids it. Its exact cause is not identified.
- **Known visual issues** from the install guide: the game's photo camera does not include Present-stage NR; model shadows can show through after reviving, after map loads and in some UI screens; NR can turn off and on briefly when UI screens change quickly; rare repeated flashing that a restart fixes.
- **Other add-ons and overlays.** NVIDIA game filters and RTX HDR can stop the add-on; Smooth Motion breaks pacing (5.4).
- **Rebuilding** needs Visual Studio 2022 Build Tools at the path in the build script and, for Generic, a RenoDX `external` folder whose exact commit was not recorded; rebuilt DLLs differ in hash because of time stamps (see `BUILDING.md` in the source archive).

## 7. Historical results from other PCs (2026-10-05 bridge and Generic)

These results come from the 2026-10-05 bridge and Generic with older MFG Unlock builds. They explain several defaults of this release but are not tests of unified1q.

- **RTX 4080 (PC A), MFG 3–6x.** Generated frames bunched at the start of each group, then about 30 ms gaps. With MFG Unlock's software pacing (`ForceFlipMeteringOff=1`, the "Legacy" option) the share of GPU-ready frames within ±25 % of the ideal interval went from 28 % to 87 % at 6x and gaps went from 17 % to 0 %; first and last present were about 45 ms and 35 ms later. It works only when set before DLSS-G creates its context (restart). It should not be combined with the NVIDIA control panel frame-rate limit. This is why Legacy defaults to on for RTX 20/30/40.
- **Periodic stutter on Windows 10 with Intel hybrid CPUs (PC A).** Every 1–2 minutes, 15–20 s of stutter, also with ReShade disabled. Cause: all three Windows 10 power plans park the performance cores (`CPMINCORES1`=0 %) while the game pins its main and render threads to them. `powercfg /setacvalueindex SCHEME_CURRENT SUB_PROCESSOR CPMINCORES1 100` followed by `powercfg /setactive SCHEME_CURRENT` removed it. Not related to this add-on.
- **RTX 3070 Ti (PC C).** The `vkCreateDevice` -3 and early `cuInit` finding of section 3.1; the Present stage needs `vk_mirror=1` and `ngx_loader=0` (both defaults); the bridge's former `slDLSSGSetOptions` observer hook conflicted with MFG Unlock (removed in this release).

## 8. Files

### 8.1 Release assets

| Asset | Content |
| --- | --- |
| `unified-20261015q-candidate.zip` | the ready-to-use package (section 1) |
| `unified-20261015q-source.zip` | the complete production source tree of the three add-ons and the shared headers, the build/test/packaging scripts, the recovery script and driver-restore tool sources, and the NVAPI SDK; `BUILDING.md` inside explains the build |
| `unified-20261015q-evidence.zip` | the logs, captures and analyses behind every number in sections 3 and 5, and the logs of the 2026-10-19 offline re-run; `README.md` inside lists each folder |
| `SHA256SUMS.txt` | SHA-256 of the three archives |

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| `unified-20261015q-candidate.zip` | 280,294,193 | `f13a454e799ce8cee52a0cbcbe980ce5a0e923113b6dbcc2762794cd6177a18a` |
| `unified-20261015q-source.zip` | 20,351,303 | `b2b31d024adc5336dd9061e1e7f0eac7f6956a214a92261ab4ac05703a867f0e` |
| `unified-20261015q-evidence.zip` | 4,793,015 | `9fdbbc615fc51c368badd1c9011b9bfad61523d807204887e6860f51f8f8e11e` |

### 8.2 Repository content added in this release

| Path | Content |
| --- | --- |
| `README.md` | this document |
| `docs/history/README-vk-fgrelay-20261005.md` | the previous README |
| `docs/unified-20261015q/USAGE-AND-RECOVERY.md` | a copy of the bilingual technical document from the package |
| `merge/unified-20261015q/` | patches and full copies of every changed file, for merging into existing trees (see its `README.md`) |
| `docs/unified-20261015q/EVIDENCE.md` | the index of the evidence archive |
| `tools/release-unified-20261015q/` | the scripts that staged this release, checked the patches and re-ran the offline fixtures |

Files of the earlier releases stay where they were. `docs/INSTALL.md` and `docs/VULKAN-FG-INPUT-NR.md` describe the 2026-10-05 release; for this release, use the install guide above.

### 8.3 Merging

`merge/unified-20261015q/` has one patch per component for each of two bases, so that it can be applied either to the state of the 2026-10-05 release or to the upstream projects directly:

| Component | Patch | Base |
| --- | --- | --- |
| bridge | `bridge/bridge-since-vk-fgrelay-20261005.patch` | Yukikaze20170315/dlss5-bridge `835c5bf` (the 2026-10-05 release) |
| bridge | `bridge/bridge-since-NIGos-main-d1cc508.patch` | NIGos/dlss5-bridge `main` at `d1cc508` |
| Generic | `generic/generic-since-rc10-stages1-c3dc63c.patch` | Generic `rc10-stages1` at `c3dc63c` (the 2026-10-05 release) |
| Generic | `generic/generic-since-v8.5.0-rc10-9bb6c0f.patch` | Generic v8.5.0-rc10 at `9bb6c0f` |
| MFG Unlock | `mfg/mfg-since-MFGAmpereUnlock-0.11.patch` | nefh/MFGAmpereUnlock-RenoDx tag `0.11` (`53b3770e`), folder `src/addons/mfgunlock` mapped to `addon/` |
| shared | `common/*.hpp` | new files |

Every patch was applied to a fresh copy of its base and the result compared byte for byte (line endings normalised to LF) with the production tree; all five matched. The bridge and Generic include the shared headers by relative path (`../../common/` from `bridge/src/`, `../../../../common/` from `generic/src/addons/dlss5/`), so place `common/` accordingly or adjust those include lines. `merge/unified-20261015q/README.md` lists every changed file and the order of application.

## 9. Licenses and third-party components

The bridge (NIGos/dlss5-bridge), Generic (RenoDX), and MFG Unlock (nefh/MFGAmpereUnlock-RenoDx, which builds on mavismmg/MFGAdaUnlock-RenoDx) are MIT licensed; their license files are in the source archive. The source archive also contains MinHook, Detours, the ReShade add-on headers with ImGui, the NVIDIA Streamline headers and the NVAPI SDK, each with the license notice it came with. It does not contain the NVIDIA NGX SDK headers, which carry a proprietary notice (`BUILDING.md` says where to get them). The NR runtimes, the Streamline plugins and the NGX snippets in the package are NVIDIA binaries and remain subject to NVIDIA's terms; the RTX 20–40 NR runtime is a community modification of an NVIDIA binary.

## Acknowledgements

RenoDX and the RenoDX DLSS 5 Generic authors; NIGos for dlss5-bridge; nefh and mavismmg for MFG Unlock; the RHI project for hosting the NR runtimes; the internal testers, and the owners of the test PCs.
