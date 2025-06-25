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
#include <fstream>
#include <shlobj.h>
#include "pluginsdk/_scriptapi_module.h"
#pragma comment(lib, "comctl32.lib")
using namespace Script::Debug;

#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

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

#define MENU_EXPORT_CONFIG 2004
#define MENU_IMPORT_CONFIG 2005

HWND hTree = NULL; // TreeView控件句柄

std::set<duint> dumpAddrSet;

#define WM_SET_BREAKPOINT (WM_USER + 100)

// 全局灰色画刷
HBRUSH hBrushGray = NULL;

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
        // 隐藏分组节点CheckBox
        TVITEMA groupItem = {0};
        groupItem.mask = TVIF_HANDLE | TVIF_STATE;
        groupItem.hItem = hGroup;
        groupItem.stateMask = TVIS_STATEIMAGEMASK;
        groupItem.state = INDEXTOSTATEIMAGEMASK(0); // 0=隐藏
        TreeView_SetItem(hTree, &groupItem);
        for (duint addr : group.second)
        {
            std::string display;
            char buf[64];
            sprintf(buf, "0x%llX", (unsigned long long)addr);
            display = buf;
            if (group.first != "memory") {
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
            HTREEITEM hItem = TreeView_InsertItem(hTree, &tvi);
            if (group.first != "memory") {
                TVITEMA item = {0};
                item.mask = TVIF_HANDLE | TVIF_STATE;
                item.hItem = hItem;
                item.stateMask = TVIS_STATEIMAGEMASK;
                item.state = INDEXTOSTATEIMAGEMASK(1); // 未选中
                TreeView_SetItem(hTree, &item);
            }
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
    int hSubMenu = _plugin_menuadd(hMenu, "Config");
    _plugin_menuaddentry(hSubMenu, MENU_EXPORT_CONFIG, "export config");
    _plugin_menuaddentry(hSubMenu, MENU_IMPORT_CONFIG, "import config");
    _plugin_menuaddentry(hMenuDisasm, MENU_ADD_TO_GROUP_DISASM, "add code to group");
    _plugin_menuaddentry(hMenuDump, MENU_ADD_TO_GROUP_DUMP, "add code to [memory] group");
}

// 弹出空窗口并输出日志
DWORD WINAPI MsgLoopThread(LPVOID)
{
    MSG msg;
    WNDCLASSA wc = {0};
    HWND hwnd;

    // 初始化灰色画刷
    if (!hBrushGray)
        hBrushGray = CreateSolidBrush(RGB(200, 200, 200));

    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = g_hInstance;
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = hBrushGray;
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "PluginTemplateWindow";

    RegisterClassA(&wc);

    hwnd = CreateWindowA(
        "PluginTemplateWindow",
        "Plugin Template",
        WS_OVERLAPPEDWINDOW,
        100, 100, 600, 500, // 宽600, 高500
        NULL, NULL, g_hInstance, NULL
    );

    hMain = hwnd; // 保存主窗口句柄

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    refreshTreeView(); // 自动刷新一次，确保分组节点checkbox消失

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
            hTree = CreateWindowExA(0, WC_TREEVIEWA, "", WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_CHECKBOXES,
                0, 0, 480, 360, hwnd, (HMENU)100, GetModuleHandle(NULL), NULL);
            // 设置TreeView控件背景为灰色
            TreeView_SetBkColor(hTree, RGB(200, 200, 200));
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
            if (((LPNMHDR)lParam)->code == NM_CLICK)
            {
                DWORD pos = GetMessagePos();
                POINT pt = { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
                ScreenToClient(hTree, &pt);
                TVHITTESTINFO ht = {0};
                ht.pt = pt;
                TreeView_HitTest(hTree, &ht);
                if (ht.hItem)
                {
                    TVITEMA item = {0};
                    item.mask = TVIF_HANDLE | TVIF_STATE | TVIF_PARAM;
                    item.hItem = ht.hItem;
                    TreeView_GetItem(hTree, &item);
                    // 判断是否点击在CheckBox上
                    if ((ht.flags & TVHT_ONITEMSTATEICON) && item.lParam != 0)
                    {
                        // 获取父节点文本
                        HTREEITEM hParent = TreeView_GetParent(hTree, ht.hItem);
                        if (hParent)
                        {
                            TVITEMA parentItem = {0};
                            parentItem.mask = TVIF_TEXT | TVIF_HANDLE;
                            parentItem.hItem = hParent;
                            char parentText[256] = {0};
                            parentItem.pszText = parentText;
                            parentItem.cchTextMax = sizeof(parentText)-1;
                            TreeView_GetItem(hTree, &parentItem);
                            if (strcmp(parentText, "memory") != 0)
                            {
                                // 切换CheckBox状态
                                bool checked = ((item.state & TVIS_STATEIMAGEMASK) >> 12) == 2;
                                if (!checked) // 之前未选中，点击后会变为选中
                                {
                                    PostMessage(hwnd, WM_SET_BREAKPOINT, (WPARAM)item.lParam, 0);
                                }
                                else // 之前已选中，点击后会变为未选中
                                {
                                    // 禁用断点
                                    char cmd[64];
                                    sprintf(cmd, "bd 0x%llX", (unsigned long long)item.lParam);
                                    DbgCmdExecDirect(cmd);
                                }
                            }
                        }
                    }
                }
            }
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
                    item.mask = TVIF_PARAM | TVIF_CHILDREN | TVIF_HANDLE | TVIF_TEXT;
                    item.hItem = ht.hItem;
                    char text[256] = {0};
                    item.pszText = text;
                    item.cchTextMax = sizeof(text)-1;
                    TreeView_GetItem(hTree, &item);
                    // 获取父节点文本
                    HTREEITEM hParent = TreeView_GetParent(hTree, ht.hItem);
                    if (hParent)
                    {
                        TVITEMA parentItem = {0};
                        parentItem.mask = TVIF_TEXT | TVIF_HANDLE;
                        parentItem.hItem = hParent;
                        char parentText[256] = {0};
                        parentItem.pszText = parentText;
                        parentItem.cchTextMax = sizeof(parentText)-1;
                        TreeView_GetItem(hTree, &parentItem);
                        if (strcmp(parentText, "memory") == 0)
                        {
                            // 跳转到内存窗口
                            char cmd[64];
                            sprintf(cmd, "dump 0x%llX", (unsigned long long)item.lParam);
                            DbgCmdExecDirect(cmd);
                        }
                        else
                        {
                            char cmd[64];
                            sprintf(cmd, "disasm 0x%llX", (unsigned long long)item.lParam);
                            DbgCmdExecDirect(cmd);
                        }
                    }
                }
            }
        }
        break;
    case WM_SET_BREAKPOINT:
        {
            duint addr = (duint)wParam;
            char cmd[64];
            sprintf(cmd, "bp 0x%llX", (unsigned long long)addr);
            DbgCmdExecDirect(cmd);
        }
        return 0;
    case WM_DESTROY:
        hTree = NULL;
        PostQuitMessage(0);
        return 0;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    {
        HDC hdc = (HDC)wParam;
        SetBkColor(hdc, RGB(200, 200, 200));
        return (INT_PTR)hBrushGray;
    }
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

