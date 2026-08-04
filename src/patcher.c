
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <winreg.h>
#include <shellapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "wbemuuid.lib")
#endif
#include <wbemidl.h>

#define COL_BG       RGB(0x16,0x17,0x1D)
#define COL_PANEL    RGB(0x1E,0x20,0x26)
#define COL_TEXT     RGB(0xF8,0xF8,0xF8)
#define COL_MUTED    RGB(0x8F,0x96,0xA3)
#define COL_ACCENT   RGB(0x00,0x74,0xE0)
#define COL_ACCENT_H RGB(0x44,0xA4,0xFC)
#define COL_ACCENT_HV RGB(0x1F,0x8A,0xF0)
#define COL_GREEN    RGB(0x35,0xC4,0x7A)
#define COL_ORANGE   RGB(0xE8,0x94,0x3A)
#define COL_GRAY     RGB(0x5C,0x64,0x70)
#define COL_GOLD     RGB(0xFF,0xD7,0x00)
#define APP_VER  L"v1.5"
#define APP_AUTH L"Adavak"

#define IDC_BNET_DOT   1001
#define IDC_BNET_LBL   1002
#define IDC_START_BTN  1003
#define IDC_STATUS_DOT 1004
#define IDC_STATUS_LBL 1005
#define IDC_CHAOS      1006
#define IDC_HINT_LBL   1007
#define IDC_TOP_LBL    1008

#define WM_TRAYICON  (WM_APP + 1)
#define WM_PATCH_DONE (WM_APP + 2)
#define WM_APP_LOG   (WM_APP + 3)

static HWND g_hwnd, g_hwndLog;
static HWND g_btnStart, g_lblStatus, g_dotStatus, g_lblBnet, g_dotBnet, g_chkChaos, g_lblHint;
static HFONT g_fontUI, g_fontTitle, g_fontSmall, g_fontMono;
static HBRUSH g_brBG, g_brPanel;
static NOTIFYICONDATAW g_nid;
static volatile BOOL g_gameRunning = FALSE;
static volatile int g_lastValue = 11;
static COLORREF g_hintColor = COL_ORANGE;
static volatile int g_jackpot = 0;
static volatile BOOL g_logOpen = FALSE;
static CRITICAL_SECTION g_logCS;
static wchar_t g_logBuf[64][160];
static int g_logCount = 0;
static int g_logHead = 0;
static int g_logStart = 0;
static BOOL g_thrRunning = TRUE;

static const unsigned char PREFIX[3] = { 0x64, 0x62, 0x96 };

#define MAX_PROC 16
static struct { DWORD pid; ULONGLONG startTime; int target; int wasChaos; } g_targets[MAX_PROC];
static int g_targetCount = 0;
static DWORD g_done[MAX_PROC];
static int g_doneN = 0;

static wchar_t g_keyBuf[16];
static int g_keyBufLen = 0;
static wchar_t g_wordBuf[32];
static int g_wordBufLen = 0;

static void LogMsg(const wchar_t *fmt, ...) {
    wchar_t line[160];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(line, 159, fmt, ap);
    va_end(ap);
    EnterCriticalSection(&g_logCS);
    wchar_t ts[16];
    SYSTEMTIME st; GetLocalTime(&st);
    swprintf(ts, 16, L"[%02d:%02d:%02d] ", st.wHour, st.wMinute, st.wSecond);
    swprintf(g_logBuf[g_logHead], 160, L"%ls%ls", ts, line);
    g_logHead = (g_logHead + 1) % 64;
    if (g_logCount < 64) g_logCount++;
    else g_logStart = (g_logStart + 1) % 64;
    LeaveCriticalSection(&g_logCS);
    if (g_logOpen && g_hwndLog) PostMessageW(g_hwndLog, WM_APP_LOG, 0, 0);
}

static BOOL IsWowName(const wchar_t *n) {
    return _wcsicmp(n, L"wow.exe") == 0 || _wcsicmp(n, L"wow") == 0;
}
static BOOL IsBnetName(const wchar_t *n) {
    return _wcsicmp(n, L"battle.net.exe") == 0 || _wcsicmp(n, L"battle.net") == 0;
}

static int FindProcesses(BOOL (*match)(const wchar_t*), DWORD *pids, int max) {
    int n = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (match(pe.szExeFile) && n < max) pids[n++] = pe.th32ProcessID;
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return n;
}

static ULONGLONG GetProcStartTime(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    FILETIME ct, et, kt, ut;
    ULONGLONG t = 0;
    if (GetProcessTimes(h, &ct, &et, &kt, &ut))
        t = ((ULONGLONG)ct.dwHighDateTime << 32) | ct.dwLowDateTime;
    CloseHandle(h);
    return t;
}

