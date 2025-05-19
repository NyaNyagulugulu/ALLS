#include <windows.h>
#include <gdiplus.h>
#include <tchar.h>
#include <math.h>

#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;

#define LOADER_DOTS 18
#define LOADER_RADIUS 22
#define LOADER_LENGTH 8
#define LOADER_THICK 3
#define LOADER_SPEED 60

ULONG_PTR gdiplusToken;
int tickCount = 0;
Image* gLogo = nullptr;
Image* gBackground = nullptr;

void LoadImages() {
    gLogo = new Image(L"logo.png");
    gBackground = new Image(L"background.png");
}

void DrawLoader(Graphics& g, int cx, int cy, int frame) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    for (int i = 0; i < LOADER_DOTS; ++i) {
        double angle = 2 * 3.141592653589793 * ((i + frame) % LOADER_DOTS) / LOADER_DOTS - 3.141592653589793 / 2;
        double sn = sin(angle), cs = cos(angle);
        int x0 = cx + (int)(cs * (LOADER_RADIUS - LOADER_LENGTH));
        int y0 = cy + (int)(sn * (LOADER_RADIUS - LOADER_LENGTH));
        int x1 = cx + (int)(cs * LOADER_RADIUS);
        int y1 = cy + (int)(sn * LOADER_RADIUS);

        BYTE alpha = (BYTE)(120 + 135 * (i) / (LOADER_DOTS - 1));
        Color color(alpha, 60, 60, 60);
        Pen pen(color, LOADER_THICK);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, x0, y0, x1, y1);
    }
    g.SetSmoothingMode(SmoothingModeHighQuality);
}

void Paint(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int winW = rc.right, winH = rc.bottom;

    HDC hdc = GetDC(hwnd);
    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP mbmp = CreateCompatibleBitmap(hdc, winW, winH);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mdc, mbmp);

    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mdc, &rc, bg);
    DeleteObject(bg);

    Graphics g(mdc);
    g.SetSmoothingMode(SmoothingModeHighQuality);

    // 居中绘制 background.png
    int bgW = 0, bgH = 0, bgX = 0, bgY = 0;
    if (gBackground && gBackground->GetLastStatus() == Ok) {
        bgW = gBackground->GetWidth();
        bgH = gBackground->GetHeight();
        bgX = (winW - bgW) / 2;
        bgY = (winH - bgH) / 2;
        g.DrawImage(gBackground, bgX, bgY, bgW, bgH);
    }

    int cx = bgX + bgW / 2;
    int cy = bgY + bgH / 2;

    int y_offset = 250;
    int logo_down = -110;
    int logoMaxW = (int)(bgW * 0.62);
    int logoMaxH = (int)(bgH * 0.26);

    int logoDrawW = 0, logoDrawH = 0;
    if (gLogo && gLogo->GetLastStatus() == Ok) {
        int imgW = gLogo->GetWidth();
        int imgH = gLogo->GetHeight();
        double scale = min((double)logoMaxW / imgW, (double)logoMaxH / imgH);
        logoDrawW = (int)(imgW * scale);
        logoDrawH = (int)(imgH * scale);
        int logoDrawX = cx - logoDrawW / 2;
        int logoDrawY = cy - 120 + y_offset + logo_down;
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(gLogo, logoDrawX, logoDrawY, logoDrawW, logoDrawH);
    }

    // -------- 文字与加载圈 --------
    int allsTextY = cy - 10 + y_offset + 50;
    FontFamily ff(L"Segoe UI");
    Font font1(&ff, 32, FontStyleRegular);
    SolidBrush brush(Color(255, 0, 0, 0));
    RectF layout1(0, (REAL)allsTextY, (REAL)winW, 48);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    g.DrawString(L"ALLS HX2", -1, &font1, layout1, &fmt, &brush);

    // 用 Arial 或 微软雅黑，E 不会变 F
    int stepTextY = allsTextY + 48;
    Font font2(L"Arial", 26, FontStyleRegular); // <<<<<<<<<<<
    RectF layout2(0, (REAL)stepTextY, (REAL)winW, 36);
    g.DrawString(L"STEP 1", -1, &font2, layout2, &fmt, &brush);

    int loadingY = stepTextY + 50;
    Font font3(L"微软雅黑", 22, FontStyleRegular);
    StringFormat fmtLeft;
    fmtLeft.SetAlignment(StringAlignmentNear);
    RectF textBounds;
    g.MeasureString(L"启动中", -1, &font3, PointF(0, 0), &fmtLeft, &textBounds);
    int textWidth = (int)ceil(textBounds.Width);
    int loaderWidth = LOADER_RADIUS * 2;
    int gap = 8;
    int totalWidth = loaderWidth + gap + textWidth;
    int startX = cx - totalWidth / 2;
    int centerY = loadingY + LOADER_RADIUS;

    DrawLoader(g, startX + LOADER_RADIUS, centerY, tickCount);

    int textBaseY = centerY - (int)ceil(textBounds.Height / 2);
    // 字体区域高度加大，底部+8保证不被裁剪
    RectF layout3((REAL)(startX + loaderWidth + gap), (REAL)textBaseY, (REAL)textWidth + 2, textBounds.Height + 8);
    g.DrawString(L"启动中", -1, &font3, layout3, &fmtLeft, &brush);

    BitBlt(hdc, 0, 0, winW, winH, mdc, 0, 0, SRCCOPY);

    SelectObject(mdc, oldBmp);
    DeleteObject(mbmp);
    DeleteDC(mdc);
    ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, LOADER_SPEED, NULL);
        break;
    case WM_TIMER:
        tickCount++;
        InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        Paint(hwnd);
        EndPaint(hwnd, &ps);
    } break;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) PostQuitMessage(0);
        break;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    LoadImages();

    const TCHAR CLASS_NAME[] = _T("ALLSLauncherHX2");
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    // 全屏无边框
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, _T("ALLS HX2"),
        WS_POPUP | WS_VISIBLE,
        0, 0, screenW, screenH,
        nullptr, nullptr, hInstance, nullptr
    );
    if (!hwnd) return 0;

    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    if (gLogo) delete gLogo;
    if (gBackground) delete gBackground;
    GdiplusShutdown(gdiplusToken);
    return 0;
}