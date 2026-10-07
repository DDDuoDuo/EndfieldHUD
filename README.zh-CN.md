# EndfieldHUD

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md)

一款《明日方舟：终末地》风格的 macOS 菜单栏应用。按下快捷键，就能打开带有层次与动态效果的 HUD，使用便笺、文件暂存、计时、音量控制和系统状态等功能。

**整合测试分支：**本分支将新的 Watch 界面与稳定版 1.0.1 的功能及存档格式整合。测试本分支时，请打开[本分支通过的 Build and test 构建](https://github.com/DDDuoDuo/EndfieldHUD/actions/workflows/build.yml?query=branch%3Acodex%2Fendfield-hud-integration)，下载其中的 **EndfieldHUD-macOS-…** 产物。下方的正式版下载链接和预览属于稳定版，不是本分支。

**[下载 v1.0.1 — EndfieldHUD-1.0.1-build11-macOS.dmg](https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.0.1/EndfieldHUD-1.0.1-build11-macOS.dmg)** · [所有版本](https://github.com/DDDuoDuo/EndfieldHUD/releases)

![EndfieldHUD 电源页面，使用示例读数](docs/media/overview.png)

*下方预览均来自实际应用，使用演示内容与示例读数，不包含用户的私人便笺、剪贴板历史或文件。*

## 安装

1. 下载上方的 DMG。如果旧版正在运行，先从菜单栏退出。
2. 打开 DMG，将 **EndfieldHUD.app** 拖入 **Applications（应用程序）**。
3. 推出磁盘映像，再从“应用程序”打开 EndfieldHUD。图标会出现在菜单栏。
4. 按 **Ctrl + 反引号（`）** 打开 HUD。

此版本使用**本地临时签名（ad hoc），未经过 Apple 公证**。如果 macOS 因无法验证开发者而阻止打开，请先尝试打开已安装的应用；确认信任此下载后，在**系统设置 → 隐私与安全性 → 仍要打开**中仅批准 EndfieldHUD，再确认打开。旧版 macOS 使用“系统偏好设置 → 安全性与隐私”。无需关闭 Gatekeeper。参见 [Apple 操作说明](https://support.apple.com/zh-cn/102445)。

应用面向 **Apple 芯片 macOS 11 及以上**、**Intel macOS 10.15.4 及以上**构建，部分功能需要更新的系统。测试主要在 macOS 15.7.4 的 M2 MacBook Air 上进行，其他机型和旧系统的验证仍有限。

## 先试试这些

- **打开或隐藏：**按 Ctrl + 反引号，也可使用菜单栏的**打开浮层**。Esc 会先退出当前编辑或选择，再关闭 HUD。
- **切换功能：**点击两侧按钮；右侧列表可滚动。存储和活动监视器位于中央下方，左下角名片可打开个人资料。
- **切换语言：**进入**系统 → 语言**，在带勾选标记的列表中选择**跟随系统、English、简体中文、繁體中文或日本語**。四种界面语言即时生效，“跟随系统”按 macOS 语言显示。[预览](docs/media/languages.png)
- **调整外观：**“显示”可设置大小、位置、颜色、主题、模糊、透视和减少动态效果；“快捷键”可更改呼出方式。
- **退出应用：**在菜单栏选择**退出 EndfieldHUD**，或点击名片旁的红色按钮并确认。隐藏 HUD 后，应用仍会运行。

<details>
<summary>观看展开、收起、倾斜与页面切换</summary>

展开、收起与随鼠标倾斜：

![HUD 展开、收起与倾斜演示](docs/media/readme/opening.gif)

功能页面之间的切换：

![HUD 页面切换演示](docs/media/readme/modules.gif)

</details>

## 功能一览

点击预览查看对应页面，功能名称链接到详细说明。

| 页面 | 可以做什么 | 预览 |
| --- | --- | --- |
| 电源 | 查看可读取的电池与充电状态；可开启独立于主 HUD 的充电提醒。 | [电源](docs/media/overview.png) |
| [便笺](docs/notes-canvas.md) | 添加文字、待办和图片，自由移动、缩放，并固定在其他 HUD 页面。 | [便笺](docs/media/notes.png) |
| [文件暂存架](docs/file-shelf.md) | 放入文件或文件夹，快速预览，再一起拖出。移除暂存项不会删除原文件。 | [文件架](docs/media/shelf.png) |
| [剪贴板](docs/clipboard-cache.md) | 找回最近复制的文字、链接、图片和文件，固定常用项目。历史在退出应用后清空。 | [剪贴板](docs/media/clipboard.png) |
| [音量](docs/audio-and-work-mode.md) | 选择音频设备，调整设备支持的音量、静音与左右平衡。独立应用音量为实验功能，需要 macOS 14.2+。 | [音量](docs/media/volume.png) |
| [工作模式](docs/audio-and-work-mode.md#work-mode) | 使用倒计时或秒表，提供 5／30／60 分钟预设和自定义时长。隐藏 HUD 后继续计时。 | [工作模式](docs/media/work-mode.png) |
| [事件日志](docs/event-log.md) | 浏览和筛选本机 HUD 事件，例如计时、文件暂存操作与设备变化。 | [事件日志](docs/media/event-log.png) |
| [存储](docs/storage-and-activity.md#storage) | 查看已用和可用空间，刷新读数，或打开 macOS 存储设置。 | [存储](docs/media/storage.png) |
| [活动监视器](docs/storage-and-activity.md) | 查看 CPU、内存、网络与磁盘图表，以及可排序的应用列表。无法读取的数据显示为横线。 | [活动监视器](docs/media/activity.png) |
| [+ 添加应用](docs/app-shortcuts.md) | 为已安装应用保存快捷入口，自定义名称与图标，从 HUD 中打开。 | [应用快捷方式](docs/media/apps.png) |
| [个人名片](docs/personal-profile.md) | 编辑名称、头像、背景、介绍与日期，查看累计工作模式时长。 | [个人名片](docs/media/profile.png) |
| [地图](docs/map.md) | 平移、缩放离线地球地图，添加自己的标记。不会读取定位，也不加载街道地图。 | [地图](docs/media/map.png) |
| 系统 | 选择语言、显示器、登录启动及其他行为。 | [语言选择](docs/media/languages.png) |
| 显示 | 调整外观、应用／菜单栏图标和电池提醒。大小与位置修改支持限时恢复。 | [设置](docs/media/settings.png) |
| 快捷键 | 直接在 HUD 中录入新的呼出快捷键。 | [设置](docs/media/settings.png) |
| 关于 | 查看版本与鸣谢，检查更新，设置是否自动安装更新。 | [设置](docs/media/settings.png) |

## 权限与本机数据

| 功能 | 需要什么 |
| --- | --- |
| 独立应用音量 | **macOS 14.2+**；启用时需要**系统音频录制**权限。音频仅在内存中处理，不保存。设备支持有限，参见[音量说明](docs/per-app-audio.md)与 [Apple 权限说明](https://support.apple.com/en-in/guide/mac-help/mchl2844ecab/mac)。 |
| 文件与图片 | 访问你选择、粘贴或拖入的项目。文件架保存引用；图片便笺与个人名片图片保存本机副本。 |
| 更新提醒 | 通知权限可选。即使拒绝通知，菜单栏和“关于”仍会显示更新信息。 |
| 登录启动 | 先安装到“应用程序”；macOS 可能要求在登录项中批准。 |
| 自动专注模式 | 本整合分支在 **macOS 11+** 上优先使用控制中心，需要你授予**辅助功能**权限且系统控件可识别；**macOS 13+** 上已配置的快捷指令作为后备。稳定版 v1.0.1 下载仍需自行创建这两个快捷指令。参见[专注模式设置](docs/audio-and-work-mode.md#work-mode)；没有自动专注也能使用计时器。 |

便笺、文件引用、个人名片、地图标记与事件历史保存在本机。剪贴板历史只保存在内存中，退出后清空。检查和下载更新会连接 GitHub，使用应用无需账号。[设置说明](docs/settings.md)

## 项目与鸣谢

EndfieldHUD 由 **DDDuoDuo** 开发，是受《明日方舟：终末地》启发的**非官方同人项目**，与游戏制作方无关联，也未经其认可。

原创代码采用 [MIT 许可证](LICENSE)。游戏图片、品牌标识及其他第三方素材保留各自的权利与许可。视觉参考、素材、地图数据和依赖来源见 [Credits／鸣谢](CREDITS.md)。

[反馈问题](https://github.com/DDDuoDuo/EndfieldHUD/issues)
