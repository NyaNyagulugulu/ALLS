#include <windows.h>
#include <gdiplus.h>
#include <tchar.h>
#include <math.h>
#include <string>
#include <vector>
#include <fstream>
#include "json.hpp"

#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;
using std::vector;
using std::wstring;
using std::ifstream;
using json = nlohmann::json;

#define LOADER_DOTS 18
#define LOADER_RADIUS 22
#define LOADER_LENGTH 8
#define LOADER_THICK 3
#define LOADER_SPEED 60

struct Action {
    int type; // 1 = launch bat
    std::wstring path;
};

struct Stage {
    int step;
    std::wstring text;
    int end_ms;
    Action action; // Action upon stage start
};


vector<Stage> stageList;
wstring launchBat = L"start.bat";

// 状态变量
int currentStage = -1;
DWORD endTick = 0; // when should current stage be end (relative).
DWORD tickCount = 0; // relative (elapsed) tick
DWORD tickOffset = 0; // Will be set by CREATE
bool timerActive = true;

Image* gLogo = nullptr;
Image* gBackground = nullptr;
ULONG_PTR gdiplusToken;
wstring statusText = L"启动中";
int stepNum = 1;
DWORD drawTickCount = 0; // tick for drawing

// 一些其他的配置
wstring model_text;

// ------------------ 配置读取 ------------------
std::wstring ToWString(const std::string& input) {
    if (input.empty()) return L"";

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), (int)input.length(), NULL, 0);
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), (int)input.length(), &result[0], size_needed);
    return result;
}


bool LoadConfig() {
    std::wstring configPathW = L"alls_config.json";

    // 转换为 std::string（UTF-8），以便 std::ifstream 使用
    int len = WideCharToMultiByte(CP_UTF8, 0, configPathW.c_str(), -1, NULL, 0, NULL, NULL);
    std::string configPath(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, configPathW.c_str(), -1, &configPath[0], len, NULL, NULL);

    std::ifstream f(configPath);
    if (!f.is_open()) return false;

    json j;
    f >> j;
    f.close();

    model_text = ToWString(j.value("model_text", "ALLS HX2"));

    int last_stage_end_ms = 0;

    for (const auto& st : j["stages"]) {
        Stage s;
        s.step = st.value("step", 0);
        s.text = ToWString(st.value("text", ""));
        s.end_ms = last_stage_end_ms + st.value("delay", 3000);
        last_stage_end_ms = s.end_ms;
        if (st.contains("action") && st["action"].is_object()) {
            const auto& action = st["action"];
            s.action.type = action.value("type", 0);
            s.action.path = ToWString(action.value("path", ""));
        }
        stageList.push_back(s);
    }

    return true;
}

// ------------------ 维护菜单 ------------------
void ShowMaintenanceMenu(HWND hwnd) {
    int result = MessageBoxW(hwnd,
        L"你已中断启动流程。\n请选择：\n\n是：启动桌面\n否：重启系统\n取消：退出程序",
        L"ALLS 维护菜单",
        MB_YESNOCANCEL | MB_TOPMOST | MB_ICONQUESTION);

    if (result == IDYES) {
        ShellExecuteW(NULL, L"open", L"explorer.exe", NULL, NULL, SW_SHOWNORMAL);
    }
    //else if (result == IDNO) {
    //    ExitWindowsEx(EWX_REBOOT, 0);
    //}
    // else Cancel → 自动退出（什么都不做）
}

// ------------------ bat 启动 ------------------
bool LaunchBatFromConfig(wstring batPath) {
    if (batPath == ToWString("")) {
        return false;
    }
    DWORD fa = GetFileAttributesW(batPath.c_str());
    if (fa != INVALID_FILE_ATTRIBUTES && !(fa & FILE_ATTRIBUTE_DIRECTORY)) {
        ShellExecuteW(NULL, L"open", batPath.c_str(), NULL, NULL, SW_SHOWNORMAL);
        return true;
    }
    return false;
}

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
    g.DrawString(model_text.c_str(), -1, &font1, layout1, &fmt, &brush);

    int stepTextY = allsTextY + 48;
    Font font2(L"Arial", 26, FontStyleRegular);
    wchar_t stepStr[32];
    wsprintf(stepStr, L"STEP %d", stepNum);
    RectF layout2(0, (REAL)stepTextY, (REAL)winW, 36);
    g.DrawString(stepStr, -1, &font2, layout2, &fmt, &brush);

    int loadingY = stepTextY + 50;
    Font font3(L"微软雅黑", 22, FontStyleRegular);
    StringFormat fmtLeft;
    fmtLeft.SetAlignment(StringAlignmentNear);
    RectF textBounds;
    g.MeasureString(statusText.c_str(), -1, &font3, PointF(0, 0), &fmtLeft, &textBounds);
    int textWidth = (int)ceil(textBounds.Width);
    int loaderWidth = LOADER_RADIUS * 2;
    int gap = 8;
    int totalWidth = loaderWidth + gap + textWidth;
    int startX = cx - totalWidth / 2;
    int centerY = loadingY + LOADER_RADIUS;

    DrawLoader(g, startX + LOADER_RADIUS, centerY, drawTickCount);

    int textBaseY = centerY - (int)ceil(textBounds.Height / 2);
    RectF layout3((REAL)(startX + loaderWidth + gap), (REAL)textBaseY, (REAL)textWidth + 2, textBounds.Height + 8);
    g.DrawString(statusText.c_str(), -1, &font3, layout3, &fmtLeft, &brush);

    BitBlt(hdc, 0, 0, winW, winH, mdc, 0, 0, SRCCOPY);

    SelectObject(mdc, oldBmp);
    DeleteObject(mbmp);
    DeleteDC(mdc);
    ReleaseDC(hwnd, hdc);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        SetTimer(hwnd, 1, 60, NULL); // loader speed
        tickOffset = GetTickCount();
        break;
    case WM_TIMER: {
        if (!timerActive) {
            break;
        }
        drawTickCount++;
        DWORD now = GetTickCount();
        tickCount = now - tickOffset;
        if (tickCount > endTick) {
            currentStage++;
            if (currentStage < (int)stageList.size()) {
                stepNum = stageList[currentStage].step;
                statusText = stageList[currentStage].text;
                endTick = (DWORD)stageList[currentStage].end_ms;

                // run action
                switch (stageList[currentStage].action.type) {
                case 1:
                    LaunchBatFromConfig(stageList[currentStage].action.path);
                    break;
                }
            } else {
                timerActive = false;
                PostQuitMessage(0);
            }
        }

        InvalidateRect(hwnd, NULL, FALSE);
        break;
    }
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            timerActive = false;
            ShowMaintenanceMenu(hwnd);
            PostQuitMessage(0);
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        Paint(hwnd);
        EndPaint(hwnd, &ps);
        break;
    }
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
    LoadConfig();

    const TCHAR CLASS_NAME[] = _T("ALLSLauncherHX2");
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

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