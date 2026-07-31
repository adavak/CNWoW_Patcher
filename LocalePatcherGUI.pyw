# -*- coding: utf-8 -*-
import ctypes
import ctypes.wintypes as wt
import threading
import time
import subprocess
import os
import random
import string
import tkinter as tk
from tkinter import messagebox
import pystray
from PIL import Image, ImageDraw, ImageFont

k32 = ctypes.windll.kernel32
k32.OpenProcess.argtypes = [wt.DWORD, wt.BOOL, wt.DWORD]
k32.CloseHandle.argtypes = [wt.HANDLE]
k32.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
k32.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
k32.Process32FirstW.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.Process32NextW.argtypes = [wt.HANDLE, ctypes.c_void_p]
k32.QueryFullProcessImageNameW.argtypes = [wt.HANDLE, wt.DWORD, ctypes.c_wchar_p, ctypes.POINTER(wt.DWORD)]

PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
PROCESS_VM_WRITE = 0x0020
MEM_COMMIT = 0x1000
MEM_PRIVATE = 0x20000
BATTLE_NET_NAMES = ("battle.net.exe", "battle.net")

BG = "#16171d"
PANEL = "#1e2026"
PANEL_LN = "#2a2d35"
TEXT = "#f8f8f8"
MUTED = "#8f96a3"
ACCENT = "#0074e0"
ACCENT_H = "#44a4fc"
GREEN = "#35c47a"
ORANGE = "#e8943a"
GRAY = "#5c6470"
GOLD = "#ffd700"
FONT = "Segoe UI"
APP_VER = "v1.3"
APP_AUTHOR = "Adavak"
PREFIX = b"\x64\x62\x96"
KEY_SEQ = ("up", "up", "down", "down", "left", "right", "left", "right", "b", "a")
WORD_WANT = "whosyourdaddy"


class MEMORY_BASIC_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
        ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD),
        ("RegionSize", ctypes.c_size_t), ("State", wt.DWORD),
        ("Protect", wt.DWORD), ("Type", wt.DWORD),
    ]


class PROCESSENTRY32(ctypes.Structure):
    _fields_ = [("dwSize", wt.DWORD), ("cntUsage", wt.DWORD), ("th32ProcessID", wt.DWORD),
                ("th32DefaultHeapID", ctypes.c_void_p), ("th32ModuleID", wt.DWORD),
                ("cntThreads", wt.DWORD), ("th32ParentProcessID", wt.DWORD),
                ("pcPriClassBase", ctypes.c_long), ("dwFlags", wt.DWORD),
                ("szExeFile", ctypes.c_wchar * 260)]


def find_processes(names):
    pids = []
    snap = k32.CreateToolhelp32Snapshot(0x00000002, 0)
    if snap in (0, -1):
        return pids
    pe = PROCESSENTRY32()
    pe.dwSize = ctypes.sizeof(PROCESSENTRY32)
    if k32.Process32FirstW(snap, ctypes.byref(pe)):
        while True:
            if pe.szExeFile.lower() in names:
                pids.append(pe.th32ProcessID)
            if not k32.Process32NextW(snap, ctypes.byref(pe)):
                break
    k32.CloseHandle(snap)
    return pids


def battle_net_running():
    return bool(find_processes(BATTLE_NET_NAMES))


def detect_wow_path():
    for pid in find_processes(("wow.exe", "wow", "wowclassic.exe")):
        h = k32.OpenProcess(PROCESS_QUERY_INFORMATION, False, pid)
        if not h:
            continue
        try:
            buf = ctypes.create_unicode_buffer(1024)
            size = wt.DWORD(1024)
            if k32.QueryFullProcessImageNameW(h, 0, buf, ctypes.byref(size)):
                p = buf.value
                if p.lower().endswith("wow.exe") and os.path.isfile(p):
                    return p
        finally:
            k32.CloseHandle(h)
    try:
        import winreg
        for key_path, val in (
            (r"SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\World of Warcraft", "InstallLocation"),
            (r"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\World of Warcraft", "InstallLocation"),
            (r"SOFTWARE\WOW6432Node\Blizzard Entertainment\World of Warcraft", "InstallPath"),
            (r"SOFTWARE\Blizzard Entertainment\World of Warcraft", "InstallPath"),
        ):
            try:
                with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, key_path) as k:
                    inst, _ = winreg.QueryValueEx(k, val)
                    for sub in ("_retail_", ""):
                        full = os.path.join(inst, sub, "Wow.exe")
                        if os.path.isfile(full):
                            return full
            except OSError:
                continue
    except Exception:
        pass
    return None


