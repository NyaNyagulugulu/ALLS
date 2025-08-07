#include <windows.h>
#include <gdiplus.h>
#include <tchar.h>
#include <math.h>
#include <string>
#include <vector>
#include <fstream>
#include <utility>
#include <iostream>
#include "json.hpp"

#pragma comment(lib, "Gdiplus.lib")

using namespace Gdiplus;
using std::vector;
using std::wstring;
using std::ifstream;
using json = nlohmann::json;

#define LOADER_DOTS 15
#define LOADER_RADIUS 15
#define LOADER_LENGTH 4
#define LOADER_THICK 2
#define LOADER_SPEED 120

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

wstring configPathW = L"alls_config.json";
bool debugMode = false;

vector<Stage> stageList;
wstring model_text;
int esc_action; // 0 = do not respond to ESC, 1 = exit (default), 2 = open explorer.exe for bash replaced environments
double size_w_override = 0; // 0 for no override, 0 < size < 10 for scale over WinW, >= 10 for absolute size override
double size_h_override = 0; // 0 for no override, 0 < size < 10 for scale over WinH, >= 10 for absolute size override 
bool keep_image_ratio = true;
wstring logo_path = L"logo.png";
wstring background_path = L"background.png";
DWORD background_color = 0x000000;
bool show_cursor = false;

// 状态变量
DWORD drawTickCount = 0; // tick for drawing
int currentStage = -1;
DWORD endTick = 0; // when should current stage be end (relative).
DWORD tickCount = 0; // relative (elapsed) tick
ULONGLONG tickOffset = 0; // Will be set by CREATE
bool timerActive = true;

Image* gLogo = nullptr;
Image* gBackground = nullptr;
ULONG_PTR gdiplusToken;
wstring statusText = L"启动中";
int stepNum = 1;


std::wstring ToWString(const std::string& input) {
    if (input.empty()) return L"";

    int size_needed = MultiByteToWideChar(CP_UTF8, 0, input.c_str(), (int)input.length(), NULL, 0);
    std::wstring result(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, input.c_str(), (int)input.length(), &result[0], size_needed);
    return result;
}


bool LoadConfig() {
    // 转换为 std::string（UTF-8），以便 std::ifstream 使用
    int len = WideCharToMultiByte(CP_UTF8, 0, configPathW.c_str(), -1, NULL, 0, NULL, NULL);
    std::string configPath(len, 0);
    WideCharToMultiByte(CP_UTF8, 0, configPathW.c_str(), -1, &configPath[0], len, NULL, NULL);

    std::ifstream f(configPath);
    if (!f.is_open()) {
        std::cout << "Failed to load config file from path '" << configPath.c_str() <<  "'" << std::endl;
        exit(1);
    }

    json j;
    f >> j;
    f.close();

    model_text = ToWString(j.value("model_text", "ALLS HX2"));
    esc_action = j.value("esc_action", 1);
    show_cursor = j.value("show_cursor", false);
    
    if (j.contains("background") && j["background"].is_object()) {
        const auto& bg = j["background"];
        background_path = ToWString(bg.value("path", "background.png"));
        size_w_override = bg.value("size_w_override", 0.0);
        size_h_override = bg.value("size_h_override", 0.0);
        keep_image_ratio = bg.value("keep_image_ratio", true);
        background_color = std::stoi(bg.value("color", "0x000000"), nullptr, 16);
    }

    if (j.contains("logo") && j["logo"].is_object()) {
        const auto& bg = j["logo"];
        logo_path = ToWString(bg.value("path", "logo.png"));
    }

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

// ------------------ bat 启动 ------------------
bool LaunchBat(wstring batPath) {
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
    gLogo = new Image(logo_path.c_str());
    gBackground = new Image(background_path.c_str());
}

void DrawLoader(Graphics& g, int cx, int cy, int frame, double ratio) {
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    for (int i = 0; i < LOADER_DOTS; ++i) {
        double angle = 2 * 3.141592653589793 * ((i + frame) % LOADER_DOTS) / LOADER_DOTS - 3.141592653589793 / 2;
        double sn = sin(angle), cs = cos(angle);
        int x0 = cx + (int)(cs * (LOADER_RADIUS - LOADER_LENGTH)) * ratio;
        int y0 = cy + (int)(sn * (LOADER_RADIUS - LOADER_LENGTH)) * ratio;
        int x1 = cx + (int)(cs * LOADER_RADIUS) * ratio;
        int y1 = cy + (int)(sn * LOADER_RADIUS) * ratio;

        BYTE alpha = (BYTE)(120 + 135 * (i) / (LOADER_DOTS - 1));
        Color color(alpha, 60, 60, 60);
        Pen pen(color, LOADER_THICK * ratio);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, x0, y0, x1, y1);
    }
    g.SetSmoothingMode(SmoothingModeHighQuality);
}

std::pair<int, int> ComputeFinalSize(int bgW, int bgH, int winW, int winH)
{
    double newW = static_cast<double>(bgW);
    double newH = static_cast<double>(bgH);

    bool w_set = size_w_override > 0.0;
    bool h_set = size_h_override > 0.0;

    double aspect = (bgH != 0) ? static_cast<double>(bgW) / bgH : 1.0;

    if (w_set) {
        if (size_w_override < 10.0)
            newW = winW * size_w_override;
        else
            newW = size_w_override;
    }

    if (h_set) {
        if (size_h_override < 10.0)
            newH = winH * size_h_override;
        else
            newH = size_h_override;
    }

    if (keep_image_ratio && (w_set ^ h_set)) {
        if (w_set && !h_set)
            newH = newW / aspect;
        else if (!w_set && h_set)
            newW = newH * aspect;
    }

    return {
        static_cast<int>(std::round(newW)),
        static_cast<int>(std::round(newH))
    };
}

