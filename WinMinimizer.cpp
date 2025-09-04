#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <d2d1.h>
#include <dwrite.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

struct WindowInfo {
    HWND hWnd;
    WINDOWPLACEMENT wp;
    std::wstring className;
    std::wstring title;
};

std::vector<WindowInfo> windowList;
HWND g_hMainWnd;

// Direct2D 资源
ID2D1Factory* pD2DFactory = nullptr;
IDWriteFactory* pDWriteFactory = nullptr;
ID2D1HwndRenderTarget* pRenderTarget = nullptr;
ID2D1SolidColorBrush* pWhiteBrush = nullptr;
ID2D1SolidColorBrush* pBlueBrush = nullptr;
ID2D1SolidColorBrush* pGrayBrush = nullptr;

// 颜色常量
const D2D1_COLOR_F BACKGROUND_COLOR = D2D1::ColorF(0x2D2D30);    // 深灰背景
const D2D1_COLOR_F BUTTON_COLOR = D2D1::ColorF(0x007ACC);        // 蓝色按钮
const D2D1_COLOR_F BUTTON_HOVER_COLOR = D2D1::ColorF(0x1C97EA);  // 悬停蓝色
const D2D1_COLOR_F BORDER_COLOR = D2D1::ColorF(0xFFFFFF);        // 白色边框
const D2D1_COLOR_F TEXT_COLOR = D2D1::ColorF(0xFFFFFF);          // 白色文字

// 按钮结构
struct Button {
    D2D1_RECT_F rect;
    std::wstring text;
    bool isHovered = false;
    int id;
};

std::vector<Button> buttons;

// 系统窗口类名黑名单
const std::vector<std::wstring> SYSTEM_WINDOW_CLASSES = {
    L"WorkerW",          // 桌面工作窗口
    L"Progman",          // Program Manager
    L"Shell_TrayWnd",    // 任务栏
    L"Button",           // 按钮控件
    L"Static",           // 静态文本
    L"Edit",             // 编辑框
    L"ComboBox",         // 组合框
    L"ListBox",          // 列表框
    L"SysListView32",    // 列表视图
    L"SysTreeView32",    // 树形视图
    L"ToolbarWindow32",  // 工具栏
    L"MSCTFIME UI",      // 输入法界面
    L"IME",              // 输入法
    L"Shell_ChromeWindow", // Chrome 桌面窗口
    L"Windows.UI.Core.CoreWindow" // UWP 应用核心窗口
};

// 窗口标题黑名单（部分匹配）
const std::vector<std::wstring> SYSTEM_WINDOW_TITLES = {
    L"Program Manager",
    L"Default IME",
    L"MSCTFIME UI",
    L"桌面"
};

bool IsSystemWindow(HWND hWnd, const std::wstring& className, const std::wstring& title) {
    // 检查类名黑名单
    for (const auto& sysClass : SYSTEM_WINDOW_CLASSES) {
        if (className.find(sysClass) != std::wstring::npos) {
            return true;
        }
    }

    // 检查标题黑名单
    for (const auto& sysTitle : SYSTEM_WINDOW_TITLES) {
        if (title.find(sysTitle) != std::wstring::npos) {
            return true;
        }
    }

    // 检查窗口样式（排除工具窗口、弹出窗口等）
    LONG style = GetWindowLong(hWnd, GWL_STYLE);
    LONG exStyle = GetWindowLong(hWnd, GWL_EXSTYLE);

    if ((style & WS_CHILD) ||           // 子窗口
        (exStyle & WS_EX_TOOLWINDOW) || // 工具窗口
        (exStyle & WS_EX_NOACTIVATE)) { // 不可激活窗口
        return true;
    }

    // 检查窗口所有者（有所有者的窗口通常是对话框或弹出窗口）
    if (GetWindow(hWnd, GW_OWNER) != NULL) {
        return true;
    }

    return false;
}