def get_private_regions(h):
    regions = []
    addr = 0
    mbi = MEMORY_BASIC_INFORMATION()
    while True:
        r = k32.VirtualQueryEx(h, ctypes.c_void_p(addr), ctypes.byref(mbi), ctypes.sizeof(mbi))
        if r == 0:
            break
        base = mbi.BaseAddress or 0
        size = mbi.RegionSize
        addr = base + size
        if mbi.State != MEM_COMMIT or size > 0x80000000:
            continue
        if mbi.Type != MEM_PRIVATE:
            continue
        if not (mbi.Protect & (0x04 | 0x40)):
            continue
        regions.append((base, size))
    return regions


def scan_region(h, base, size, target):
    patched = 0
    want = 0x40 + target * 2
    off = 0
    while off < size:
        chunk = min(4 * 1024 * 1024, size - off)
        buf = ctypes.create_string_buffer(chunk)
        br = ctypes.c_size_t(0)
        ok = k32.ReadProcessMemory(h, ctypes.c_void_p(base + off), buf, chunk, ctypes.byref(br))
        if ok and br.value:
            data = bytes(buf.raw[:br.value])
            idx = 0
            while True:
                i = data.find(PREFIX, idx)
                if i < 0:
                    break
                if i + 4 < len(data) and data[i + 4] == 0x01:
                    tgt = base + off + i + 3
                    if data[i + 3] != want:
                        val = ctypes.c_ubyte(want)
                        bw = ctypes.c_size_t(0)
                        if k32.WriteProcessMemory(h, ctypes.c_void_p(tgt), ctypes.byref(val), 1, ctypes.byref(bw)):
                            patched += 1
                idx = i + 1
        off += chunk
    return patched


def patch_all(targets):
    results = []
    for pid in find_processes(("wow.exe", "wow", "wowclassic.exe")):
        if pid not in targets:
            continue
        h = k32.OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE, False, pid)
        if not h:
            continue
        try:
            regions = get_private_regions(h)
            import concurrent.futures
            target = targets[pid]
            with concurrent.futures.ThreadPoolExecutor(max_workers=8) as ex:
                results_scans = list(ex.map(lambda r: scan_region(h, r[0], r[1], target), regions))
            s = sum(results_scans)
            results.append((pid, s, target))
        finally:
            k32.CloseHandle(h)
    return results


