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
#include "pluginsdk/_scriptapi_stack.h"
#include <algorithm>
#include <locale>
#include <codecvt>
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

// �洢�ϵ���Ϣ
std::string g_breakpointInfo;

// ���ݽṹ
std::map<std::string, std::vector<duint>> groupMap;

// ÿ����ַ�ڵ��Ӧ���ӽڵ�
std::map<duint, std::vector<duint>> childMap;

// ���˵�ID
#define MENU_MAINWINDOW_POPUP 0

// �Ӳ˵�ID
#define MENU_ADD_TO_GROUP_DISASM 1001
#define MENU_ADD_TO_GROUP_DUMP   1002
#define MENU_ADDGROUP_BASE 3000
#define MENU_ADDGROUP_NEW (MENU_ADDGROUP_BASE + 0x7FFF)

#define MENU_EXPORT_CONFIG 2004
#define MENU_IMPORT_CONFIG 2005
#define MENU_ADD_TO_GROUP_STACK 1003
#define MENU_ABOUT 2006

HWND hTree = NULL; // TreeView���

std::set<duint> dumpAddrSet;

#define WM_SET_BREAKPOINT (WM_USER + 100)

// ȫ��ˢ����
HBRUSH hBrushGray = NULL;

// ע����Ϣ
std::map<duint, std::string> addrComments;

// utf8תgbk����
std::string Utf8ToGbk(const std::string& utf8);

void expandAllTree();
void collapseAllTree();
void insertAddressNode(const std::string& groupName, HTREEITEM hParent, duint addr, TVINSERTSTRUCTA& tvi);