bool IsValidAppWindow(HWND hWnd, const std::wstring& className, const std::wstring& title) {
    // 基本可见性检查
    if (!IsWindowVisible(hWnd) || hWnd == g_hMainWnd) {
        return false;
    }

    // 跳过系统窗口
    if (IsSystemWindow(hWnd, className, title)) {
        return false;
    }

    // 检查窗口尺寸（太小的窗口可能是控件或装饰元素）
    RECT rect;
    GetWindowRect(hWnd, &rect);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;

    if (width < 100 || height < 50) {  // 最小尺寸阈值
        return false;
    }

    // 检查窗口是否在屏幕上（避免处理离屏窗口）
    if (rect.right <= 0 || rect.bottom <= 0 ||
        rect.left >= GetSystemMetrics(SM_CXSCREEN) ||
        rect.top >= GetSystemMetrics(SM_CYSCREEN)) {
        return false;
    }

    return true;
}

// 比较函数：按Z序排序（从顶层到底层）
bool CompareZOrder(HWND hWnd1, HWND hWnd2) {
    // 检查窗口1是否在窗口2的上面
    HWND hWndAbove = GetWindow(hWnd1, GW_HWNDNEXT);
    while (hWndAbove != NULL) {
        if (hWndAbove == hWnd2) {
            return false; // hWnd1 在 hWnd2 上面
        }
        hWndAbove = GetWindow(hWndAbove, GW_HWNDNEXT);
    }
    return true; // hWnd1 在 hWnd2 下面
}

BOOL CALLBACK EnumWindowsProc(HWND hWnd, LPARAM lParam) {
    wchar_t className[256] = { 0 };
    wchar_t title[256] = { 0 };
    GetClassName(hWnd, className, 256);
    GetWindowText(hWnd, title, 256);

    std::wstring clsName(className);
    std::wstring windowTitle(title);

    // 应用健壮过滤
    if (!IsValidAppWindow(hWnd, clsName, windowTitle)) {
        return TRUE;
    }

    WindowInfo info;
    info.hWnd = hWnd;
    info.className = clsName;
    info.title = windowTitle;
    info.wp.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hWnd, &info.wp);

    windowList.push_back(info);

    // 最小化窗口
    ShowWindow(hWnd, SW_MINIMIZE);

    return TRUE;
}

void MinimizeForScreenshot() {
    windowList.clear();
    EnumWindows(EnumWindowsProc, 0);

    // 没什么用
    //HWND hDesktop = FindWindow(L"Progman", L"Program Manager");
    //if (hDesktop) {
    //    SetForegroundWindow(hDesktop);
    //}
}

void RestoreWindows() {
    // 按Z序排序（从底层到顶层，这样恢复时顶层窗口最后恢复）
    std::sort(windowList.begin(), windowList.end(),
        [](const WindowInfo& a, const WindowInfo& b) {
            return CompareZOrder(a.hWnd, b.hWnd);
        });

    for (const auto& info : windowList) {
        if (IsWindow(info.hWnd)) {
            SetWindowPlacement(info.hWnd, &info.wp);
            Sleep(30);
        }
    }

    windowList.clear();
}

// Direct2D 初始化
HRESULT InitD2D(HWND hwnd) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &pD2DFactory);
    if (SUCCEEDED(hr)) {
        hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&pDWriteFactory));
    }

    if (SUCCEEDED(hr)) {
        RECT rc;
        GetClientRect(hwnd, &rc);
        hr = pD2DFactory->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top)),
            &pRenderTarget);
    }

    if (SUCCEEDED(hr)) {
        hr = pRenderTarget->CreateSolidColorBrush(BORDER_COLOR, &pWhiteBrush);
    }
    if (SUCCEEDED(hr)) {
        hr = pRenderTarget->CreateSolidColorBrush(BUTTON_COLOR, &pBlueBrush);
    }
    if (SUCCEEDED(hr)) {
        hr = pRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0x666666), &pGrayBrush);
    }

    return hr;
}

void CleanupD2D() {
    if (pWhiteBrush) pWhiteBrush->Release();
    if (pBlueBrush) pBlueBrush->Release();
    if (pGrayBrush) pGrayBrush->Release();
    if (pRenderTarget) pRenderTarget->Release();
    if (pDWriteFactory) pDWriteFactory->Release();
    if (pD2DFactory) pD2DFactory->Release();
}

