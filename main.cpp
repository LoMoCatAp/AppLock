#include <windows.h>
#include <tlhelp32.h>
#include <userconsentverifierinterop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Security.Credentials.UI.h>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include "json.hpp"
using namespace winrt;
using namespace Windows::Foundation;
using namespace Windows::Security::Credentials::UI;
using json = nlohmann::json;
struct Hidden { DWORD pid; bool minimized; };
std::map<HWND, Hidden> hidden;
std::set<DWORD> targets;
bool retry = false;
HWND owner = nullptr;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_CLOSE) { ShowWindow(hwnd, SW_HIDE); return 0; }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
bool sameWindow(HWND hwnd, DWORD pid) {
    DWORD actual = 0; GetWindowThreadProcessId(hwnd, &actual);
    return IsWindow(hwnd) && actual == pid;
}
void restore(std::map<HWND, Hidden>::iterator it) {
    if (sameWindow(it->first, it->second.pid))
        ShowWindowAsync(it->first, it->second.minimized ? SW_SHOWMINNOACTIVE : SW_SHOWNOACTIVATE);
}
struct Scan { bool unlocked; bool visible = false; };
BOOL CALLBACK ScanWindow(HWND hwnd, LPARAM lp) {
    auto& scan = *reinterpret_cast<Scan*>(lp);
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
    if (!targets.count(pid) || !IsWindowVisible(hwnd)) return TRUE;
    scan.visible = true;
    if (!scan.unlocked) {
        hidden.emplace(hwnd, Hidden{pid, IsIconic(hwnd) != FALSE});
        ShowWindowAsync(hwnd, SW_HIDE);
    }
    return TRUE;
}
int main(int argc, char** argv) {
    init_apartment(apartment_type::single_threaded);
    if (argc > 1 && std::string(argv[1]) == "--hello-status") {
        // Blocking WinRT calls must run in an MTA.
        uninit_apartment(); init_apartment(apartment_type::multi_threaded);
        try { return UserConsentVerifier::CheckAvailabilityAsync().get() == UserConsentVerifierAvailability::Available ? 0 : 2; }
        catch (...) { return 3; }
    }
    const wchar_t* eventName = L"Local\\AppLockDemo.Stop.v3";
    if (argc > 1 && std::string(argv[1]) == "--stop") {
        HANDLE event = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
        if (event) { SetEvent(event); CloseHandle(event); } return 0;
    }
    HANDLE instance = CreateMutexW(nullptr, TRUE, L"Local\\AppLockDemo.Core.v3");
    if (!instance || GetLastError() == ERROR_ALREADY_EXISTS) return 4;
    HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, eventName);
    if (!stop) return 5;
    ResetEvent(stop);
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    auto configPath = argc > 1 ? std::filesystem::path(to_hstring(argv[1]).c_str()) : std::filesystem::path(exe).parent_path() / L"config.json";
    WNDCLASSW wc{}; wc.lpfnWndProc = WindowProc; wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"AppLockVerifyOwner"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    // A desktop Hello dialog needs a visible, active top-level owner. A fully
    // transparent 1x1 tool window provides that owner without showing UI.
    owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, wc.lpszClassName,
        L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    if (!owner) return 6;
    SetLayeredWindowAttributes(owner, 0, 0, LWA_ALPHA);
    IAsyncOperation<UserConsentVerificationResult> operation{nullptr};
    bool unlocked = false, failed = false;
    ULONGLONG lastVisible = GetTickCount64(), nextConfig = 0;
    std::set<std::wstring> names;
    while (WaitForSingleObject(stop, 0) != WAIT_OBJECT_0) {
        MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        if (GetTickCount64() >= nextConfig) {
            nextConfig = GetTickCount64() + 1000;
            try {
                std::ifstream file(configPath); json config; file >> config;
                if (!config.at("apps").is_array()) throw std::runtime_error("Invalid apps");
                std::set<std::wstring> updated;
                for (auto& app : config.at("apps")) {
                    if (!app.value("locked", app.value("enabled", true))) continue;
                    auto name = app.value("process", "");
                    if (name.empty()) name = std::filesystem::path(to_hstring(app.value("path", "")).c_str()).filename().u8string();
                    if (!name.empty()) updated.insert(to_hstring(name).c_str());
                }
                if (updated != names) { names = std::move(updated); unlocked = false; }
            } catch (...) { /* Keep last valid configuration during failed writes. */ }
        }
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            targets.clear(); PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
            if (Process32FirstW(snapshot, &pe)) do {
                for (auto& name : names) if (_wcsicmp(name.c_str(), pe.szExeFile) == 0 && pe.th32ProcessID != GetCurrentProcessId()) targets.insert(pe.th32ProcessID);
            } while (Process32NextW(snapshot, &pe));
            CloseHandle(snapshot);
        }
        for (auto it = hidden.begin(); it != hidden.end();) {
            if (!sameWindow(it->first, it->second.pid) || !targets.count(it->second.pid)) {
                if (!targets.count(it->second.pid)) restore(it);
                it = hidden.erase(it);
            } else ++it;
        }
        // Consume completion BEFORE scanning or scheduling another verification.
        if (operation && operation.Status() != winrt::Windows::Foundation::AsyncStatus::Started) {
            bool success = false;
            try { success = operation.GetResults() == UserConsentVerificationResult::Verified; } catch (...) {}
            operation = nullptr;
            if (success) {
                unlocked = true; failed = false; lastVisible = GetTickCount64();
                for (auto it = hidden.begin(); it != hidden.end(); ++it) restore(it);
                hidden.clear(); ShowWindow(owner, SW_HIDE);
            } else {
                failed = true;
                ShowWindow(owner, SW_HIDE);
            }
        }
        Scan scan{unlocked}; EnumWindows(ScanWindow, reinterpret_cast<LPARAM>(&scan));
        if (unlocked) {
            if (scan.visible) lastVisible = GetTickCount64();
            else if (GetTickCount64() - lastVisible > 1500) unlocked = false;
        }
        if (hidden.empty() && !operation) { failed = false; ShowWindow(owner, SW_HIDE); }
        bool requested = retry; retry = false;
        if (!unlocked && !hidden.empty() && !operation && (!failed || requested)) {
            // The topmost transparent owner gives the system dialog a stable
            // z-order while the async operation keeps this thread pumping UI.
            SetWindowPos(owner, HWND_TOPMOST, 0, 0, 0, 0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
            ShowWindow(owner, SW_SHOWNOACTIVATE);
            AllowSetForegroundWindow(ASFW_ANY);
            HWND foreground = GetForegroundWindow();
            DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
            DWORD currentThread = GetCurrentThreadId();
            if (foregroundThread && foregroundThread != currentThread) {
                AttachThreadInput(foregroundThread, currentThread, TRUE);
            }
            SetForegroundWindow(owner);
            BringWindowToTop(owner);
            if (foregroundThread && foregroundThread != currentThread) {
                AttachThreadInput(foregroundThread, currentThread, FALSE);
            }
            try {
                auto factory = get_activation_factory<UserConsentVerifier, IUserConsentVerifierInterop>();
                hstring message = L"请验证身份以解锁应用";
                check_hresult(factory->RequestVerificationForWindowAsync(owner,
                    reinterpret_cast<HSTRING>(get_abi(message)),
                    guid_of<IAsyncOperation<UserConsentVerificationResult>>(),
                    put_abi(operation)));
            } catch (...) {
                operation = nullptr;
                failed = true;
                ShowWindow(owner, SW_HIDE);
            }
        }
        MsgWaitForMultipleObjects(1, &stop, FALSE, 100, QS_ALLINPUT);
    }
    if (operation) { try { operation.Cancel(); } catch (...) {} }
    for (auto it = hidden.begin(); it != hidden.end(); ++it) restore(it);
    DestroyWindow(owner); CloseHandle(stop); ReleaseMutex(instance); CloseHandle(instance);
    return 0;
}