static BOOL IsPidDone(DWORD pid) {
    for (int d = 0; d < g_doneN; d++) if (g_done[d] == pid) return TRUE;
    return FALSE;
}

static BOOL BattleNetRunning(void) {
    DWORD pids[16];
    return FindProcesses(IsBnetName, pids, 16) > 0;
}

static int GetProcessPath(DWORD pid, wchar_t *buf, int cap) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return 0;
    DWORD sz = cap;
    BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &sz);
    CloseHandle(h);
    return ok ? (int)sz : 0;
}

static wchar_t g_wowPath[1024] = L"";

static void DetectWowPath(void) {
    DWORD pids[16];
    int n = FindProcesses(IsWowName, pids, 16);
    for (int i = 0; i < n; i++) {
        wchar_t buf[1024];
        if (GetProcessPath(pids[i], buf, 1024) && GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) {
            wcscpy(g_wowPath, buf);
            return;
        }
    }
    HKEY key;
    struct { const wchar_t *key; const wchar_t *val; } regs[] = {
        { L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\World of Warcraft", L"InstallLocation" },
        { L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\World of Warcraft", L"InstallLocation" },
        { L"SOFTWARE\\WOW6432Node\\Blizzard Entertainment\\World of Warcraft", L"InstallPath" },
        { L"SOFTWARE\\Blizzard Entertainment\\World of Warcraft", L"InstallPath" },
    };
    for (int r = 0; r < 4; r++) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regs[r].key, 0, KEY_READ, &key) == ERROR_SUCCESS) {
            wchar_t inst[1024] = L"";
            DWORD sz = sizeof(inst);
            if (RegQueryValueExW(key, regs[r].val, NULL, NULL, (BYTE*)inst, &sz) == ERROR_SUCCESS) {
                const wchar_t *subs[] = { L"_retail_\\Wow.exe", L"Wow.exe" };
                for (int s = 0; s < 2; s++) {
                    wchar_t full[1100];
                    swprintf(full, 1100, L"%ls\\%ls", inst, subs[s]);
                    if (GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES) {
                        wcscpy(g_wowPath, full);
                        RegCloseKey(key);
                        return;
                    }
                }
            }
            RegCloseKey(key);
        }
    }
    g_wowPath[0] = 0;
}

static int ScanRegion(HANDLE h, unsigned char *buf, SIZE_T size, SIZE_T baseAddr, int target, SIZE_T limit) {
    int patched = 0;
    for (SIZE_T i = 0; i + 5 <= size; i++) {
        if (i >= limit) break;
        if (buf[i] == PREFIX[0] && buf[i+1] == PREFIX[1] && buf[i+2] == PREFIX[2] && buf[i+4] == 0x01) {
            unsigned char want = (unsigned char)(0x40 + target * 2);
            if (buf[i+3] != want) {
                SIZE_T written = 0;
                if (WriteProcessMemory(h, (BYTE*)(baseAddr + i + 3), &want, 1, &written) && written == 1)
                    patched++;
            }
        }
    }
    return patched;
}