void DrawButton(const Button& button) {
    // 绘制白色边框矩形
    pRenderTarget->DrawRectangle(
        D2D1::RectF(button.rect.left, button.rect.top, button.rect.right, button.rect.bottom),
        pWhiteBrush, 2.0f);

    // 绘制按钮背景
    ID2D1SolidColorBrush* fillBrush = button.isHovered ? pBlueBrush : pGrayBrush;
    pRenderTarget->FillRectangle(
        D2D1::RectF(button.rect.left + 2, button.rect.top + 2,
            button.rect.right - 2, button.rect.bottom - 2),
        fillBrush);

    // 绘制文字
    IDWriteTextFormat* pTextFormat = nullptr;
    pDWriteFactory->CreateTextFormat(
        L"Arial", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"", &pTextFormat);

    if (pTextFormat) {
        pTextFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        pTextFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

        pRenderTarget->DrawTextW(
            button.text.c_str(), button.text.length(),
            pTextFormat,
            D2D1::RectF(button.rect.left, button.rect.top, button.rect.right, button.rect.bottom),
            pWhiteBrush);

        pTextFormat->Release();
    }
}

void Render() {
    if (!pRenderTarget) return;

    pRenderTarget->BeginDraw();
    pRenderTarget->Clear(BACKGROUND_COLOR);

    // 绘制所有按钮
    for (const auto& button : buttons) {
        DrawButton(button);
    }

    pRenderTarget->EndDraw();
}

void CreateButtons() {
    buttons.clear();

    Button btn1 = { D2D1::RectF(20.0f, 20.0f, 180.0f, 60.0f), L"最小化截图模式", false, 1 };
    Button btn2 = { D2D1::RectF(20.0f, 70.0f, 180.0f, 110.0f), L"恢复窗口", false, 2 };

    buttons.push_back(btn1);
    buttons.push_back(btn2);
}

int GetButtonAtPoint(float x, float y) {
    for (const auto& button : buttons) {
        if (x >= button.rect.left && x <= button.rect.right &&
            y >= button.rect.top && y <= button.rect.bottom) {
            return button.id;
        }
    }
    return 0;
}

void UpdateButtonHover(int x, int y) {
    bool needRedraw = false;
    for (auto& button : buttons) {
        bool wasHovered = button.isHovered;
        button.isHovered = (x >= button.rect.left && x <= button.rect.right &&
            y >= button.rect.top && y <= button.rect.bottom);
        if (wasHovered != button.isHovered) {
            needRedraw = true;
        }
    }
    if (needRedraw) {
        InvalidateRect(g_hMainWnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        InitD2D(hWnd);
        CreateButtons();
        break;

    case WM_DESTROY:
        CleanupD2D();
        PostQuitMessage(0);
        break;

    case WM_PAINT:
        Render();
        break;

    case WM_MOUSEMOVE: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        UpdateButtonHover(x, y);
        break;
    }

    case WM_LBUTTONDOWN: {
        int x = LOWORD(lParam);
        int y = HIWORD(lParam);
        int buttonId = GetButtonAtPoint(x, y);

        switch (buttonId) {
        case 1:
            MinimizeForScreenshot();
            break;
        case 2:
            RestoreWindows();
            break;
        }
        break;
    }

    case WM_SIZE:
        if (pRenderTarget) {
            RECT rc;
            GetClientRect(hWnd, &rc);
            pRenderTarget->Resize(D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top));
            InvalidateRect(hWnd, nullptr, FALSE);
        }
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = { 0 };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ScreenshotHelper";
    wc.style = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClass(&wc)) return 0;

    g_hMainWnd = CreateWindow(wc.lpszClassName, L"截图助手",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        100, 100, 220, 160, nullptr, nullptr, hInstance, nullptr);

    if (!g_hMainWnd) return 0;

    ShowWindow(g_hMainWnd, nCmdShow);
    UpdateWindow(g_hMainWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return (int)msg.wParam;
}