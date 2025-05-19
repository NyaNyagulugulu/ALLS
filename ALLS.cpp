#include <windows.h>
#include <gdiplus.h>
#include <tchar.h>
#include <math.h>
#pragma comment(lib, "gdiplus.lib")

#define TIMER_ID 101
#define LOADER_DOTS 12
#define LOADER_RADIUS 16
#define LOADER_DOT_R_BIG 4
#define LOADER_DOT_R_SMALL 2

using namespace Gdiplus;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int loader_frame = 0;
ULONG_PTR gdiplusToken;
Image* gLogo = nullptr;

// 用于从资源中加载PNG图片
Image* LoadPngFromResource(HINSTANCE hInst, LPCTSTR resName, LPCTSTR resType)
{
    HRSRC hRes = FindResource(hInst, resName, resType);
    if (!hRes) return nullptr;
    DWORD imageSize = SizeofResource(hInst, hRes);
    HGLOBAL hGlobal = LoadResource(hInst, hRes);
    if (!hGlobal) return nullptr;
    void* pData = LockResource(hGlobal);
    if (!pData) return nullptr;

    HGLOBAL hBuffer = GlobalAlloc(GMEM_MOVEABLE, imageSize);
    void* pBuffer = GlobalLock(hBuffer);
    memcpy(pBuffer, pData, imageSize);

    IStream* pStream = nullptr;
    CreateStreamOnHGlobal(hBuffer, TRUE, &pStream);
    Image* img = new Image(pStream);
    pStream->Release();

    GlobalUnlock(hBuffer); // 释放内存锁
    // 注意：hBuffer会随IStream释放

    return img;
}

// 居中画LOGO图片，若加载失败给出提示
void DrawLogoImg(HDC hdc, int winW, int winH)
{
    if (!gLogo) {
        TextOut(hdc, winW / 2 - 100, winH / 2 - 120, _T("LOGO未加载"), 8);
        return;
    }
    if (gLogo->GetLastStatus() != Ok) {
        TextOut(hdc, winW / 2 - 100, winH / 2 - 120, _T("LOGO加载失败"), 8);
        return;
    }
    int imgW = gLogo->GetWidth();
    int imgH = gLogo->GetHeight();
    // 自动缩放以适配不同分辨率
    int maxW = winW * 0.7;
    int maxH = winH * 0.24;
    double scale = min((double)maxW / imgW, (double)maxH / imgH);
    if (scale > 1.0) scale = 1.0;
    int drawW = (int)(imgW * scale);
    int drawH = (int)(imgH * scale);
    int x = (winW - drawW) / 2;
    int y = winH / 2 - drawH - 40;
    Graphics graphics(hdc);
    graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    graphics.DrawImage(gLogo, x, y, drawW, drawH);
}

// 绘制旋转加载圈
void DrawLoader(HDC hdc, int x, int y, int frame)
{
    for (int i = 0; i < LOADER_DOTS; ++i)
    {
        double angle = 2 * 3.1415926 * ((i + frame) % LOADER_DOTS) / LOADER_DOTS;
        int px = x + (int)(cos(angle) * LOADER_RADIUS);
        int py = y + (int)(sin(angle) * LOADER_RADIUS);
        int gray = 170 + 80 * ((i == 0) ? 1 : 0);
        HBRUSH hBrush = CreateSolidBrush(RGB(gray, gray, gray));
        HBRUSH hOld = (HBRUSH)SelectObject(hdc, hBrush);
        int dotR = (i == 0) ? LOADER_DOT_R_BIG : LOADER_DOT_R_SMALL;
        Ellipse(hdc, px - dotR, py - dotR, px + dotR, py + dotR);
        SelectObject(hdc, hOld);
        DeleteObject(hBrush);
        frame = (frame + 1) % LOADER_DOTS;
    }
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // 初始化GDI+
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    // 载入LOGO图片（注意ID和类型和rc中一致！推荐RCDATA类型）
    gLogo = LoadPngFromResource(hInstance, _T("IDB_PNG1"), _T("RCDATA"));
    if (!gLogo || gLogo->GetLastStatus() != Ok) {
        MessageBox(NULL, _T("LOGO加载失败！"), _T("错误"), MB_OK);
    }

    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = _T("ALLSLauncherHX");
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc);

    // 创建无边框全屏窗口
    HWND hWnd = CreateWindow(
        _T("ALLSLauncherHX"), _T("ALLS HX"),
        WS_POPUP | WS_VISIBLE,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN),
        NULL, NULL, hInstance, NULL
    );
    ShowWindow(hWnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hWnd);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (gLogo) delete gLogo;
    GdiplusShutdown(gdiplusToken);
    return (int)msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int width, height;
    switch (message)
    {
    case WM_CREATE:
        SetTimer(hWnd, TIMER_ID, 60, NULL); // 60ms刷新加载圈
        ShowCursor(FALSE); // 隐藏鼠标
        break;
    case WM_TIMER:
        if (wParam == TIMER_ID) {
            loader_frame = (loader_frame + 1) % LOADER_DOTS;
            InvalidateRect(hWnd, NULL, FALSE);
        }
        break;
    case WM_SIZE:
        width = LOWORD(lParam);
        height = HIWORD(lParam);
        break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            ShowCursor(TRUE);
            PostQuitMessage(0);
        }
        break;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        int winW = rc.right, winH = rc.bottom;

        // 上半黑色
        HBRUSH hBlack = CreateSolidBrush(RGB(20, 20, 20));
        RECT topRect = rc; topRect.bottom = winH / 2;
        FillRect(hdc, &topRect, hBlack);
        DeleteObject(hBlack);

        // 下半白色
        HBRUSH hWhite = CreateSolidBrush(RGB(250, 250, 247));
        RECT botRect = rc; botRect.top = winH / 2;
        FillRect(hdc, &botRect, hWhite);
        DeleteObject(hWhite);

        // LOGO图片
        DrawLogoImg(hdc, winW, winH);

        // STEP文字
        SetBkMode(hdc, TRANSPARENT);
        HFONT hFontStep = CreateFont(38, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, _T("Arial"));
        HFONT hOld = (HFONT)SelectObject(hdc, hFontStep);
        SetTextColor(hdc, RGB(30, 30, 30));
        int baseY = winH / 2 + 30;
        TextOut(hdc, winW / 2 - 90, baseY, _T("ALLS HX"), 8);
        TextOut(hdc, winW / 2 - 65, baseY + 46, _T("STEP 4"), 6);
        SelectObject(hdc, hOld);
        DeleteObject(hFontStep);

        // Loading日语和圈
        HFONT hFontJp = CreateFont(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, VARIABLE_PITCH, _T("微软雅黑"));
        hOld = (HFONT)SelectObject(hdc, hFontJp);
        SetTextColor(hdc, RGB(30, 30, 30));
        int loaderX = winW / 2 - 120;
        int loaderY = baseY + 90;
        DrawLoader(hdc, loaderX, loaderY + 13, loader_frame);
        TextOut(hdc, winW / 2 - 85, loaderY, _T("ネットワークの設定をしています"), 20);
        SelectObject(hdc, hOld);
        DeleteObject(hFontJp);

        EndPaint(hWnd, &ps);
    }
    break;
    case WM_DESTROY:
        ShowCursor(TRUE);
        KillTimer(hWnd, TIMER_ID);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}