// 辅助函数：获取地址所在模块名和RVA
bool getModuleAndRva(duint addr, std::string& modName, duint& rva)
{
    duint base = Script::Module::BaseFromAddr(addr);
    if (base)
    {
        char mod[MAX_MODULE_SIZE] = {0};
        if (Script::Module::NameFromAddr(addr, mod))
        {
            modName = mod;
            rva = addr - base;
            return true;
        }
    }
    return false;
}

// 辅助函数：根据模块名和RVA获取绝对地址
bool getAddrFromModuleAndRva(const std::string& modName, duint rva, duint& addr)
{
    duint base = Script::Module::BaseFromName(modName.c_str());
    if (base)
    {
        addr = base + rva;
        return true;
    }
    return false;
}

// 修改导出配置：CPU分组导出模块名+RVA，memory分组导出绝对地址
void exportConfig()
{
    char filePath[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Export Config";
    if (!GetSaveFileNameA(&ofn)) return;
    std::ofstream ofs(filePath);
    for (const auto& group : groupMap)
    {
        ofs << "[" << group.first << "]\n";
        for (duint addr : group.second)
        {
            if (group.first == "memory")
            {
                ofs << std::hex << addr << "\n";
            }
            else
            {
                std::string modName;
                duint rva = 0;
                if (getModuleAndRva(addr, modName, rva))
                    ofs << modName << " " << std::hex << rva << "\n";
            }
        }
    }
    ofs.close();
    _plugin_logprintf("Exported config to %s\n", filePath);
}

// 修改导入配置：CPU分组读取模块名+RVA，memory分组读取绝对地址
void importConfig()
{
    char filePath[MAX_PATH] = {0};
    OPENFILENAMEA ofn = {0};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "Text Files (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filePath;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = "Import Config";
    if (!GetOpenFileNameA(&ofn)) return;
    std::ifstream ifs(filePath);
    if (!ifs) return;
    groupMap.clear();
    dumpAddrSet.clear();
    std::string line, curGroup;
    while (std::getline(ifs, line))
    {
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']')
        {
            curGroup = line.substr(1, line.size() - 2);
            groupMap[curGroup] = {};
        }
        else if (!curGroup.empty())
        {
            if (curGroup == "memory")
            {
                duint addr = 0;
                std::istringstream iss(line);
                iss >> std::hex >> addr;
                groupMap[curGroup].push_back(addr);
                dumpAddrSet.insert(addr);
            }
            else
            {
                std::istringstream iss(line);
                std::string modName;
                duint rva = 0;
                iss >> modName >> std::hex >> rva;
                duint addr = 0;
                if (getAddrFromModuleAndRva(modName, rva, addr))
                    groupMap[curGroup].push_back(addr);
            }
        }
    }
    ifs.close();
    refreshMainWindow();
    _plugin_logprintf("Imported config from %s\n", filePath);
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
    case MENU_EXPORT_CONFIG:
        exportConfig();
        break;
    case MENU_IMPORT_CONFIG:
        importConfig();
        break;
    case MENU_ADD_TO_GROUP_DISASM:
        addr = Script::Gui::Disassembly::SelectionGetStart();
        isDump = false;
        goto ADD_GROUP_MENU;
    case MENU_ADD_TO_GROUP_DUMP:
        addr = Script::Gui::Dump::SelectionGetStart();
        isDump = true;
        groupMap["memory"].push_back(addr);
        dumpAddrSet.insert(addr);
        refreshMainWindow();
        _plugin_logprintf("Added 0x%p to group [memory]\n", (void*)addr);
        break;
    ADD_GROUP_MENU:
        {
            HMENU hMenu = CreatePopupMenu();
            int idx = 0;
            for (const auto& group : groupMap)
            {
                if (group.first == "memory") continue;
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
                int skip = 0;
                for (; it != groupMap.end(); ++it)
                {
                    if (it->first == "memory") continue;
                    if (skip == groupIdx) break;
                    ++skip;
                }
                if (it != groupMap.end())
                {
                    it->second.push_back(addr);
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
                    refreshMainWindow();
                    _plugin_logprintf("Added 0x%p to new group [%s]\n", (void*)addr, groupName.c_str());
                }
            }
        }
        break;
    }
}