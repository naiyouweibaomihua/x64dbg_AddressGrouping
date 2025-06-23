#include "plugin.h"
#include <vector>
#include <string>
#include <sstream>
#include <windows.h>
#include "pluginsdk/_scriptapi_debug.h"
#include <map>
#include "pluginsdk/_scriptapi_gui.h"
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
using namespace Script::Debug;

// #ifdef _UNICODE
// #error "USE ASCII CODE PAGE"
// #endif

bool bIsMainWindowShow = false;
HWND hMain = NULL;
HINSTANCE g_hInstance = NULL;

// 存储断点信息
std::string g_breakpointInfo;

// 分组管理数据结构
std::map<std::string, std::vector<duint>> groupMap;

// 主窗口弹窗菜单ID
#define MENU_MAINWINDOW_POPUP 0

// 新菜单项ID
#define MENU_ADD_TO_GROUP 1001

HWND hTree = NULL; // TreeView控件句柄

// 刷新TreeView内容
void refreshTreeView()
{
    if (!hTree)
        return;
    TreeView_DeleteAllItems(hTree);
    TVINSERTSTRUCTA tvi = {0};
    for (const auto& group : groupMap)
    {
        tvi.hParent = TVI_ROOT;
        tvi.hInsertAfter = TVI_LAST;
        tvi.item.mask = TVIF_TEXT;
        tvi.item.pszText = (LPSTR)group.first.c_str();
        HTREEITEM hGroup = TreeView_InsertItem(hTree, &tvi);
        for (duint addr : group.second)
        {
            char buf[64];
            sprintf(buf, "0x%llX", (unsigned long long)addr);
            tvi.hParent = hGroup;
            tvi.item.pszText = buf;
            // 地址作为lParam存储
            tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
            tvi.item.lParam = (LPARAM)addr;
            TreeView_InsertItem(hTree, &tvi);
        }
    }
}

void refreshMainWindow()
{
    if (hMain)
    {
        InvalidateRect(hMain, NULL, TRUE);
        UpdateWindow(hMain);
        refreshTreeView();
    }
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
    _plugin_menuaddentry(hMenuDisasm, MENU_ADD_TO_GROUP, "add code to group");
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
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1); // 这里不再用背景色，由 WndProc 处理
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "PluginTemplateWindow";

    RegisterClassA(&wc);

    hwnd = CreateWindowA(
        "PluginTemplateWindow",
        "Plugin Template",
        WS_OVERLAPPEDWINDOW,
        100, 100, 500, 400,
        NULL, NULL, g_hInstance, NULL
    );

    hMain = hwnd; // 保存主窗口句柄

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    dprintf("hello plugintemplate!\n");

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    bIsMainWindowShow = false;
    hMain = NULL;
    return (int)msg.wParam;
}

// 修改窗口过程，集成TreeView控件
LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg)
    {
    case WM_CREATE:
        {
            INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_TREEVIEW_CLASSES};
            InitCommonControlsEx(&icex);
            hTree = CreateWindowExA(0, WC_TREEVIEWA, "", WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS,
                0, 0, 480, 360, hwnd, (HMENU)100, GetModuleHandle(NULL), NULL);
            refreshTreeView();
        }
        return 0;
    case WM_SIZE:
        if (hTree)
        {
            MoveWindow(hTree, 0, 0, LOWORD(lParam), HIWORD(lParam), TRUE);
        }
        break;
    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->hwndFrom == hTree && ((LPNMHDR)lParam)->code == NM_DBLCLK)
        {
            TVHITTESTINFO ht = {0};
            GetCursorPos(&ht.pt);
            ScreenToClient(hTree, &ht.pt);
            TreeView_HitTest(hTree, &ht);
            if (ht.hItem)
            {
                TVITEMA item = {0};
                item.mask = TVIF_PARAM | TVIF_CHILDREN;
                item.hItem = ht.hItem;
                TreeView_GetItem(hTree, &item);
                if (item.lParam != 0 && item.cChildren == 0)
                {
                    duint addr = (duint)item.lParam;
                    char cmd[64];
                    sprintf(cmd, "disasm 0x%llX", (unsigned long long)addr);
                    DbgCmdExecDirect(cmd);
                }
            }
        }
        break;
    case WM_DESTROY:
        hTree = NULL;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, iMsg, wParam, lParam);
}

// 弹窗输入分组名
std::string InputGroupName()
{
    char buf[128] = {0};
    if(Script::Gui::InputLine("Input group name", buf))
        return std::string(buf);
    return std::string();
}

// 在分组有变化时刷新窗口和TreeView
extern "C" __declspec(dllexport) void CBMENUENTRY(CBTYPE cbType, PLUG_CB_MENUENTRY* info)
{
    switch (info->hEntry)
    {
    case MENU_MAINWINDOW_POPUP:
        if (!bIsMainWindowShow && DbgIsDebugging())
        {
            DbgCmdExecDirect("bp 401000");
            CloseHandle(CreateThread(0, 0, MsgLoopThread, 0, 0, 0));
            bIsMainWindowShow = true;
        }
        break;
    case MENU_ADD_TO_GROUP:
        {
            duint addr = Script::Gui::Disassembly::SelectionGetStart();
            std::string groupName = InputGroupName();
            if (!groupName.empty())
            {
                groupMap[groupName].push_back(addr);
                refreshMainWindow(); // 分组有变化时刷新窗口和TreeView
                _plugin_logprintf("Added 0x%p to group [%s]\n", (void*)addr, groupName.c_str());
            }
        }
        break;
    }
}
