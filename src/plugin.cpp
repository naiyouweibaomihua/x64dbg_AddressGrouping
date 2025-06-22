#include "plugin.h"

// #ifdef _UNICODE
// #error "USE ASCII CODE PAGE"
// #endif

bool bIsMainWindowShow = false;
HWND hMain = NULL;
HINSTANCE g_hInstance = NULL;

// 主窗口弹窗菜单ID
#define MENU_MAINWINDOW_POPUP 0

// Examples: https://github.com/x64dbg/x64dbg/wiki/Plugins
// References:
// - https://help.x64dbg.com/en/latest/developers/plugins/index.html
// - https://x64dbg.com/blog/2016/10/04/architecture-of-x64dbg.html
// - https://x64dbg.com/blog/2016/10/20/threading-model.html
// - https://x64dbg.com/blog/2016/07/30/x64dbg-plugin-sdk.html

// Command use the same signature as main in C
// argv[0] contains the full command, after that are the arguments
// NOTE: arguments are separated by a COMMA (not space like WinDbg)
static bool cbExampleCommand(int argc, char** argv)
{
    if (argc < 3)
    {
        dputs("Usage: " PLUGIN_NAME "expr1, expr2");

        // Return false to indicate failure (used for scripting)
        return false;
    }

    // Helper function for parsing expressions
    // Reference: https://help.x64dbg.com/en/latest/introduction/Expressions.html
    auto parseExpr = [](const char* expression, duint& value)
    {
        bool success = false;
        value = DbgEval(expression, &success);
        if (!success)
            dprintf("Invalid expression '%s'\n", expression);
        return success;
    };

    duint a = 0;
    if (!parseExpr(argv[1], a))
        return false;

    duint b = 0;
    if (!parseExpr(argv[2], b))
        return false;

    // NOTE: Look at x64dbg-sdk/pluginsdk/bridgemain.h for a list of available functions.
    // The Script:: namespace and DbgFunctions()->... are also good to check out.

    // Do something meaningful with the arguments
    duint result = a + b;
    dprintf("$result = 0x%p + 0x%p = 0x%p\n", a, b, result);

    // The $result variable can be used for scripts
    DbgValToString("$result", result);

    return true;
}

// Initialize your plugin data here.
bool pluginInit(PLUG_INITSTRUCT* initStruct)
{
    return true;
}

// Deinitialize your plugin data here.
// NOTE: you are responsible for gracefully closing your GUI
// This function is not executed on the GUI thread, so you might need
// to use WaitForSingleObject or similar to wait for everything to close.
void pluginStop()
{
}

// Do GUI/Menu related things here.
// This code runs on the GUI thread: GetCurrentThreadId() == GuiGetMainThreadId()
// You can get the HWND using GuiGetWindowHandle()
void pluginSetup()
{
    _plugin_menuaddentry(hMenu, MENU_MAINWINDOW_POPUP, "Plugin Template");
}

// 弹出空窗口并输出日志
DWORD WINAPI MsgLoopThread(LPVOID)
{
    MSG msg;
    WNDCLASSA wc = {0};
    HWND hwnd;

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "PluginTemplateWindow";

    RegisterClassA(&wc);

    hwnd = CreateWindowA(
        "PluginTemplateWindow",
        "Plugin Template",
        WS_OVERLAPPEDWINDOW,
        100, 100, 400, 300,
        NULL, NULL, g_hInstance, NULL
    );

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    dprintf("hello plugintemplate!\n");

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    bIsMainWindowShow = false;
    return (int)msg.wParam;
}

// 最简窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg)
    {
    case WM_CREATE:
        return 0;
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, iMsg, wParam, lParam);
}

// 菜单回调，只处理弹窗
extern "C" __declspec(dllexport) void CBMENUENTRY(CBTYPE cbType, PLUG_CB_MENUENTRY* info)
{
    switch (info->hEntry)
    {
    case MENU_MAINWINDOW_POPUP:
        if (!bIsMainWindowShow && DbgIsDebugging())
        {
            CloseHandle(CreateThread(0, 0, MsgLoopThread, 0, 0, 0));
            bIsMainWindowShow = true;
        }
        break;
    }
}