typedef struct { HANDLE h; unsigned char *base; SIZE_T size; int target; int result; } ScanJob;
static DWORD WINAPI ScanWorker(LPVOID p) {
    ScanJob *job = (ScanJob*)p;
    job->result = 0;
    SIZE_T maxChunk = min(4*1024*1024 + 8, job->size);
    unsigned char *buf = malloc(maxChunk);
    if (!buf) return 0;
    for (SIZE_T off = 0; off < job->size; off += 4*1024*1024) {
        SIZE_T chunk = min(4*1024*1024, job->size - off);
        SIZE_T want = min(chunk + 8, job->size - off);
        SIZE_T read = 0;
        if (ReadProcessMemory(job->h, job->base + off, buf, want, &read) && read > 0)
            job->result += ScanRegion(job->h, buf, read, (SIZE_T)(job->base + off), job->target, chunk);
    }
    free(buf);
    return 0;
}
static int PatchPid(DWORD pid, int target) {
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid);
    if (!h) return 0;
    int cap = 256;
    ScanJob *jobs = malloc(sizeof(ScanJob) * cap);
    if (!jobs) { CloseHandle(h); return 0; }
    int nj = 0;
    unsigned char *addr = NULL;
    MEMORY_BASIC_INFORMATION mbi;
    while (VirtualQueryEx(h, addr, &mbi, sizeof(mbi))) {
        SIZE_T base = (SIZE_T)mbi.BaseAddress;
        SIZE_T size = mbi.RegionSize;
        addr = (unsigned char*)(base + size);
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE) continue;
        if (!(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE | PAGE_WRITECOPY))) continue;
        if (nj == cap) {
            cap *= 2;
            ScanJob *njobs = realloc(jobs, sizeof(ScanJob) * cap);
            if (!njobs) break;
            jobs = njobs;
        }
        jobs[nj].h = h;
        jobs[nj].base = (unsigned char*)base;
        jobs[nj].size = size;
        jobs[nj].target = target;
        jobs[nj].result = 0;
        nj++;
    }
    int total = 0;
    const int NT = 4;
    for (int i = 0; i < nj; i += NT) {
        HANDLE th[NT];
        int nt = 0;
        for (int t = 0; t < NT && i + t < nj; t++)
            th[nt++] = CreateThread(NULL, 0, ScanWorker, &jobs[i + t], 0, NULL);
        WaitForMultipleObjects(nt, th, TRUE, INFINITE);
        for (int t = 0; t < nt; t++) { CloseHandle(th[t]); total += jobs[i + t].result; }
    }
    free(jobs);
    CloseHandle(h);
    return total;
}
static void PatchAll(void) {
    DWORD pids[16];
    int n = FindProcesses(IsWowName, pids, 16);
    if (n == 0) { g_gameRunning = FALSE; g_targetCount = 0; g_doneN = 0; return; }
    g_gameRunning = TRUE;
    BOOL chaos = (SendMessageW(g_chkChaos, BM_GETCHECK, 0, 0) == BST_CHECKED);
    int w = 0;
    for (int i = 0; i < g_targetCount; i++) {
        BOOL alive = FALSE;
        for (int j = 0; j < n; j++) if (g_targets[i].pid == pids[j]) { alive = TRUE; break; }
        if (alive) g_targets[w++] = g_targets[i];
    }
    g_targetCount = w;
    w = 0;
    for (int i = 0; i < g_doneN; i++) {
        BOOL alive = FALSE;
        for (int j = 0; j < n; j++) if (g_done[i] == pids[j]) { alive = TRUE; break; }
        if (alive) g_done[w++] = g_done[i];
    }
    g_doneN = w;
    for (int i = 0; i < n; i++) {
        ULONGLONG st = GetProcStartTime(pids[i]);
        int j;
        for (j = 0; j < g_targetCount; j++) if (g_targets[j].pid == pids[i]) break;
        if (j == g_targetCount) {
            if (g_targetCount < MAX_PROC) {
                g_targets[g_targetCount].pid = pids[i];
                g_targets[g_targetCount].startTime = st;
                g_targets[g_targetCount].target = chaos ? (rand() % 12) : 11;
                g_targets[g_targetCount].wasChaos = chaos;
                g_targetCount++;
            }
        } else if (g_targets[j].startTime != st) {
            g_targets[j].startTime = st;
            g_targets[j].target = chaos ? (rand() % 12) : 11;
            g_targets[j].wasChaos = chaos;
            for (int d = 0; d < g_doneN; d++) if (g_done[d] == pids[i]) { g_done[d] = g_done[--g_doneN]; break; }
        }
    }
    int lastVal = 11, jp = 0;
    for (int i = 0; i < g_targetCount; i++) {
        BOOL need = FALSE;
        if (g_targets[i].wasChaos != chaos) {
            g_targets[i].target = chaos ? (rand() % 12) : 11;
            g_targets[i].wasChaos = chaos;
            need = TRUE;
        }
        BOOL done = FALSE;
        for (int d = 0; d < g_doneN; d++) if (g_done[d] == g_targets[i].pid) { done = TRUE; break; }
        if (!done) need = TRUE;
        if (need) {
            int c = PatchPid(g_targets[i].pid, g_targets[i].target);
            if (c > 0) {
                LogMsg(L"pid %lu: 已修改 %d 处 → 值 %d", g_targets[i].pid, c, g_targets[i].target);
                if (g_doneN < MAX_PROC) g_done[g_doneN++] = g_targets[i].pid;
            }
        }
        if (g_targets[i].target == 6) jp = 1;
        else if (g_targets[i].target == 2) jp = 2;
        else if (g_targets[i].target == 1) jp = 3;
        lastVal = g_targets[i].target;
    }
    g_lastValue = lastVal;
    g_jackpot = jp;
    if (jp == 1) LogMsg(L"你中奖了！扎昆守护着你！");
    if (jp == 2) LogMsg(L"龙飞走了，堡垒化了…");
    if (jp == 3) LogMsg(L"光源远征了…");
    PostMessageW(g_hwnd, WM_PATCH_DONE, 0, 0);
}
static void TrayShow(void) { Shell_NotifyIconW(NIM_ADD, &g_nid); }
static void TrayHide(void) { Shell_NotifyIconW(NIM_DELETE, &g_nid); }

