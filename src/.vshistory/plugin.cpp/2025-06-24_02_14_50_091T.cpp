#include "plugin.h"
#include <vector>
#include <string>
#include <sstream>
#include <windows.h>
#include "pluginsdk/_scriptapi_debug.h"
#include <map>
#include "pluginsdk/_scriptapi_gui.h"
#include <commctrl.h>
#include "pluginsdk/_scriptapi_comment.h"
#include <set>
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
#define MENU_ADD_TO_GROUP_DISASM 1001
#define MENU_ADD_TO_GROUP_DUMP   1002
#define MENU_ADDGROUP_BASE 3000
#define MENU_ADDGROUP_NEW (MENU_ADDGROUP_BASE + 0x7FFF)

HWND hTree = NULL; // TreeView控件句柄

std::set<duint> dumpAddrSet;

void expandAllTree();

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
            std::string display;
            char buf[64];
            sprintf(buf, "0x%llX", (unsigned long long)addr);
            display = buf;
            if (dumpAddrSet.count(addr) == 0) // 不是Dump窗口添加的，显示注释
            {
                char comment[1024] = {0};
                if(Script::Comment::Get(addr, comment) && comment[0])
                {
                    display += " // ";
                    display += comment;
                }
            }
            tvi.hParent = hGroup;
            tvi.item.pszText = (LPSTR)display.c_str();
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
        expandAllTree(); // 刷新后自动展开所有节点
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
    _plugin_menuaddentry(hMenuDisasm, MENU_ADD_TO_GROUP_DISASM, "add code to group");
    _plugin_menuaddentry(hMenuDump, MENU_ADD_TO_GROUP_DUMP, "add code to group");
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

#define ID_MENU_REFRESH 2001
#define ID_MENU_EXPANDALL 2002
#define ID_MENU_COLLAPSEALL 2003

void expandAllTree()
{
    if (!hTree) return;
    HTREEITEM hItem = TreeView_GetRoot(hTree);
    while (hItem)
    {
        TreeView_Expand(hTree, hItem, TVE_EXPAND);
        HTREEITEM hChild = TreeView_GetChild(hTree, hItem);
        while (hChild)
        {
            TreeView_Expand(hTree, hChild, TVE_EXPAND);
            hChild = TreeView_GetNextSibling(hTree, hChild);
        }
        hItem = TreeView_GetNextSibling(hTree, hItem);
    }
}

void collapseAllTree()
{
    if (!hTree) return;
    HTREEITEM hItem = TreeView_GetRoot(hTree);
    while (hItem)
    {
        TreeView_Expand(hTree, hItem, TVE_COLLAPSE);
        hItem = TreeView_GetNextSibling(hTree, hItem);
    }
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
    case WM_CONTEXTMENU:
        {
            // 只在鼠标右键弹出时处理，避免重复弹出
            if (GetFocus() != hwnd && GetFocus() != hTree) break;
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, ID_MENU_REFRESH, "Refresh");
            AppendMenuA(hMenu, MF_STRING, ID_MENU_EXPANDALL, "Expand All");
            AppendMenuA(hMenu, MF_STRING, ID_MENU_COLLAPSEALL, "Collapse All");
            POINT pt;
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);
            if (pt.x == -1 && pt.y == -1) // 键盘弹出
            {
                RECT rect;
                GetWindowRect(hwnd, &rect);
                pt.x = rect.left + 10;
                pt.y = rect.top + 10;
            }
            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            return 0;
        }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_MENU_REFRESH:
            refreshMainWindow();
            return 0;
        case ID_MENU_EXPANDALL:
            expandAllTree();
            return 0;
        case ID_MENU_COLLAPSEALL:
            collapseAllTree();
            return 0;
        }
        break;
    case WM_NOTIFY:
        if (((LPNMHDR)lParam)->hwndFrom == hTree)
        {
            if (((LPNMHDR)lParam)->code == NM_RCLICK)
            {
                if (GetFocus() != hTree) break;
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, ID_MENU_REFRESH, "Refresh");
                AppendMenuA(hMenu, MF_STRING, ID_MENU_EXPANDALL, "Expand All");
                AppendMenuA(hMenu, MF_STRING, ID_MENU_COLLAPSEALL, "Collapse All");
                TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);
                SetWindowLongPtr(hwnd, DWLP_MSGRESULT, TRUE);
                return TRUE;
            }
            if (((LPNMHDR)lParam)->code == NM_DBLCLK)
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
    duint addr = 0;
    bool isDump = false;
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
    case MENU_ADD_TO_GROUP_DISASM:
        addr = Script::Gui::Disassembly::SelectionGetStart();
        isDump = false;
        goto ADD_GROUP_MENU;
    case MENU_ADD_TO_GROUP_DUMP:
        addr = Script::Gui::Dump::SelectionGetStart();
        isDump = true;
        goto ADD_GROUP_MENU;
    ADD_GROUP_MENU:
        {
            HMENU hMenu = CreatePopupMenu();
            int idx = 0;
            for (const auto& group : groupMap)
            {
                AppendMenuA(hMenu, MF_STRING, MENU_ADDGROUP_BASE + idx, group.first.c_str());
                idx++;
            }
            AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuA(hMenu, MF_STRING, MENU_ADDGROUP_NEW, "New Group...");
            POINT pt;
            GetCursorPos(&pt);
            int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, GuiGetWindowHandle(), NULL);
            DestroyMenu(hMenu);
            if (sel >= MENU_ADDGROUP_BASE && sel < MENU_ADDGROUP_NEW)
            {
                int groupIdx = sel - MENU_ADDGROUP_BASE;
                auto it = groupMap.begin();
                std::advance(it, groupIdx);
                if (it != groupMap.end())
                {
                    it->second.push_back(addr);
                    if (isDump) dumpAddrSet.insert(addr);
                    refreshMainWindow();
                    _plugin_logprintf("Added 0x%p to group [%s]\n", (void*)addr, it->first.c_str());
                }
            }
            else if (sel == MENU_ADDGROUP_NEW)
            {
                std::string groupName = InputGroupName();
                if (!groupName.empty())
                {
                    groupMap[groupName].push_back(addr);
                    if (isDump) dumpAddrSet.insert(addr);
                    refreshMainWindow();
                    _plugin_logprintf("Added 0x%p to new group [%s]\n", (void*)addr, groupName.c_str());
                }
            }
        }
        break;
    }
}