class App:
    def __init__(self, root):
        self.root = root
        root.title("CN-WoW Patcher   " + APP_VER + "   Author: " + APP_AUTHOR)
        root.resizable(False, False)
        root.configure(bg=BG)
        try:
            ico = os.path.join(os.path.dirname(os.path.abspath(__file__)), "CNWoW.ico")
            if os.path.isfile(ico):
                root.iconbitmap(ico)
        except Exception:
            pass

        self.game_running = False
        self.patched = 0
        self.last_pids = set()
        self.last_value = 11
        self.jackpot = 0
        self.targets = {}
        self.log_open = False
        self.log_data = []
        self.key_buf = []
        self.word_buf = []

        tk.Label(root, text="通过战网或点击下面按钮启动游戏", font=(FONT, 9),
                 bg=BG, fg=MUTED).pack(padx=22, pady=(16, 6))

        bnet_row = tk.Frame(root, bg=PANEL)
        bnet_row.pack(fill="x", padx=22)
        self.bnet_dot = tk.Canvas(bnet_row, width=12, height=12, bg=PANEL, highlightthickness=0)
        self.bnet_dot.pack(side="left", padx=(14, 8), pady=9)
        self.bnet_dot_id = self.bnet_dot.create_oval(2, 2, 10, 10, fill=GRAY, outline="")
        self.bnet_var = tk.StringVar(value="战网：检测中…")
        tk.Label(bnet_row, textvariable=self.bnet_var, font=(FONT, 10),
                 bg=PANEL, fg=TEXT).pack(side="left")

        self.start_btn = tk.Button(root, text="▶  进入游戏",
                                   font=(FONT, 12, "bold"),
                                   bg=ACCENT, fg="white", activebackground=ACCENT_H,
                                   activeforeground="white", relief="flat", bd=0,
                                   cursor="hand2", padx=10, pady=12)
        self.start_btn.pack(fill="x", padx=22, pady=14)
        self.start_btn.bind("<Enter>", lambda e: self.start_btn.config(bg=ACCENT_H))
        self.start_btn.bind("<Leave>", lambda e: self.start_btn.config(bg=ACCENT))
        self.start_btn.config(command=self.launch_game)

        self.view_hint = tk.Label(root, text="", font=(FONT, 9), bg=BG, fg=ORANGE)
        self.view_hint.pack(padx=24, pady=(4, 0), anchor="w")

        status_panel = tk.Frame(root, bg=PANEL)
        status_panel.pack(fill="x", padx=22)
        self.status_dot = tk.Canvas(status_panel, width=12, height=12, bg=PANEL, highlightthickness=0)
        self.status_dot.pack(side="left", padx=(14, 8), pady=12)
        self.status_dot_id = self.status_dot.create_oval(2, 2, 10, 10, fill=GRAY, outline="")
        self.status_var = tk.StringVar(value="等待游戏启动…")
        tk.Label(status_panel, textvariable=self.status_var, font=(FONT, 11, "bold"),
                 bg=PANEL, fg=TEXT).pack(side="left")

        opt_row = tk.Frame(root, bg=BG)
        opt_row.pack(fill="x", padx=24, pady=(2, 0))
        self.chaos_var = tk.BooleanVar(value=False)
        chaos_cb = tk.Checkbutton(opt_row, text="混乱模式", variable=self.chaos_var,
                                  bg=BG, fg=TEXT, activebackground=BG, activeforeground=ACCENT_H,
                                  selectcolor=PANEL, font=(FONT, 9), cursor="hand2")
        chaos_cb.pack(side="right")
        tk.Label(opt_row, text="暂仅支持单进程，等待游戏完全退出后再启动",
                 font=(FONT, 9), bg=BG, fg=GOLD).pack(side="right", padx=(0, 12))

        tk.Label(root, text="关闭窗口将隐藏到右下角托盘，右键托盘图标可彻底退出",
                 font=(FONT, 8), bg=BG, fg=MUTED).pack(padx=22, pady=(8, 14), anchor="w")

        self.log_text = None
        self.log_win = None
        for k in ("<Up>", "<Down>", "<Left>", "<Right>"):
            root.bind_all(k, lambda e, kk=k[1:-1]: self.on_key(kk))
        for ch in string.ascii_lowercase:
            root.bind_all("<Key-%s>" % ch, lambda e, c=ch: self.on_key(c))
            root.bind_all("<Key-%s>" % ch.upper(), lambda e, c=ch: self.on_key(c))

        self.tray_icon = self._make_tray_icon()
        threading.Thread(target=self.tray_icon.run, daemon=True).start()
        root.protocol("WM_DELETE_WINDOW", self.hide_to_tray)

        threading.Thread(target=self.patcher_loop, daemon=True).start()
        self.root.after(200, self.refresh_ui)

    def _make_tray_icon(self):
        img = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        d.rounded_rectangle([2, 2, 62, 62], radius=10, fill=(0, 116, 224, 255))
        d.text((14, 8), "12", font=ImageFont.truetype(r"C:\Windows\Fonts\segoeuib.ttf", 40),
               fill=(255, 255, 255, 255))
        return pystray.Icon(
            "wow_locale_patcher", img, "CN-WoW Patcher   " + APP_VER + "   Author: " + APP_AUTHOR,
            pystray.Menu(
                pystray.MenuItem("显示窗口", self.show_window, default=True),
                pystray.MenuItem("彻底退出", self.quit_app),
            ))

    def hide_to_tray(self):
        self.root.withdraw()
        self.tray_icon.visible = True
        self.tray_icon.notify("CN-WoW Patcher 仍在后台运行\n右键托盘图标可彻底退出")

    def show_window(self, icon=None, item=None):
        self.root.after(0, self._show)

    def _show(self):
        self.tray_icon.visible = False
        self.root.deiconify()
        self.root.lift()
        self.root.focus_force()

    def quit_app(self, icon=None, item=None):
        self.tray_icon.stop()
        self.root.after(0, self.root.destroy)

    def launch_game(self):
        path = detect_wow_path()
        if not path or not os.path.isfile(path):
            messagebox.showwarning("错误", "找不到游戏，请先启动一次游戏。\n（未检测到运行中的 Wow.exe，注册表也无安装记录）")
            return
        try:
            subprocess.Popen([path, "-launcherlogin", "-uid", "wow"], cwd=os.path.dirname(path))
            self.append_log("通过战网方式启动游戏: %s" % path)
        except Exception as e:
            messagebox.showerror("错误", "启动失败: " + str(e))

    def on_key(self, k):
        if self.log_open:
            return
        k = k.lower()
        if k in ("up", "down", "left", "right"):
            self.key_buf.append(k)
            if len(self.key_buf) > 10:
                self.key_buf.pop(0)
            if "".join(self.key_buf) != KEY_SEQ[:len(self.key_buf)]:
                self.key_buf = []
            if tuple(self.key_buf) == KEY_SEQ:
                self.key_buf, self.word_buf = [], []
                self.open_log_mode()
            self.word_buf = []
        else:
            if k in ("a", "b"):
                self.key_buf.append(k)
                if len(self.key_buf) > 10:
                    self.key_buf.pop(0)
                if "".join(self.key_buf) != KEY_SEQ[:len(self.key_buf)]:
                    self.key_buf = []
                if tuple(self.key_buf) == KEY_SEQ:
                    self.key_buf, self.word_buf = [], []
                    self.open_log_mode()
                    return
            else:
                self.key_buf = []
            self.word_buf.append(k)
            if len(self.word_buf) > 13:
                self.word_buf.pop(0)
            if "".join(self.word_buf) != WORD_WANT[:len(self.word_buf)]:
                self.word_buf = []
            if "".join(self.word_buf) == WORD_WANT:
                self.key_buf, self.word_buf = [], []
                self.open_log_mode()

    def open_log_mode(self):
        self.log_open = True
        self.log_win = tk.Toplevel(self.root)
        self.log_win.title("CN-WoW Patcher · Log")
        self.log_win.configure(bg=PANEL)
        self.log_win.protocol("WM_DELETE_WINDOW", self.close_log_win)
        self.log_text = tk.Text(self.log_win, width=60, height=20, bg=PANEL, fg=TEXT,
                                font=(FONT, 9), state="disabled", relief="flat", bd=0)
        self.log_text.pack(padx=8, pady=8)
        self.append_log("秘籍生效！Log 模式开启")

    def close_log_win(self):
        self.log_open = False
        self.log_win.destroy()
        self.log_win = None

    def append_log(self, msg):
        ts = time.strftime("%H:%M:%S")
        self.log_data.append("[%s] %s" % (ts, msg))
        if len(self.log_data) > 200:
            self.log_data.pop(0)
        if self.log_open and self.log_text:
            self.log_text.config(state="normal")
            self.log_text.delete("1.0", "end")
            self.log_text.insert("end", "\n".join(self.log_data))
            self.log_text.see("end")
            self.log_text.config(state="disabled")

    def refresh_ui(self):
        if battle_net_running():
            self.bnet_var.set("战网：运行中")
            self.bnet_dot.itemconfig(self.bnet_dot_id, fill=GREEN)
            self.view_hint.config(text="")
        else:
            self.bnet_var.set("战网：未运行")
            self.bnet_dot.itemconfig(self.bnet_dot_id, fill=GRAY)
            self.view_hint.config(text="⚠ 战网未运行，可启动游戏但仅能查看登录界面")

        if self.game_running:
            if self.jackpot == 1:
                self.status_var.set("你中奖了，扎昆守护着你！(版本 → 7.0)")
                self.status_dot.itemconfig(self.status_dot_id, fill=GOLD)
            elif self.jackpot == 2:
                self.status_var.set("龙飞走了，堡垒化了…(版本 → 3.0)")
                self.status_dot.itemconfig(self.status_dot_id, fill=GOLD)
            elif self.jackpot == 3:
                self.status_var.set("光源远征了…(版本 → 2.0)")
                self.status_dot.itemconfig(self.status_dot_id, fill=GOLD)
            else:
                self.status_var.set("已生效 · 载入界面版本 → %d.0" % (self.last_value + 1))
                self.status_dot.itemconfig(self.status_dot_id, fill=GREEN)
        else:
            self.status_var.set("等待游戏启动…")
            self.status_dot.itemconfig(self.status_dot_id, fill=GRAY)
        self.root.after(200, self.refresh_ui)

    def patcher_loop(self):
        while True:
            try:
                pids = set(find_processes(("wow.exe", "wow", "wowclassic.exe")))
                self.game_running = bool(pids)
                new_pids = pids - self.last_pids
                for pid in new_pids:
                    self.append_log("检测到游戏进程 (pid %d)" % pid)
                if pids:
                    if self.chaos_var.get():
                        for pid in pids:
                            if pid not in self.targets:
                                self.targets[pid] = random.randint(0, 11)
                        self.targets = {p: t for p, t in self.targets.items() if p in pids}
                    else:
                        self.targets = {p: 11 for p in pids}
                    res = patch_all(self.targets)
                    if res:
                        for pid, cnt, val in res:
                            if cnt:
                                self.append_log("pid %d: 已修改 %d 处 → 值 %d" % (pid, cnt, val))
                        self.patched = sum(r[1] for r in res)
                        self.last_value = res[-1][2]
                        jp = 0
                        for pid, cnt, val in res:
                            if val == 6:
                                jp = 1
                            elif val == 2:
                                jp = 2
                            elif val == 1:
                                jp = 3
                        self.jackpot = jp
                        if jp == 1:
                            self.append_log("你中奖了！扎昆守护着你！")
                        elif jp == 2:
                            self.append_log("龙飞走了，堡垒化了…")
                        elif jp == 3:
                            self.append_log("光源远征了…")
                    self.last_pids = pids
                    time.sleep(0.4)
                else:
                    if self.last_pids:
                        self.append_log("游戏进程已退出")
                    self.last_pids = set()
                    self.patched = 0
                    self.targets = {}
                    self.jackpot = 0
                    time.sleep(0.1)
            except Exception:
                time.sleep(1)


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