static LRESULT WINAPI OnCtlColor(HWND hwnd, HDC hdc, HWND child, int) {
    int id = GetDlgCtrlID(child);
    SetBkMode(hdc, TRANSPARENT);
    if (id == IDC_STATUS_LBL || id == IDC_BNET_LBL || id == IDC_HINT_LBL || id == IDC_TOP_LBL) {
        if (id == IDC_HINT_LBL) SetTextColor(hdc, g_hintColor);
        else if (id == IDC_TOP_LBL) SetTextColor(hdc, COL_MUTED);
        else SetTextColor(hdc, COL_TEXT);
        return (LRESULT)g_brBG;
    }
    if (id == IDC_CHAOS) {
        SetTextColor(hdc, COL_TEXT);
        return (LRESULT)g_brBG;
    }
    return DefWindowProcW(hwnd, WM_CTLCOLORBTN, (WPARAM)hdc, (LPARAM)child);
}
static COLORREF g_bnetColor = COL_GRAY, g_statusColor = COL_GRAY;
static BOOL g_btnHover = FALSE;
static void SetDot(HWND dot, COLORREF *store, COLORREF c) {
    if (*store == c) return;
    *store = c;
    InvalidateRect(dot, NULL, TRUE);
}
static void SetTextIfChanged(HWND h, const wchar_t *s) {
    wchar_t cur[256];
    GetWindowTextW(h, cur, 256);
    if (wcscmp(cur, s) != 0) SetWindowTextW(h, s);
}
static DWORD WINAPI WmiThread(LPVOID) {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr)) {
        hr = CoInitializeSecurity(NULL, -1, NULL, NULL, RPC_C_AUTHN_LEVEL_DEFAULT,
                                  RPC_C_IMP_LEVEL_IMPERSONATE, NULL, EOAC_NONE, NULL);
    }
    IWbemLocator *loc = NULL;
    IWbemServices *svc = NULL;
    IEnumWbemClassObject *en = NULL;
    BSTR ns = SysAllocString(L"ROOT\\CIMV2");
    BSTR q = SysAllocString(L"SELECT * FROM Win32_ProcessStartTrace WHERE ProcessName='Wow.exe' OR ProcessName='wow.exe'");
    BSTR wql = SysAllocString(L"WQL");
    BOOL wmiOK = FALSE;
    if (SUCCEEDED(CoCreateInstance(&CLSID_WbemLocator, 0, CLSCTX_INPROC_SERVER, &IID_IWbemLocator, (LPVOID*)&loc)) &&
        SUCCEEDED(loc->lpVtbl->ConnectServer(loc, ns, NULL, NULL, 0, 0, NULL, NULL, &svc)) &&
        SUCCEEDED(svc->lpVtbl->ExecNotificationQuery(svc, wql, q, WBEM_FLAG_FORWARD_ONLY, NULL, &en))) {
        wmiOK = TRUE;
    }
    if (!wmiOK) {
        while (g_thrRunning) {
            DWORD pids[16];
            int n = FindProcesses(IsWowName, pids, 16);
            if (n > 0) {
                PatchAll();
                Sleep(100);
            } else {
                if (g_gameRunning) { g_gameRunning = FALSE; g_targetCount = 0; g_doneN = 0; }
                Sleep(500);
            }
        }
        if (en) en->lpVtbl->Release(en);
        if (svc) svc->lpVtbl->Release(svc);
        if (loc) loc->lpVtbl->Release(loc);
        SysFreeString(ns); SysFreeString(q); SysFreeString(wql);
        if (SUCCEEDED(hr)) CoUninitialize();
        return 0;
    }
    while (g_thrRunning) {
        IWbemClassObject *obj = NULL;
        ULONG got = 0;
        HRESULT hr2 = en->lpVtbl->Next(en, 500, 1, &obj, &got);
        if (hr2 == WBEM_S_NO_ERROR && got) {
            VARIANT v;
            VariantInit(&v);
            DWORD started = 0;
            if (SUCCEEDED(obj->lpVtbl->Get(obj, L"ProcessID", 0, &v, 0, 0)) && v.vt == VT_I4) {
                started = (DWORD)v.lVal;
                LogMsg(L"检测到游戏进程 (pid %lu)", (unsigned long)started);
            }
            VariantClear(&v);
            obj->lpVtbl->Release(obj);
            for (int a = 0; a < 8 && g_thrRunning; a++) {
                PatchAll();
                if (started == 0 || IsPidDone(started)) break;
                Sleep(250);
            }
        } else if (hr2 == WBEM_S_TIMEDOUT) {
            PatchAll();
        }
    }
    en->lpVtbl->Release(en);
    svc->lpVtbl->Release(svc);
    loc->lpVtbl->Release(loc);
    SysFreeString(ns); SysFreeString(q); SysFreeString(wql);
    if (SUCCEEDED(hr)) CoUninitialize();
    return 0;
}
static void UpdateUI(void) {
    if (!g_hwnd) return;
    BOOL bnet = BattleNetRunning();
    SetTextIfChanged(g_lblBnet, bnet ? L"战网：运行中" : L"战网：未运行");
    SetDot(g_dotBnet, &g_bnetColor, bnet ? COL_GREEN : COL_GRAY);
    if (bnet) {
        SetTextIfChanged(g_lblHint, L"多开/重开后若未生效，请反馈");
        g_hintColor = COL_GOLD;
    } else {
        SetTextIfChanged(g_lblHint, L"⚠ 战网未运行，仅可查看登录界面");
        g_hintColor = COL_ORANGE;
    }
    if (g_gameRunning) {
        if (g_jackpot == 1) {
            SetTextIfChanged(g_lblStatus, L"你中奖了，扎昆守护着你！(版本 → 7.0)");
            SetDot(g_dotStatus, &g_statusColor, COL_GOLD);
        } else if (g_jackpot == 2) {
            SetTextIfChanged(g_lblStatus, L"龙飞走了，堡垒化了…(版本 → 3.0)");
            SetDot(g_dotStatus, &g_statusColor, COL_GOLD);
        } else if (g_jackpot == 3) {
            SetTextIfChanged(g_lblStatus, L"光源远征了…(版本 → 2.0)");
            SetDot(g_dotStatus, &g_statusColor, COL_GOLD);
        } else {
            wchar_t buf[64];
            swprintf(buf, 64, L"已生效 · 载入界面版本 → %d.0", g_lastValue + 1);
            SetTextIfChanged(g_lblStatus, buf);
            SetDot(g_dotStatus, &g_statusColor, COL_GREEN);
        }
    } else {
        SetTextIfChanged(g_lblStatus, L"等待游戏启动…");
        SetDot(g_dotStatus, &g_statusColor, COL_GRAY);
    }
}

