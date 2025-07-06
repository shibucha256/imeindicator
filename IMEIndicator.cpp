#include <windows.h>
#include <imm.h>
#include <gdiplus.h>
#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

HINSTANCE hInst;
HWND hOverlay = NULL;
ULONG_PTR gdiplusToken;

// タスクトレイ
#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
NOTIFYICONDATA nid = {};

const int INDICATOR_SIZE = 10;
const COLORREF IME_ON_COLOR = RGB(255, 0, 0);
#define IMC_GETOPENSTATUS 0x0005

// IME状態取得
BOOL IsIMEOpen() {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return FALSE;
    HWND imeWnd = ImmGetDefaultIMEWnd(hwnd);
    if (!imeWnd) return FALSE;
    return (SendMessage(imeWnd, WM_IME_CONTROL, IMC_GETOPENSTATUS, 0) == TRUE);
}

// キャレット座標取得（失敗時はマウス）
POINT GetCaretScreenPos() {
    POINT pt = { 0 };
    GUITHREADINFO info = { sizeof(info) };
    HWND hwnd = GetForegroundWindow();
    DWORD tid = GetWindowThreadProcessId(hwnd, NULL);

    if (GetGUIThreadInfo(tid, &info) && info.hwndCaret) {
        pt.x = info.rcCaret.left;
        pt.y = info.rcCaret.top;
        ClientToScreen(info.hwndCaret, &pt);
    }
    else {
        GetCursorPos(&pt);
    }
    return pt;
}

// オーバーレイの位置
void UpdateOverlayPosition(POINT pt) {
    SetWindowPos(hOverlay, HWND_TOPMOST, pt.x, pt.y - INDICATOR_SIZE,
        INDICATOR_SIZE, INDICATOR_SIZE, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}


void DrawOverlay(HWND hwnd) {
    HDC hdc = GetDC(hwnd);
    Graphics g(hdc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);

    SolidBrush brush(Color(255, 255, 0, 0));  // 不透明な赤
    g.FillEllipse(&brush, 0, 0, INDICATOR_SIZE, INDICATOR_SIZE);

    ReleaseDC(hwnd, hdc);
}

// タスクトレイにアイコンを追加
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

// アイコン削除
void RemoveTrayIcon() {
    Shell_NotifyIcon(NIM_DELETE, &nid);
}

// メニュー表示
void ShowTrayMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    HMENU hMenu = CreatePopupMenu();
    AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"終了");

    SetForegroundWindow(hwnd);  // 必須
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

// メッセージ処理
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowTrayMenu(hwnd);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            RemoveTrayIcon();
            PostQuitMessage(0);
        }
        break;
    case WM_PAINT:
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        DrawOverlay(hwnd);
        EndPaint(hwnd, &ps);
        break;
    case WM_DESTROY:
        RemoveTrayIcon();
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}


// オーバーレイウィンドウの作成
void CreateOverlayWindow(HINSTANCE hInst) {
    const wchar_t* CLASS_NAME = L"IMEOverlay";

    WNDCLASS wc = {};
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;  // ← 背景なし（重要）
    RegisterClass(&wc);

    hOverlay = CreateWindowEx(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME,
        NULL,
        WS_POPUP,
        0, 0, INDICATOR_SIZE, INDICATOR_SIZE,
        NULL, NULL, hInst, NULL
    );

    COLORREF transparentColor = RGB(0, 0, 0);  // 完全に透過する色
    SetLayeredWindowAttributes(hOverlay, transparentColor, 255, LWA_COLORKEY);
    ShowWindow(hOverlay, SW_HIDE);
}

// メインループ
DWORD WINAPI UpdateLoop(LPVOID) {
    while (true) {
        if (IsIMEOpen()) {
            POINT pt = GetCaretScreenPos();
            UpdateOverlayPosition(pt);
            ShowWindow(hOverlay, SW_SHOW);
            InvalidateRect(hOverlay, NULL, TRUE);
        }
        else {
            ShowWindow(hOverlay, SW_HIDE);
        }
        Sleep(500);
    }
    return 0;
}

// WinMain
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    hInst = hInstance;
    GdiplusStartupInput gdiStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiStartupInput, NULL);

    CreateOverlayWindow(hInst);
    AddTrayIcon(hOverlay);
    CreateThread(NULL, 0, UpdateLoop, NULL, 0, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    GdiplusShutdown(gdiplusToken);
    return 0;
}

