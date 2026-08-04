# CN-WoW Patcher

中文客户端载入界面修复工具（ClientDisplayExpansion 6 → 11），C / Win32 原生实现，单文件 exe **~120 KB**，零依赖。

国服客户端被暴雪锁在 Legion（值 6）载入界面，其他语言都是当前版本（值 11）。本工具通过内存 patch 解除限制，支持固定 12.0 或混乱模式随机。

## 功能

- **载入界面修复**：内存修改 Locale 记录（位打包，byte3 = 0x40 + 值×2），默认固定值 11（12.0 至暗之夜界面）
- **战网检测**：未运行仅提示（可启动但只能看登录界面）
- **游戏路径自动侦测**：运行中进程 → 注册表（卸载表 InstallLocation / Blizzard 键）
- **托盘**：关闭窗口隐藏到右下角，右键菜单彻底退出
- **多进程 patch**（当前建议单进程使用）

## 使用

双击 `CN-WoW Patcher.exe`（无需安装任何运行时），点「进入游戏」或自行通过战网启动游戏，状态区变绿「已生效」即可。

注意：支持多开，每个进程独立处理；若多开或重开后未生效，请反馈。

## 编译

需要 [w64devkit](https://github.com/skeeto/w64devkit)（免安装 MinGW）：

```sh
windres -c 65001 src/resource.rc -O coff -o resource.o
gcc -O2 -s -municode -finput-charset=UTF-8 -o "CN-WoW Patcher.exe" src/patcher.c \
    resource.o -luser32 -lkernel32 -ladvapi32 -lshell32 -lcomctl32 -lole32 -loleaut32 -lwbemuuid -mwindows
```

## 文件

- `src/patcher.c` — C 源码（Win32 GUI，约 700 行）
- `CN-WoW Patcher.exe` — 编译产物（不在仓库，由 GitHub Actions 自动构建，从 Releases 下载）
- `CN-WoW Patcher.pyw` — Python/tkinter 版本（参考，需要 Python 3 + pystray + pillow + pywin32；WMI 事件驱动侦测游戏启动，非管理员自动回退轮询）

## 技术要点

- Locale 表 WDC5 记录：前缀 `64 62 96` + byte4==0x01 为 zhCN 记录；ClientDisplayExpansion 位打包在 byte3
- 值映射：byte3 = 0x40 + 值×2（0x4C=6 Legion，0x56=11 当前版本）
- 代码段被 Warden 反作弊保护，只能改数据段（堆区可写）
- 首屏（启动瞬间）读取在 patch 可介入窗口之外，热启动可能仍是 6