static void OpenLogWindow(void);
static void HandleKey(const wchar_t *k) {
    if (g_logOpen) return;
    static const wchar_t *KEY_WANT = L"uuddlrlrba";
    static const wchar_t *WORD_WANT = L"whosyourdaddy";
    BOOL isDir = (_wcsicmp(k, L"up") == 0 || _wcsicmp(k, L"down") == 0 ||
                  _wcsicmp(k, L"left") == 0 || _wcsicmp(k, L"right") == 0);
    if (isDir) {
        g_keyBuf[g_keyBufLen++] = k[0];
        if (g_keyBufLen > 10) { memmove(g_keyBuf, g_keyBuf + 1, 10 * sizeof(wchar_t)); g_keyBufLen = 10; }

        if (wcsncmp(g_keyBuf, KEY_WANT, g_keyBufLen) != 0) g_keyBufLen = 0;
        if (g_keyBufLen == 10 && wcsncmp(g_keyBuf, KEY_WANT, 10) == 0) {
            g_keyBufLen = 0; g_wordBufLen = 0;
            OpenLogWindow();
        }
        g_wordBufLen = 0;
    } else {
        if (k[0] == L'b' || k[0] == L'a') {
            g_keyBuf[g_keyBufLen++] = k[0];
            if (g_keyBufLen > 10) { memmove(g_keyBuf, g_keyBuf + 1, 10 * sizeof(wchar_t)); g_keyBufLen = 10; }
            if (wcsncmp(g_keyBuf, KEY_WANT, g_keyBufLen) != 0) g_keyBufLen = 0;
            if (g_keyBufLen == 10 && wcsncmp(g_keyBuf, KEY_WANT, 10) == 0) {
                g_keyBufLen = 0; g_wordBufLen = 0;
                OpenLogWindow();
                return;
            }
        } else {
            g_keyBufLen = 0;
        }
        g_wordBuf[g_wordBufLen++] = k[0];
        if (g_wordBufLen > 13) { memmove(g_wordBuf, g_wordBuf + 1, 13 * sizeof(wchar_t)); g_wordBufLen = 13; }

        if (_wcsnicmp(g_wordBuf, WORD_WANT, g_wordBufLen) != 0) g_wordBufLen = 0;
        if (g_wordBufLen == 13 && _wcsicmp(g_wordBuf, WORD_WANT) == 0) {
            g_keyBufLen = 0; g_wordBufLen = 0;
            OpenLogWindow();
        }
    }
}

