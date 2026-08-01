# CN-WoW Patcher

中文客户端载入界面修复工具（ClientDisplayExpansion 6 → 11），C / Win32 原生实现，单文件 exe **~120 KB**，零依赖。

国服客户端被暴雪锁在 Legion（值 6）载入界面，其他语言都是当前版本（值 11）。本工具通过内存 patch 解除限制，支持固定 12.0 或混乱模式随机。

## 功能

- **载入界面修复**：内存修改 Locale 记录（位打包，byte3 = 0x40 + 值×2），默认固定值 11（12.0 至暗之夜界面）
- **混乱模式**（默认关闭）：随机 0-11，随机到特定值触发金色彩蛋：
  - 值 1（2.0 TBC）→ 「光源远征了…」
  - 值 2（3.0 WLK）→ 「龙飞走了，堡垒化了…」
  - 值 6（7.0 Legion）→ 「扎昆守护着你！」
- **战网检测**：未运行仅提示（可启动但只能看登录界面）
- **游戏路径自动侦测**：运行中进程 → 注册表（卸载表 InstallLocation / Blizzard 键）
- **秘籍 Log 窗口**：`↑↑↓↓←→←→BA`（Konami）或 `whosyourdaddy` 开启实时日志；输入错误自动重置
- **托盘**：关闭窗口隐藏到右下角，右键菜单彻底退出
- **多进程 patch**（当前建议单进程使用）

## 使用

双击 `CN-WoW Patcher.exe`（无需安装任何运行时），点「进入游戏」或自行通过战网启动游戏，状态区变绿「已生效」即可。

注意：**暂仅支持单进程，等待游戏完全退出后再启动**（热启动首屏可能仍是 6，冷启动正常）。

## 编译

需要 [w64devkit](https://github.com/skeeto/w64devkit)（免安装 MinGW）：

```sh
gcc -O2 -municode -finput-charset=UTF-8 -o "CN-WoW Patcher.exe" src/patcher.c \
    -luser32 -lkernel32 -ladvapi32 -lshell32 -lcomctl32 -mwindows
```

## 文件

- `src/patcher.c` — C 源码（Win32 GUI，约 700 行）
- `CN-WoW Patcher.exe` — 编译产物（不在仓库，由 GitHub Actions 自动构建，从 Releases 下载）
- `LocalePatcherGUI.pyw` — Python/tkinter 版本（参考，需要 Python 3 + pystray + pillow + pywin32；WMI 事件驱动侦测游戏启动，非管理员自动回退轮询）

## 技术要点

- Locale 表 WDC5 记录：前缀 `64 62 96` + byte4==0x01 为 zhCN 记录；ClientDisplayExpansion 位打包在 byte3
- 值映射：byte3 = 0x40 + 值×2（0x4C=6 Legion，0x56=11 当前版本）
- 代码段被 Warden 反作弊保护，只能改数据段（堆区可写）
- 首屏（启动瞬间）读取在 patch 可介入窗口之外，热启动可能仍是 6
