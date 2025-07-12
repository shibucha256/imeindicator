#include <windows.h>
#include <imm.h>
#include <gdiplus.h>
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

HINSTANCE hInst;
HWND hOverlay = NULL;
ULONG_PTR gdiplusToken;
HWINEVENTHOOK hImeHook = NULL;
NOTIFYICONDATA nid = {};
bool isImeOpen = false;
ULONGLONG lastEventTime = 0;
HHOOK hKeyHook;

#define IMC_GETOPENSTATUS 0x0005
#define OBJID_IME ((LONG)0xFFFFFFFE)
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define WM_CHECK_IME_STATUS (WM_USER + 100)

const int INDICATOR_SIZE = 10;
const COLORREF IME_ON_COLOR = RGB(255, 0, 0);

BOOL CheckIMEStatus() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return FALSE;
    HWND imeWnd = ImmGetDefaultIMEWnd(hwnd);
    if (!imeWnd) return FALSE;
    return (SendMessage(imeWnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0) != 0) ? TRUE : FALSE;
}

POINT GetCaretScreenPos() {
    POINT pt = { 0 };
    GUITHREADINFO info = { sizeof(info) };
    HWND hwnd = GetForegroundWindow();
    DWORD tid = GetWindowThreadProcessId(hwnd, NULL);
    if (GetGUIThreadInfo(tid, &info) && info.hwndCaret) {
        pt.x = info.rcCaret.left;
        pt.y = info.rcCaret.top;
        ClientToScreen(info.hwndCaret, &pt);
    } else {
        GetCursorPos(&pt);
    }
    return pt;
}

void ShowOverlay() {
    POINT pt = GetCaretScreenPos();
    SetWindowPos(hOverlay, HWND_TOPMOST, pt.x, pt.y - INDICATOR_SIZE,
        INDICATOR_SIZE, INDICATOR_SIZE, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ShowWindow(hOverlay, SW_SHOW);
    InvalidateRect(hOverlay, NULL, TRUE);
}

void HideOverlay() {
    ShowWindow(hOverlay, SW_HIDE);
}

void CALLBACK _WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
    wchar_t buf[256];
    wsprintf(buf, L"[IME-DEBUG] event=0x%X hwnd=0x%p idObject=0x%X\n", event, hwnd, idObject);
    OutputDebugString(buf);
}

void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD event, HWND, LONG idObject, LONG, DWORD, DWORD) {
    if (idObject != OBJID_IME) return;
    lastEventTime = GetTickCount64();
    if (event == EVENT_OBJECT_SHOW) {
        isImeOpen = true;
        ShowOverlay();
    } else if (event == EVENT_OBJECT_HIDE) {
        isImeOpen = false;
        HideOverlay();
    }
}

DWORD WINAPI PollFallbackThread(LPVOID) {
    while (true) {
        ULONGLONG now = GetTickCount64();
        if (now - lastEventTime > 5000) {
            BOOL current = CheckIMEStatus();
            if (current != isImeOpen) {
                isImeOpen = current;
                if (current) ShowOverlay();
                else HideOverlay();
            }
            lastEventTime = now;
        }
        Sleep(1000);
    }
    return 0;
}

void DrawOverlay(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    SolidBrush brush(Color(255, 255, 0, 0));
    g.FillEllipse(&brush, 0, 0, INDICATOR_SIZE, INDICATOR_SIZE);
    ReleaseDC(hwnd, hdc);
}

void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"終了");
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void AddTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_INFORMATION);
    wcscpy_s(nid.szTip, L"IME Indicator");
    Shell_NotifyIcon(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) ShowTrayMenu(hwnd);
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            RemoveTrayIcon();
            UnhookWinEvent(hImeHook);
            UnhookWindowsHookEx(hKeyHook);
            PostQuitMessage(0);
        }
        break;
    case WM_CHECK_IME_STATUS:
        SetTimer(hwnd, 1, 100, NULL);
        break;
    case WM_TIMER:
        KillTimer(hwnd, 1);
        isImeOpen = CheckIMEStatus();
        if (isImeOpen) ShowOverlay();
        else HideOverlay();
        break;
    case WM_IME_STARTCOMPOSITION:
        isImeOpen = true;
        ShowOverlay();
        break;
    case WM_PAINT:
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        DrawOverlay(hwnd);
        EndPaint(hwnd, &ps);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;

        if (p->vkCode == VK_KANJI || p->vkCode == VK_OEM_AUTO 
        || p->vkCode == VK_CONVERT || p->vkCode == VK_NONCONVERT) { 
			// 半角/全角キー、変換キー、無変換キーが押された場合

            PostMessage(hOverlay, WM_CHECK_IME_STATUS, 0, 0);
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// オーバーレイウィンドウの作成
void CreateOverlayWindow(HINSTANCE hInst) {
    const wchar_t* CLASS_NAME = L"IMEOverlayHybrid";
    WNDCLASS wc = {};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = NULL;
    wc.style = CS_IME;
    RegisterClass(&wc);

    hOverlay = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME, NULL, WS_POPUP,
        0, 0, INDICATOR_SIZE, INDICATOR_SIZE,
        NULL, NULL, hInst, NULL
    );

    SetLayeredWindowAttributes(hOverlay, RGB(0, 0, 0), 255, LWA_COLORKEY);
    ShowWindow(hOverlay, SW_HIDE);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    hInst = hInstance;
    GdiplusStartupInput gdiStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiStartupInput, NULL);

    CreateOverlayWindow(hInst);
    AddTrayIcon(hOverlay);

    hImeHook = SetWinEventHook(
        EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE,
        NULL, WinEventProc, 0, 0,
        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS
    );

    hKeyHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, NULL, 0);
    CreateThread(NULL, 0, PollFallbackThread, NULL, 0, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}