void Paint(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int winW = rc.right, winH = rc.bottom;

    HDC hdc = GetDC(hwnd);
    HDC mdc = CreateCompatibleDC(hdc);
    HBITMAP mbmp = CreateCompatibleBitmap(hdc, winW, winH);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mdc, mbmp);

    HBRUSH bg = CreateSolidBrush(background_color);
    FillRect(mdc, &rc, bg);
    DeleteObject(bg);

    Graphics g(mdc);
    g.SetSmoothingMode(SmoothingModeHighQuality);

    // 居中绘制 background.png
    int bgW = 0, bgH = 0, bgX = 0, bgY = 0;
    double h_ratio = 1;
    if (gBackground && gBackground->GetLastStatus() == Ok) {
        bgW = gBackground->GetWidth();
        bgH = gBackground->GetHeight();

        auto res = ComputeFinalSize(bgW, bgH, winW, winH);
        h_ratio = (double)res.second / (double)bgH;
        bgW = res.first;
        bgH = res.second;
        bgX = (winW - bgW) / 2;
        bgY = (winH - bgH) / 2;

        g.DrawImage(gBackground, bgX, bgY, bgW, bgH);
    }

    int cx = bgX + bgW / 2;
    int cy = bgY + bgH / 2;

    int y_offset = 250 * h_ratio;
    int logo_down = -220 * h_ratio;
    int logoMaxW = (int)(bgW * 0.45);
    int logoMaxH = (int)(bgH * 0.26);

    int logoDrawW = 0, logoDrawH = 0;
    if (gLogo && gLogo->GetLastStatus() == Ok) {
        int imgW = gLogo->GetWidth();
        int imgH = gLogo->GetHeight();
        double scale = min((double)logoMaxW / imgW, (double)logoMaxH / imgH);
        logoDrawW = (int)(imgW * scale);
        logoDrawH = (int)(imgH * scale);
        int logoDrawX = cx - logoDrawW / 2;
        int logoDrawY = cy + y_offset + logo_down;
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(gLogo, logoDrawX, logoDrawY, logoDrawW, logoDrawH);
    }

    // -------- 文字与加载圈 --------
    int allsTextY = cy - 10 * h_ratio + y_offset + 90 * h_ratio;
    FontFamily ff(L"Arial");
    Font font1(&ff, 14 * h_ratio, FontStyleRegular);
    SolidBrush brush(Color(255, 0, 0, 0));
    RectF layout1(0, (REAL)allsTextY, (REAL)winW, 48);
    StringFormat fmt;
    fmt.SetAlignment(StringAlignmentCenter);
    g.DrawString(model_text.c_str(), -1, &font1, layout1, &fmt, &brush);

    int stepTextY = allsTextY + 30 * h_ratio;
    Font font2(L"Arial", 14 * h_ratio, FontStyleRegular);
    wchar_t stepStr[32];
    wsprintf(stepStr, L"STEP %d", stepNum);
    RectF layout2(0, (REAL)stepTextY, (REAL)winW, 36);
    g.DrawString(stepStr, -1, &font2, layout2, &fmt, &brush);

    int loadingY = stepTextY + 30 * h_ratio;
    Font font3(L"黑体", 14 * h_ratio, FontStyleRegular);
    StringFormat fmtLeft;
    fmtLeft.SetAlignment(StringAlignmentNear);
    RectF textBounds;
    g.MeasureString(statusText.c_str(), -1, &font3, PointF(0, 0), &fmtLeft, &textBounds);
    int textWidth = (int)ceil(textBounds.Width);
    int loaderWidth = LOADER_RADIUS * 2 * h_ratio;
    int gap = 3 * h_ratio;
    int totalWidth = loaderWidth + gap + textWidth;
    int startX = cx - totalWidth / 2;
    int centerY = loadingY + LOADER_RADIUS * h_ratio;

    DrawLoader(g, startX + LOADER_RADIUS * h_ratio, centerY, drawTickCount, h_ratio);

    int textBaseY = centerY - (int)ceil(textBounds.Height / 2 - 2.4 * h_ratio);
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
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        SetTimer(hwnd, 1, 60, NULL); // loader speed
        tickOffset = GetTickCount64();
        break;
    case WM_TIMER: {
        if (!timerActive) {
            break;
        }
        drawTickCount++;
        ULONGLONG now = GetTickCount64();
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
                    LaunchBat(stageList[currentStage].action.path);
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
            
            switch (esc_action) {
            case 0:
                // do nothing
                break;
            case 1:
                timerActive = false;
                PostQuitMessage(0);
                break;
            case 2:
                timerActive = false;
                ShellExecuteW(NULL, L"open", L"explorer.exe", NULL, NULL, SW_SHOWNORMAL);
                PostQuitMessage(0);
                break;
            }
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

void InitConsole() {
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    std::ios::sync_with_stdio(); // optional
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR lpCmdLine, int nCmdShow) {
    // 简单命令行参数解析
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc - 1; ++i) {
        if (wcscmp(argv[i], L"--config") == 0) {
            configPathW = argv[i + 1];
            break;
        }
    }
    for (int i = 1; i < argc - 1; ++i) {
        if (wcscmp(argv[i], L"--debug") == 0) {
            debugMode = true;
            break;
        }
    }
    LocalFree(argv);

    if (debugMode) {
        InitConsole();
        std::cout << "Debug console started" << std::endl;
    }

    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);
    LoadConfig();
    LoadImages();
    ShowCursor(show_cursor);

    const TCHAR CLASS_NAME[] = _T("ALLSLauncher");
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    HWND hwnd = CreateWindowEx(
        0, CLASS_NAME, model_text.c_str(),
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