static HWND g_logEdit;
static wchar_t *BuildLogText(void) {
    EnterCriticalSection(&g_logCS);
    wchar_t *all = malloc(((size_t)g_logCount * 170 + 8) * sizeof(wchar_t));
    if (all) {
        all[0] = 0;
        for (int i = 0; i < g_logCount; i++) {
            wcscat(all, g_logBuf[(g_logStart + i) % 64]);
            wcscat(all, L"\r\n");
        }
    }
    LeaveCriticalSection(&g_logCS);
    return all;
}
static void OpenLogWindow(void) {
    if (g_logOpen) return;
    g_logOpen = TRUE;
    g_hwndLog = CreateWindowExW(0, L"CNWoWPatcherLog", L"CN-WoW Patcher · Log",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 675, 525, NULL, NULL, GetModuleHandleW(NULL), NULL);
    {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            HRESULT (WINAPI *dwa)(HWND, DWORD, const void*, DWORD) =
                (HRESULT(WINAPI*)(HWND,DWORD,const void*,DWORD))(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (dwa) {
                int pref = 2;
                dwa(g_hwndLog, 33, &pref, sizeof(pref));
            }
            FreeLibrary(dwm);
        }
    }
    RECT cr; GetClientRect(g_hwndLog, &cr);
    g_logEdit = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
        0, 0, cr.right, cr.bottom, g_hwndLog, NULL, GetModuleHandleW(NULL), NULL);
    SendMessageW(g_logEdit, WM_SETFONT, (WPARAM)g_fontMono, TRUE);

    RECT r; GetWindowRect(g_hwnd, &r);
    SetWindowPos(g_hwndLog, NULL, r.right + 8, r.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    ShowWindow(g_hwndLog, SW_SHOW);

    wchar_t *all = BuildLogText();
    if (all) { SetWindowTextW(g_logEdit, all); free(all); }
    LogMsg(L"秘籍生效！Log 模式开启");
}

static void RefreshLog(void) {
    wchar_t *all = BuildLogText();
    if (all) { SetWindowTextW(g_logEdit, all); free(all); }
    SendMessageW(g_logEdit, EM_SETSEL, -1, -1);
}

static LRESULT CALLBACK FwdProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    WNDPROC old = (WNDPROC)GetPropW(h, L"OLDPROC");
    if (h == g_btnStart) {
        if (m == WM_MOUSEMOVE) {
            if (!g_btnHover) {
                g_btnHover = TRUE;
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
                InvalidateRect(h, NULL, TRUE);
            }
        } else if (m == WM_MOUSELEAVE) {
            if (g_btnHover) {
                g_btnHover = FALSE;
                InvalidateRect(h, NULL, TRUE);
            }
        }
    }
    if ((m == WM_CHAR || m == WM_KEYDOWN) && g_hwnd && !g_logOpen)
        SendMessageW(g_hwnd, m, w, l);
    return CallWindowProcW(old, h, m, w, l);
}
static void Subclass(HWND ctl) {
    WNDPROC old = (WNDPROC)SetWindowLongPtrW(ctl, GWLP_WNDPROC, (LONG_PTR)FwdProc);
    SetPropW(ctl, L"OLDPROC", (HANDLE)old);
}