// ˢ��TreeView���ݣ�A�����
void refreshTreeView()
{
	if (!hTree)
		return;
	TreeView_DeleteAllItems(hTree);
	// ɾ�����Դ��룬ֻ����ʵ�ʷ�������߼�
	TVINSERTSTRUCTA tvi = { 0 };
	for (const auto& group : groupMap)
	{
		tvi.hParent = TVI_ROOT;
		tvi.hInsertAfter = TVI_LAST;
		tvi.item.mask = TVIF_TEXT;
		tvi.item.pszText = (LPSTR)group.first.c_str();
		HTREEITEM hGroup = TreeView_InsertItem(hTree, &tvi);
		// ���ط���ڵ�CheckBox
		TVITEMA groupItem = { 0 };
		groupItem.mask = TVIF_HANDLE | TVIF_STATE;
		groupItem.hItem = hGroup;
		groupItem.stateMask = TVIS_STATEIMAGEMASK;
		groupItem.state = INDEXTOSTATEIMAGEMASK(0); // 0=����
		TreeView_SetItem(hTree, &groupItem);
		for (duint addr : group.second)
		{
			insertAddressNode(group.first, hGroup, addr, tvi);
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
		expandAllTree(); // �Զ�չ�������ӽڵ�
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
	_plugin_menuaddentry(hMenu, MENU_MAINWINDOW_POPUP, "Address Grouping");
	int hSubMenu = _plugin_menuadd(hMenu, "Config");
	_plugin_menuaddentry(hSubMenu, MENU_EXPORT_CONFIG, "export config");
	_plugin_menuaddentry(hSubMenu, MENU_IMPORT_CONFIG, "import config");
	_plugin_menuaddentry(hMenuDisasm, MENU_ADD_TO_GROUP_DISASM, "add code to group");
	_plugin_menuaddentry(hMenuDump, MENU_ADD_TO_GROUP_DUMP, "add code to [memory] group");
	_plugin_menuaddentry(hMenuStack, MENU_ADD_TO_GROUP_STACK, "add stack address to [stack] group");
	// About�˵�����ӵ����˵�
	_plugin_menuaddentry(hMenu, MENU_ABOUT, "About");
}

// �����߳�
DWORD WINAPI MsgLoopThread(LPVOID)
{
	MSG msg;
	WNDCLASSA wc = { 0 };
	HWND hwnd;

	// ��ʼ������ˢ
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
	wc.lpszClassName = "AddressGroupingWindow";

	RegisterClassA(&wc);

	hwnd = CreateWindowA(
		"AddressGroupingWindow",
		"Address Grouping",
		WS_OVERLAPPEDWINDOW,
		100, 100, 600, 500, // ��600, ��500
		NULL, NULL, g_hInstance, NULL
	);

	hMain = hwnd; // ���������ھ��

	ShowWindow(hwnd, SW_SHOW);
	UpdateWindow(hwnd);
	refreshTreeView(); // �Զ�ˢ��һ�Σ�ȷ���ڵ�checkbox��ʧ

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
#define ID_MENU_ADD_COMMENT 30001
#define ID_MENU_DELETE_GROUP 30002
#define ID_MENU_DELETE_ADDR  30003
#define ID_MENU_ADD_CHILD_ADDR 30004

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

// ��ȡĳ���ڵ��Ӧ�ĸ��ڵ�����
std::string GetRootGroupName(HTREEITEM hItem)
{
	HTREEITEM hParent = TreeView_GetParent(hTree, hItem);
	HTREEITEM hCurrent = hItem;
	char text[256] = {0};
	while (hParent)
	{
		hCurrent = hParent;
		hParent = TreeView_GetParent(hTree, hCurrent);
	}
	TVITEMA item = {0};
	item.mask = TVIF_TEXT | TVIF_HANDLE;
	item.hItem = hCurrent;
	item.pszText = text;
	item.cchTextMax = sizeof(text) - 1;
	TreeView_GetItem(hTree, &item);
	return std::string(text);
}

// �޸Ĺ������޸�TreeView���
LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
	switch (iMsg)
	{
	case WM_CREATE:
	{
		INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_TREEVIEW_CLASSES };
		InitCommonControlsEx(&icex);
		hTree = CreateWindowExA(0, WC_TREEVIEWA, "", WS_VISIBLE | WS_CHILD | WS_BORDER | TVS_HASLINES | TVS_LINESATROOT | TVS_HASBUTTONS | TVS_CHECKBOXES,
			0, 0, 480, 360, hwnd, (HMENU)100, GetModuleHandle(NULL), NULL);
		// ����TreeView���Ϊ��ɫ
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
		// ֻ������TreeView�ϵ���Ľڵ�
		if (GetFocus() != hwnd && GetFocus() != hTree) break;
		HMENU hMenu = CreatePopupMenu();
		AppendMenuA(hMenu, MF_STRING, ID_MENU_REFRESH, "Refresh");
		AppendMenuA(hMenu, MF_STRING, ID_MENU_EXPANDALL, "Expand All");
		AppendMenuA(hMenu, MF_STRING, ID_MENU_COLLAPSEALL, "Collapse All");
		POINT pt;
		pt.x = LOWORD(lParam);
		pt.y = HIWORD(lParam);
		if (pt.x == -1 && pt.y == -1) // �Ҽ��˵�
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
				TVHITTESTINFO ht = { 0 };
				ht.pt = pt;
				TreeView_HitTest(hTree, &ht);
				if (ht.hItem)
				{
					TVITEMA item = { 0 };
					item.mask = TVIF_HANDLE | TVIF_STATE | TVIF_PARAM;
					item.hItem = ht.hItem;
					TreeView_GetItem(hTree, &item);
					// �ж��Ƿ���CheckBox
					if ((ht.flags & TVHT_ONITEMSTATEICON) && item.lParam != 0)
					{
						// ��ȡ�ڵ㸸�ڵ�
						HTREEITEM hParent = TreeView_GetParent(hTree, ht.hItem);
						if (hParent)
						{
							TVITEMA parentItem = { 0 };
							parentItem.mask = TVIF_TEXT | TVIF_HANDLE;
							parentItem.hItem = hParent;
							char parentText[256] = { 0 };
							parentItem.pszText = parentText;
							parentItem.cchTextMax = sizeof(parentText) - 1;
							TreeView_GetItem(hTree, &parentItem);
							if (strcmp(parentText, "memory") != 0)
							{
								// �޸�CheckBox״̬
								bool checked = ((item.state & TVIS_STATEIMAGEMASK) >> 12) == 2;
								if (!checked) // ֮ǰδѡ�У���Ϊѡ��
								{
									PostMessage(hwnd, WM_SET_BREAKPOINT, (WPARAM)item.lParam, 0);
								}
								else // ֮ǰѡ�У���Ϊδѡ��
								{
									// ִ���ж�
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
				ScreenToClient(hTree, &pt);
				TVHITTESTINFO ht = { 0 };
				ht.pt = pt;
				TreeView_HitTest(hTree, &ht);
				if (ht.hItem)
				{
					TVITEMA item = { 0 };
					item.mask = TVIF_PARAM | TVIF_HANDLE | TVIF_TEXT;
					item.hItem = ht.hItem;
					char nodeText[256] = { 0 };
					item.pszText = nodeText;
					item.cchTextMax = sizeof(nodeText) - 1;
					TreeView_GetItem(hTree, &item);
					HTREEITEM hParent = TreeView_GetParent(hTree, ht.hItem);
					char parentText[256] = { 0 };
					if (hParent) // �ӽڵ�
					{
						TVITEMA parentItem = { 0 };
						parentItem.mask = TVIF_TEXT | TVIF_HANDLE;
						parentItem.hItem = hParent;
						parentItem.pszText = parentText;
						parentItem.cchTextMax = sizeof(parentText) - 1;
						TreeView_GetItem(hTree, &parentItem);
						HMENU hMenu = CreatePopupMenu();
						AppendMenuA(hMenu, MF_STRING, ID_MENU_ADD_COMMENT, "add comment");
						AppendMenuA(hMenu, MF_STRING, ID_MENU_ADD_CHILD_ADDR, "add child address");
						AppendMenuA(hMenu, MF_STRING, ID_MENU_DELETE_ADDR, "delete address");
						AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
						AppendMenuA(hMenu, MF_STRING, ID_MENU_REFRESH, "Refresh");
						AppendMenuA(hMenu, MF_STRING, ID_MENU_EXPANDALL, "Expand All");
						AppendMenuA(hMenu, MF_STRING, ID_MENU_COLLAPSEALL, "Collapse All");
						ClientToScreen(hTree, &pt);
						int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
						DestroyMenu(hMenu);
						if (sel == ID_MENU_ADD_COMMENT)
						{
							char comment[256] = { 0 };
							auto it = addrComments.find(item.lParam);
							if (it != addrComments.end())
								strncpy(comment, it->second.c_str(), sizeof(comment) - 1);
							if (Script::Gui::InputLine(u8"���뱸ע(����ɾ��)", comment))
							{
								std::string commentStr(comment); // �ù��캯��
								commentStr = Utf8ToGbk(commentStr); // ת��
								if (strlen(comment) > 0)
									addrComments[item.lParam] = commentStr;
								else
									addrComments.erase(item.lParam);
								refreshMainWindow();
							}
						}
						else if (sel == ID_MENU_ADD_CHILD_ADDR)
						{
							duint childAddr = 0;
							bool valid = false;
							if (strcmp(parentText, "memory") == 0)
							{
								childAddr = Script::Gui::Dump::SelectionGetStart();
								valid = (childAddr != 0);
							}
							else if (strcmp(parentText, "stack") == 0)
							{
								childAddr = Script::Gui::Stack::SelectionGetStart();
								valid = (childAddr != 0);
							}
							else
							{
								childAddr = Script::Gui::Disassembly::SelectionGetStart();
								valid = (childAddr != 0);
							}
							if (!valid)
							{
								MessageBoxA(hwnd, "Please select a valid address in the corresponding window before adding a child node!", "Tip", MB_OK | MB_ICONWARNING);
								break;
							}
							if ((duint)item.lParam == childAddr)
							{
								MessageBoxA(hwnd, "Cannot add the node itself as its child!", "Tip", MB_OK | MB_ICONWARNING);
								break;
							}
							childMap[(duint)item.lParam].push_back(childAddr);
							refreshMainWindow();
						}
						else if (sel == ID_MENU_DELETE_ADDR)
						{
							// �ҵ��ӽڵ�
							TVITEMA parentItem = { 0 };
							parentItem.mask = TVIF_TEXT | TVIF_HANDLE;
							parentItem.hItem = hParent;
							char parentText[256] = { 0 };
							parentItem.pszText = parentText;
							parentItem.cchTextMax = sizeof(parentText) - 1;
							TreeView_GetItem(hTree, &parentItem);
							std::string groupName = parentText;
							auto it = groupMap.find(groupName);
							if (it != groupMap.end())
							{
								auto& vec = it->second;
								vec.erase(std::remove(vec.begin(), vec.end(), (duint)item.lParam), vec.end());
								addrComments.erase((duint)item.lParam);
								refreshMainWindow();
							}
						}
						else if (sel == ID_MENU_REFRESH)
							refreshMainWindow();
						else if (sel == ID_MENU_EXPANDALL)
							expandAllTree();
						else if (sel == ID_MENU_COLLAPSEALL)
							collapseAllTree();
						SetWindowLongPtr(hwnd, DWLP_MSGRESULT, TRUE);
						return TRUE;
					}
					else // ���ڵ�
					{
						HMENU hMenu = CreatePopupMenu();
						AppendMenuA(hMenu, MF_STRING, ID_MENU_DELETE_GROUP, "delete group");
						AppendMenuA(hMenu, MF_SEPARATOR, 0, NULL);
						AppendMenuA(hMenu, MF_STRING, ID_MENU_REFRESH, "Refresh");
						AppendMenuA(hMenu, MF_STRING, ID_MENU_EXPANDALL, "Expand All");
						AppendMenuA(hMenu, MF_STRING, ID_MENU_COLLAPSEALL, "Collapse All");
						ClientToScreen(hTree, &pt);
						int sel = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0, hwnd, NULL);
						DestroyMenu(hMenu);
						if (sel == ID_MENU_DELETE_GROUP)
						{
							std::string groupName = nodeText;
							groupMap.erase(groupName);
							refreshMainWindow();
						}
						else if (sel == ID_MENU_REFRESH)
							refreshMainWindow();
						else if (sel == ID_MENU_EXPANDALL)
							expandAllTree();
						else if (sel == ID_MENU_COLLAPSEALL)
							collapseAllTree();
						SetWindowLongPtr(hwnd, DWLP_MSGRESULT, TRUE);
						return TRUE;
					}
				}
				// �Ҽ�������ڵ�ʱȫѡ���нڵ�
				HMENU hMenu = CreatePopupMenu();
				AppendMenuA(hMenu, MF_STRING, ID_MENU_REFRESH, "Refresh");
				AppendMenuA(hMenu, MF_STRING, ID_MENU_EXPANDALL, "Expand All");
				AppendMenuA(hMenu, MF_STRING, ID_MENU_COLLAPSEALL, "Collapse All");
				ClientToScreen(hTree, &pt);
				TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, NULL);
				DestroyMenu(hMenu);
				SetWindowLongPtr(hwnd, DWLP_MSGRESULT, TRUE);
				return TRUE;
			}
			if (((LPNMHDR)lParam)->code == NM_DBLCLK)
			{
				TVHITTESTINFO ht = { 0 };
				GetCursorPos(&ht.pt);
				ScreenToClient(hTree, &ht.pt);
				TreeView_HitTest(hTree, &ht);
				if (ht.hItem)
				{
					// �ж��Ƿ��ǽڵ㣨�ӽڵ㣩����ֹչ��/�۵�
					if (!TreeView_GetParent(hTree, ht.hItem))
					{
						SetWindowLongPtr(hwnd, DWLP_MSGRESULT, TRUE);
						return TRUE;
					}
					TVITEMA item = { 0 };
					item.mask = TVIF_PARAM | TVIF_CHILDREN | TVIF_HANDLE | TVIF_TEXT;
					item.hItem = ht.hItem;
					char text[256] = { 0 };
					item.pszText = text;
					item.cchTextMax = sizeof(text) - 1;
					TreeView_GetItem(hTree, &item);
					// ��ȡ�����ӽڵ�
					std::string groupName = GetRootGroupName(ht.hItem);
					if (groupName == "memory")
					{
						// ת���ڴ�
						char cmd[64];
						sprintf(cmd, "dump 0x%llX", (unsigned long long)item.lParam);
						DbgCmdExecDirect(cmd);
					}
					else if (groupName == "stack")
					{
						// ת������ʾʹ��API
						MessageBoxA(hwnd, "Please locate this address manually in the stack window.", "Tip", MB_OK | MB_ICONINFORMATION);
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

// ��������
std::string InputGroupName()
{
	char buf[128] = { 0 };
	if (Script::Gui::InputLine(u8"Input group name", buf))
		return std::string(buf);
	return std::string();
}

// ��ȡģ��RVA������ģ������RVA
bool getModuleAndRva(duint addr, std::string& modName, duint& rva)
{
	duint base = Script::Module::BaseFromAddr(addr);
	if (base)
	{
		char mod[MAX_MODULE_SIZE] = { 0 };
		if (Script::Module::NameFromAddr(addr, mod))
		{
			modName = mod;
			rva = addr - base;
			return true;
		}
	}
	return false;
}

// ��ȡģ��RVA�����ص�ַ
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

// �������ã�CPUģ��+RVA��memory��ַ
void exportConfig()
{
	char filePath[MAX_PATH] = { 0 };
	OPENFILENAMEA ofn = { 0 };
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
	// ���ע��
	ofs << "[comments]\n";
	for (const auto& kv : addrComments)
	{
		if (!kv.second.empty())
			ofs << "0x" << std::hex << kv.first << "=" << kv.second << "\n";
	}
	// ����ӽڵ����
	ofs << "[children]\n";
	for (const auto& kv : childMap)
	{
		if (!kv.second.empty())
		{
			ofs << "0x" << std::hex << kv.first << "=";
			for (size_t i = 0; i < kv.second.size(); ++i)
			{
				ofs << "0x" << std::hex << kv.second[i];
				if (i + 1 < kv.second.size())
					ofs << ",";
			}
			ofs << "\n";
		}
	}
	ofs.close();
	_plugin_logprintf("Exported config to %s\n", filePath);
}

// �������ã�CPUģ��+RVA��memory��ַ
void importConfig()
{
	char filePath[MAX_PATH] = { 0 };
	OPENFILENAMEA ofn = { 0 };
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
	addrComments.clear();
	childMap.clear();
	std::string line, curGroup;
	bool inComments = false;
	bool inChildren = false;
	while (std::getline(ifs, line))
	{
		if (line.empty()) continue;
		if (line.front() == '[' && line.back() == ']')
		{
			curGroup = line.substr(1, line.size() - 2);
			inComments = (curGroup == "comments");
			inChildren = (curGroup == "children");
			if (!inComments && !inChildren)
				groupMap[curGroup] = {};
			continue;
		}
		if (inComments)
		{
			// ��ַ=ע��
			size_t eq = line.find('=');
			if (eq != std::string::npos)
			{
				std::string addrStr = line.substr(0, eq);
				std::string comment = line.substr(eq + 1);
				duint addr = 0;
				if (addrStr.find("0x") == 0 || addrStr.find("0X") == 0)
					addr = std::stoull(addrStr, nullptr, 16);
				else
					addr = std::stoull(addrStr, nullptr, 10);
				addrComments[addr] = comment;
			}
			continue;
		}
		if (inChildren)
		{
			// 0x=0x1,0x2,...
			size_t eq = line.find('=');
			if (eq != std::string::npos)
			{
				std::string parentStr = line.substr(0, eq);
				std::string childrenStr = line.substr(eq + 1);
				duint parent = 0;
				if (parentStr.find("0x") == 0 || parentStr.find("0X") == 0)
					parent = std::stoull(parentStr, nullptr, 16);
				else
					parent = std::stoull(parentStr, nullptr, 10);
				std::vector<duint> children;
				std::stringstream ss(childrenStr);
				std::string child;
				while (std::getline(ss, child, ','))
				{
					duint c = 0;
					if (child.find("0x") == 0 || child.find("0X") == 0)
						c = std::stoull(child, nullptr, 16);
					else
						c = std::stoull(child, nullptr, 10);
					children.push_back(c);
				}
				childMap[parent] = children;
			}
			continue;
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

// �����ñ仯ʱˢ��TreeView
extern "C" __declspec(dllexport) void CBMENUENTRY(CBTYPE cbType, PLUG_CB_MENUENTRY * info)
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
	case MENU_ADD_TO_GROUP_STACK:
		addr = Script::Gui::Stack::SelectionGetStart();
		if (addr)
		{
			groupMap["stack"].push_back(addr);
			refreshMainWindow();
			_plugin_logprintf("Added 0x%p to group [stack]\n", (void*)addr);
		}
		break;
	case MENU_ABOUT:
		MessageBoxA(
			NULL,
			"x64dbg Address Grouping Plugin\n\n" \
			"Author: naiyouweibaomihua\n" \
			"GitHub: https://github.com/naiyouweibaomihua/x64dbg_AddressGrouping.git\n\n" \
			"Features:\n" \
			"- Group and manage addresses from disassembly, memory, and stack windows.\n" \
			"- Tree view, comments, child nodes, import/export, and more.\n\n" \
			"For details, see the README on GitHub.",
			"About Address Grouping Plugin",
			MB_OK | MB_ICONINFORMATION);
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
					groupName = Utf8ToGbk(groupName); // ת��
					groupMap[groupName].push_back(addr);
					refreshMainWindow();
					_plugin_logprintf("Added 0x%p to new group [%s]\n", (void*)addr, groupName.c_str());
				}
			}
		}
		break;
	}
}

void insertAddressNode(const std::string& groupName, HTREEITEM hParent, duint addr, TVINSERTSTRUCTA& tvi)
{
	std::string display;
	char buf[64];
	sprintf(buf, "0x%llX", (unsigned long long)addr);
	display = buf;
	// ��ʾע�ͣ�û��ע����ʾx64dbgע��
	auto it = addrComments.find(addr);
	if (it != addrComments.end() && !it->second.empty()) {
		display += " // ";
		display += it->second;
	}
	else if (groupName != "memory") {
		char comment[1024] = { 0 };
		if (Script::Comment::Get(addr, comment) && comment[0])
		{
			display += " // ";
			display += Utf8ToGbk(std::string(comment)); // ת�룬��ֹx64dbgע����������
		}
	}
	tvi.hParent = hParent;
	tvi.item.pszText = (LPSTR)display.c_str();
	tvi.item.mask = TVIF_TEXT | TVIF_PARAM;
	tvi.item.lParam = (LPARAM)addr;
	HTREEITEM hItem = TreeView_InsertItem(hTree, &tvi);
	if (groupName != "memory") {
		TVITEMA item = { 0 };
		item.mask = TVIF_HANDLE | TVIF_STATE;
		item.hItem = hItem;
		item.stateMask = TVIS_STATEIMAGEMASK;
		item.state = INDEXTOSTATEIMAGEMASK(1); // δѡ��
		TreeView_SetItem(hTree, &item);
	}
	// �ݹ�����ӽڵ�
	auto childIt = childMap.find(addr);
	if (childIt != childMap.end()) {
		for (duint childAddr : childIt->second) {
			insertAddressNode(groupName, hItem, childAddr, tvi);
		}
	}
}

std::string Utf8ToGbk(const std::string& utf8)
{
	int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
	std::wstring wstr(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, &wstr[0], len);
	len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
	std::string gbk(len, '\0');
	WideCharToMultiByte(CP_ACP, 0, wstr.c_str(), -1, &gbk[0], len, NULL, NULL);
	if (!gbk.empty() && gbk.back() == '\0') gbk.pop_back();
	return gbk;
}