static void LaunchGame(void) {
    DetectWowPath();
    if (g_wowPath[0] == 0) {
        MessageBoxW(g_hwnd, L"找不到游戏，请先启动一次游戏。\n（未检测到运行中的 Wow.exe，注册表也无安装记录）", L"错误", MB_ICONWARNING);
        return;
    }
    wchar_t cmd[1100];
    if (BattleNetRunning())
        swprintf(cmd, 1100, L"\"%ls\" -launcherlogin -uid wow", g_wowPath);
    else
        swprintf(cmd, 1100, L"\"%ls\"", g_wowPath);
    wchar_t dir[1024]; wcscpy(dir, g_wowPath);
    wchar_t *slash = wcsrchr(dir, L'\\'); if (slash) *slash = 0;
    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    if (CreateProcessW(NULL, cmd, NULL, NULL, FALSE, 0, NULL, dir, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        LogMsg(L"通过战网方式启动游戏: %ls", g_wowPath);
    } else {
        MessageBoxW(g_hwnd, L"启动失败", L"错误", MB_ICONERROR);
    }
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = hwnd;
        g_brBG = CreateSolidBrush(COL_BG);
        g_brPanel = CreateSolidBrush(COL_PANEL);
        g_fontUI = CreateFontW(-21, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_fontTitle = CreateFontW(-24, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_fontSmall = CreateFontW(-18, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        g_fontMono = CreateFontW(-20, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");

        RECT cr;
        GetClientRect(hwnd, &cr);
        int W = cr.right;


        CreateWindowExW(0, L"STATIC", L"通过战网或点击下面按钮启动游戏",
            WS_CHILD | WS_VISIBLE | SS_CENTER, 0, 21, W, 30, hwnd, (HMENU)IDC_TOP_LBL, NULL, NULL);
        HWND topLbl = GetDlgItem(hwnd, IDC_TOP_LBL);
        SendMessageW(topLbl, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);


        g_dotBnet = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            30, 75, 18, 18, hwnd, (HMENU)IDC_BNET_DOT, NULL, NULL);
        g_lblBnet = CreateWindowExW(0, L"STATIC", L"战网：检测中…", WS_CHILD | WS_VISIBLE,
            60, 69, 300, 30, hwnd, (HMENU)IDC_BNET_LBL, NULL, NULL);
        SendMessageW(g_lblBnet, WM_SETFONT, (WPARAM)g_fontUI, TRUE);


        int bx = (W - 600) / 2;
        if (bx < 10) bx = 10;
        g_btnStart = CreateWindowExW(0, L"BUTTON", L"▶  进入游戏",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, bx, 114, 600, 69, hwnd, (HMENU)IDC_START_BTN, NULL, NULL);
        SendMessageW(g_btnStart, WM_SETFONT, (WPARAM)g_fontTitle, TRUE);
        SetClassLongPtrW(g_btnStart, GCLP_HCURSOR, (LONG_PTR)LoadCursorW(NULL, IDC_HAND));


        g_dotStatus = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
            30, 207, 18, 18, hwnd, (HMENU)IDC_STATUS_DOT, NULL, NULL);
        g_lblStatus = CreateWindowExW(0, L"STATIC", L"等待游戏启动…", WS_CHILD | WS_VISIBLE,
            60, 201, 600, 30, hwnd, (HMENU)IDC_STATUS_LBL, NULL, NULL);
        SendMessageW(g_lblStatus, WM_SETFONT, (WPARAM)g_fontUI, TRUE);


        {
            HDC hdc = GetDC(g_btnStart);
            HFONT old = (HFONT)SelectObject(hdc, g_fontUI);
            SIZE sz;
            wchar_t chkText[] = L"混乱模式";
            GetTextExtentPoint32W(hdc, chkText, 4, &sz);
            SelectObject(hdc, old);
            ReleaseDC(g_btnStart, hdc);
            int cw = sz.cx + 30;
            g_chkChaos = CreateWindowExW(0, L"BUTTON", L"混乱模式",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, bx + 600 - cw, 292, cw, 33, hwnd, (HMENU)IDC_CHAOS, NULL, NULL);
        }

        SendMessageW(g_chkChaos, WM_SETFONT, (WPARAM)g_fontUI, TRUE);
        Subclass(g_btnStart);
        Subclass(g_chkChaos);


        g_lblHint = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
            30, 296, W - 184 - 40, 26, hwnd, (HMENU)IDC_HINT_LBL, NULL, NULL);
        SendMessageW(g_lblHint, WM_SETFONT, (WPARAM)g_fontUI, TRUE);


        memset(&g_nid, 0, sizeof(g_nid));
        g_nid.cbSize = sizeof(g_nid);
        g_nid.hWnd = hwnd;
        g_nid.uID = 1;
        g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        g_nid.uCallbackMessage = WM_TRAYICON;
        g_nid.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
        wcscpy(g_nid.szTip, L"CN-WoW Patcher   " APP_VER L"   Author: " APP_AUTH);

        {
            HICON hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(1));
            SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
        }
        InitializeCriticalSection(&g_logCS);
        CreateThread(NULL, 0, WmiThread, NULL, 0, NULL);
        SetTimer(hwnd, 1, 500, NULL);
        return 0;
    }
    case WM_ERASEBKGND:
        if (g_brBG) {
            RECT r; GetClientRect(hwnd, &r);
            FillRect((HDC)wp, &r, g_brBG);
            return 1;
        }
        break;
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *di = (DRAWITEMSTRUCT*)lp;
        if (di->CtlType == ODT_BUTTON && di->CtlID == IDC_START_BTN) {
            HDC hdc = di->hDC;
            RECT r = di->rcItem;
            BOOL down = (di->itemState & ODS_SELECTED) != 0;
            HBRUSH br = CreateSolidBrush(down ? COL_ACCENT_H : (g_btnHover ? COL_ACCENT_HV : COL_ACCENT));
            FillRect(hdc, &r, br);
            DeleteObject(br);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255,255,255));
            DrawTextW(hdc, L"▶  进入游戏", -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        if (di->CtlType == ODT_STATIC && (di->CtlID == IDC_BNET_DOT || di->CtlID == IDC_STATUS_DOT)) {
            HDC hdc = di->hDC;
            RECT r = di->rcItem;
            COLORREF c = (di->CtlID == IDC_BNET_DOT) ? g_bnetColor : g_statusColor;
            HBRUSH bg = CreateSolidBrush(COL_BG);
            FillRect(hdc, &r, bg);
            DeleteObject(bg);
            HBRUSH br = CreateSolidBrush(c);
            HPEN pen = CreatePen(PS_NULL, 0, 0);
            HGDIOBJ ob = SelectObject(hdc, br);
            HGDIOBJ op = SelectObject(hdc, pen);
            Ellipse(hdc, r.left, r.top, r.right, r.bottom);
            SelectObject(hdc, ob);
            SelectObject(hdc, op);
            DeleteObject(br);
            DeleteObject(pen);
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        return (LRESULT)OnCtlColor(hwnd, (HDC)wp, (HWND)lp, msg);
    case WM_TIMER:
        UpdateUI();
        return 0;
    case WM_PATCH_DONE:
        UpdateUI();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_START_BTN) {
            LaunchGame();
        }
        return 0;
    case WM_KEYDOWN: {

        const wchar_t *k = NULL;
        switch (wp) {
            case VK_UP: k = L"up"; break;
            case VK_DOWN: k = L"down"; break;
            case VK_LEFT: k = L"left"; break;
            case VK_RIGHT: k = L"right"; break;
        }
        if (k) HandleKey(k);
        return 0;
    }
    case WM_CHAR: {
        wchar_t c = (wchar_t)wp;
        if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) {
            wchar_t buf[2] = { towlower(c), 0 };
            HandleKey(buf);
        }
        return 0;
    }
    case WM_ACTIVATE:

        if (LOWORD(wp) != WA_INACTIVE) SetFocus(hwnd);
        return 0;
    case WM_LBUTTONDOWN:

        SetFocus(hwnd);
        break;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 700; mmi->ptMinTrackSize.y = 380;
        return 0;
    }
    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"显示窗口");
            AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(menu, MF_STRING, 2, L"彻底退出");
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            int r = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(menu);
            if (r == 1) { ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); TrayHide(); }
            else if (r == 2) { DestroyWindow(hwnd); }
        } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); TrayHide();
        }
        return 0;
    case WM_CLOSE:

        ShowWindow(hwnd, SW_HIDE);
        TrayShow();
        return 0;
    case WM_DESTROY:
        g_thrRunning = FALSE;
        TrayHide();
        DeleteCriticalSection(&g_logCS);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK LogWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        if (g_brPanel) {
            RECT r; GetClientRect(hwnd, &r);
            FillRect((HDC)wp, &r, g_brPanel);
            return 1;
        }
        break;
    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetBkColor(hdc, COL_PANEL);
        SetTextColor(hdc, COL_TEXT);
        return (LRESULT)g_brPanel;
    }
    case WM_APP_LOG:
        RefreshLog();
        return 0;
    case WM_SIZE:
        if (g_logEdit) {
            RECT cr; GetClientRect(hwnd, &cr);
            MoveWindow(g_logEdit, 0, 0, cr.right, cr.bottom, TRUE);
        }
        return 0;
    case WM_CLOSE:
        g_logOpen = FALSE;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_hwndLog = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {

    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    if (u32) {
        BOOL (WINAPI *sda)(void*) = (BOOL(WINAPI*)(void*))(void*)GetProcAddress(u32, "SetProcessDpiAwarenessContext");
        if (sda) sda((void*)-4);
    }
    srand((unsigned)time(NULL) ^ GetCurrentProcessId());

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"CNWoWPatcherMain";
    RegisterClassW(&wc);

    WNDCLASSW wc2 = {0};
    wc2.lpfnWndProc = LogWndProc;
    wc2.hInstance = hInst;
    wc2.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc2.lpszClassName = L"CNWoWPatcherLog";
    RegisterClassW(&wc2);

    HWND hwnd = CreateWindowExW(0, L"CNWoWPatcherMain", L"CN-WoW Patcher   " APP_VER L"   Author: " APP_AUTH,
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 400, NULL, NULL, hInst, NULL);
    if (!hwnd) return 0;
    {
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) {
            HRESULT (WINAPI *dwa)(HWND, DWORD, const void*, DWORD) =
                (HRESULT(WINAPI*)(HWND,DWORD,const void*,DWORD))(void*)GetProcAddress(dwm, "DwmSetWindowAttribute");
            if (dwa) {
                int pref = 2;
                dwa(hwnd, 33, &pref, sizeof(pref));
            }
            FreeLibrary(dwm);
        }
    }
    ShowWindow(hwnd, SW_SHOW);

    {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        RECT wr;
        GetWindowRect(hwnd, &wr);
        int w = wr.right - wr.left, h = wr.bottom - wr.top;
        SetWindowPos(hwnd, NULL, (sw - w) / 2, (sh - h) / 2, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return (int)msg.wParam;
}
