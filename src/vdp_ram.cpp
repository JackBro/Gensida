// A few notes about this implementation of a RAM dump window:
//
// Speed of update was one of the highest priories.
// This is because I wanted the RAM search window to be able to
// update every single value in RAM every single frame,
// without causing the emulation to run noticeably slower than normal.
//

#include "resource.h"
#include "gens.h"
#include "mem_m68k.h"
#include "mem_s68k.h"
#include "mem_sh2.h"
#include "misc.h"
#include "mem_z80.h"
#include "vdp_io.h"
#include "vdp_rend.h"
#include "save.h"
#include "ram_search.h"
#include "vdp_ram.h"
#include "g_main.h"
#include <assert.h>
#include <commctrl.h>
#include "G_dsound.h"
#include "ramwatch.h"
#include "luascript.h"
#include <list>
#include <vector>
#include "ida_debug.h"

HDC VDPRamMemDC;
HBITMAP VDPRamMemBMP;
HBITMAP VDPRamLastBMP;
BITMAPINFO MemBMPi;
COLORREF *MemBMPBits;
int VDPRamPal, VDPRamTile;
bool ShowVRAM;
#define VDP_RAM_VCOUNT 20

struct TabInfo
{
	TabInfo(const std::string& atabName, int adialogID, DLGPROC adialogProc)
		:tabName(atabName), dialogID(adialogID), dialogProc(adialogProc), hwndDialog(NULL)
	{}

	std::string tabName;
	int dialogID;
	DLGPROC dialogProc;
	HWND hwndDialog;
};
HWND activeTabWindow;
std::vector<TabInfo> tabItems;

void Redraw_VDP_View()
{
	RECT r;

	r.left = 5;
	r.top = 5 + 4 * 16 + 5;
	r.right = r.left + 8 * 16 * 2;
	r.bottom = r.top + VDP_RAM_VCOUNT * 8 * 2;
	InvalidateRect(VDPRamHWnd, &r, FALSE);

	r.left = 5;
	r.top = 5;
	r.right = r.left + 16 * 16;
	r.bottom = r.top + 4 * 16;
	InvalidateRect(VDPRamHWnd, &r, FALSE);

	r.left = 5 + 16 * 16 + 16 + 8;
	r.top = 295;
	r.right = r.left + 64;
	r.bottom = r.top + 64;
	InvalidateRect(VDPRamHWnd, &r, FALSE);
}

void Update_VDP_RAM()
{
    unsigned short scroll_begin, scroll_end, tableA_begin, tableA_end, tableB_begin, tableB_end;
    unsigned short tableW_begin, tableW_end, tableS_begin, tableS_end, screenMode, screenW, screenH;

    if (VDPRamHWnd)
    {
		InvalidateRect(VDPRamHWnd, NULL, FALSE);

        scroll_begin = (VDP_Reg.H_Scr_Adr & 0x3F) << 10;
        if ((VDP_Reg.Set3 & 0x3) == 0x3)
            scroll_end = scroll_begin + 0x400;
        else if (VDP_Reg.Set3 & 0x3)
            scroll_end = scroll_begin + 0x3F4;
        else
            scroll_end = scroll_begin + 4;

        tableA_begin = (VDP_Reg.Pat_ScrA_Adr & 0x38) << 10;
        tableB_begin = (VDP_Reg.Pat_ScrB_Adr & 0x7) << 13;
        if (VDP_Reg.Set4 & 0x81)
        {
            tableW_begin = (VDP_Reg.Pat_Win_Adr & 0x3C) << 10;
            tableW_end = (tableW_begin + 0x1000) & 0xFFFF;
            tableS_begin = (VDP_Reg.Spr_Att_Adr & 0x7E) << 9;
        }
        else
        {
            tableW_begin = (VDP_Reg.Pat_Win_Adr & 0x3E) << 10;
            tableW_end = tableW_begin + 0x800;
            tableS_begin = (VDP_Reg.Spr_Att_Adr & 0x7F) << 9;
        }
        tableS_end = tableS_begin + 640;
        unsigned char scrollsize = ((VDP_Reg.Scr_Size & 0x30) >> 2) | (VDP_Reg.Scr_Size & 0x3);
        switch (scrollsize & 0xF) {
        case 1:
        case 4:
            tableA_end = tableA_begin + 0x1000;
            tableB_end = tableB_begin + 0x1000;
            break;
        case 3:
        case 5:
        case 12:
            tableA_end = tableA_begin + 0x2000;
            tableB_end = tableB_begin + 0x2000;
            break;
        default:
            tableA_end = tableA_begin + 0x800;
            tableB_end = tableB_begin + 0x800;
            break;
        }

        screenMode = VDP_Reg.Set4 & 0x6;
        screenW = 0x20 + (VDP_Reg.Scr_Size & 0x3) * 32;
        screenH = 0x20 + ((VDP_Reg.Scr_Size >> 4) & 0x3) * 32;

        static char buff[2000];
        sprintf(buff, "PLANEA: [%04X-%04X]\n"
            "PLANEB: [%04X-%04X]\n"
            "WINDOW: [%04X-%04X]\n"
            "SPRITES TABLE: [%04X-%04X]\n"
            "HSCROLL TABLE: [%04X-%04X]\n"
            "--------\n"
            "MODE: %s\n"
            "SCREEN SIZE: %dx%d (%dx%d)\n"
            , tableA_begin, (tableA_end - 1) & 0xFFFF
            , tableB_begin, (tableB_end - 1) & 0xFFFF
            , tableW_begin, (tableW_end - 1) & 0xFFFF
            , tableS_begin, (tableS_end - 1) & 0xFFFF
            , scroll_begin, (scroll_end - 1) & 0xFFFF
            , (screenMode == 2) ? "Interlaced" : (screenMode == 6) ? "Double interlaced" : "Normal"
            , screenW, screenH, screenW * 8, screenH * 8);
        SetWindowText(GetDlgItem(VDPRamHWnd, IDC_VDP_ADDRESSES), buff);
    }
}

BOOL SetClipboardText(LPCTSTR pszText)
{
    BOOL ok = FALSE;
    if (OpenClipboard(VDPRamHWnd))
    {
        EmptyClipboard();
        HGLOBAL hMem = GlobalAlloc(GMEM_SHARE | GMEM_MOVEABLE,
            (lstrlen(pszText) + 1)*sizeof(pszText[0]));
        LPTSTR ptxt = (LPTSTR)GlobalLock(hMem);
        lstrcpy(ptxt, pszText);
        GlobalUnlock(hMem);
        ok = (BOOL)SetClipboardData(CF_TEXT, hMem);

        CloseClipboard();
    }
    return ok;
}

extern int Show_Genesis_Screen(HWND hWnd);

//----------------------------------------------------------------------------------------
//Window procedure helper functions
//----------------------------------------------------------------------------------------
void WndProcDialogImplementSaveFieldWhenLostFocus(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
		//Make sure no textbox is selected on startup, and remove focus from textboxes when
		//the user clicks an unused area of the window.
	case WM_LBUTTONDOWN:
	case WM_SHOWWINDOW:
		SendMessage(hwnd, WM_COMMAND, MAKEWPARAM(0, EN_SETFOCUS), NULL);
		SetFocus(NULL);
		break;
	}
}

//----------------------------------------------------------------------------------------
INT_PTR msgModeRegistersWM_PAINT(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	//Mode registers
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_VSI, (VDP_Reg.Set1 & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_HSI, (VDP_Reg.Set1 & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_LCB, (VDP_Reg.Set1 & mask(5)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_IE1, (VDP_Reg.Set1 & mask(4)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_SS, (VDP_Reg.Set1 & mask(3)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_PS, (VDP_Reg.Set1 & mask(2)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_M2, (VDP_Reg.Set1 & mask(1)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_ES, (VDP_Reg.Set1 & mask(0)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_EVRAM, (VDP_Reg.Set2 & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_DISP, (VDP_Reg.Set2 & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_IE0, (VDP_Reg.Set2 & mask(5)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_M1, (VDP_Reg.Set2 & mask(4)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_M3, (VDP_Reg.Set2 & mask(3)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_M5, (VDP_Reg.Set2 & mask(2)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_SZ, (VDP_Reg.Set2 & mask(1)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_MAG, (VDP_Reg.Set2 & mask(0)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_0B7, (VDP_Reg.Set3 & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_0B6, (VDP_Reg.Set3 & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_0B5, (VDP_Reg.Set3 & mask(5)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_0B4, (VDP_Reg.Set3 & mask(4)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_IE2, (VDP_Reg.Set3 & mask(3)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_VSCR, (VDP_Reg.Set3 & mask(2)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_HSCR, (VDP_Reg.Set3 & mask(1)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_LSCR, (VDP_Reg.Set3 & mask(0)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_RS0, (VDP_Reg.Set4 & mask(7)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_U1, (VDP_Reg.Set4 & mask(6)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_U2, (VDP_Reg.Set4 & mask(5)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_U3, (VDP_Reg.Set4 & mask(4)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_STE, (VDP_Reg.Set4 & mask(3)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_LSM1, (VDP_Reg.Set4 & mask(2)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_LSM0, (VDP_Reg.Set4 & mask(1)) ? BST_CHECKED : BST_UNCHECKED);
	CheckDlgButton(hwnd, IDC_VDP_REGISTERS_RS1, (VDP_Reg.Set4 & mask(0)) ? BST_CHECKED : BST_UNCHECKED);

	return TRUE;
}

//----------------------------------------------------------------------------------------
INT_PTR msgModeRegistersWM_COMMAND(HWND hwnd, WPARAM wparam, LPARAM lparam)
{
	if (HIWORD(wparam) == BN_CLICKED)
	{
		unsigned int controlID = LOWORD(wparam);
		int chk = (IsDlgButtonChecked(hwnd, controlID) == BST_CHECKED) ? 1 : 0;
		switch (controlID)
		{
		case IDC_VDP_REGISTERS_VSI:
			VDP_Reg.Set1 |= (chk << 7);
			break;
		case IDC_VDP_REGISTERS_HSI:
			VDP_Reg.Set1 |= (chk << 6);
			break;
		case IDC_VDP_REGISTERS_LCB:
			VDP_Reg.Set1 |= (chk << 5);
			break;
		case IDC_VDP_REGISTERS_IE1:
			VDP_Reg.Set1 |= (chk << 4);
			break;
		case IDC_VDP_REGISTERS_SS:
			VDP_Reg.Set1 |= (chk << 3);
			break;
		case IDC_VDP_REGISTERS_PS:
			VDP_Reg.Set1 |= (chk << 2);
			break;
		case IDC_VDP_REGISTERS_M2:
			VDP_Reg.Set1 |= (chk << 1);
			break;
		case IDC_VDP_REGISTERS_ES:
			VDP_Reg.Set1 |= (chk << 0);
			break;
		case IDC_VDP_REGISTERS_EVRAM:
			VDP_Reg.Set2 |= (chk << 7);
			break;
		case IDC_VDP_REGISTERS_DISP:
			VDP_Reg.Set2 |= (chk << 6);
			break;
		case IDC_VDP_REGISTERS_IE0:
			VDP_Reg.Set2 |= (chk << 5);
			break;
		case IDC_VDP_REGISTERS_M1:
			VDP_Reg.Set2 |= (chk << 4);
			break;
		case IDC_VDP_REGISTERS_M3:
			VDP_Reg.Set2 |= (chk << 3);
			break;
		case IDC_VDP_REGISTERS_M5:
			VDP_Reg.Set2 |= (chk << 2);
			break;
		case IDC_VDP_REGISTERS_SZ:
			VDP_Reg.Set2 |= (chk << 1);
			break;
		case IDC_VDP_REGISTERS_MAG:
			VDP_Reg.Set2 |= (chk << 0);
			break;
		case IDC_VDP_REGISTERS_0B7:
			VDP_Reg.Set3 |= (chk << 7);
			break;
		case IDC_VDP_REGISTERS_0B6:
			VDP_Reg.Set3 |= (chk << 6);
			break;
		case IDC_VDP_REGISTERS_0B5:
			VDP_Reg.Set3 |= (chk << 5);
			break;
		case IDC_VDP_REGISTERS_0B4:
			VDP_Reg.Set3 |= (chk << 4);
			break;
		case IDC_VDP_REGISTERS_IE2:
			VDP_Reg.Set3 |= (chk << 3);
			break;
		case IDC_VDP_REGISTERS_VSCR:
			VDP_Reg.Set3 |= (chk << 2);
			break;
		case IDC_VDP_REGISTERS_HSCR:
			VDP_Reg.Set3 |= (chk << 1);
			break;
		case IDC_VDP_REGISTERS_LSCR:
			VDP_Reg.Set3 |= (chk << 0);
			break;
		case IDC_VDP_REGISTERS_RS0:
			VDP_Reg.Set4 |= (chk << 7);
			break;
		case IDC_VDP_REGISTERS_U1:
			VDP_Reg.Set4 |= (chk << 6);
			break;
		case IDC_VDP_REGISTERS_U2:
			VDP_Reg.Set4 |= (chk << 5);
			break;
		case IDC_VDP_REGISTERS_U3:
			VDP_Reg.Set4 |= (chk << 4);
			break;
		case IDC_VDP_REGISTERS_STE:
			VDP_Reg.Set4 |= (chk << 3);
			break;
		case IDC_VDP_REGISTERS_LSM1:
			VDP_Reg.Set4 |= (chk << 2);
			break;
		case IDC_VDP_REGISTERS_LSM0:
			VDP_Reg.Set4 |= (chk << 1);
			break;
		case IDC_VDP_REGISTERS_RS1:
			VDP_Reg.Set4 |= (chk << 0);
			break;
		}
	}

	return TRUE;
}

//----------------------------------------------------------------------------------------
INT_PTR WndProcModeRegisters(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	WndProcDialogImplementSaveFieldWhenLostFocus(hwnd, msg, wparam, lparam);
	switch (msg)
	{
	case WM_COMMAND:
		return msgModeRegistersWM_COMMAND(hwnd, wparam, lparam);
	}
	return FALSE;
}

//----------------------------------------------------------------------------------------
//Mode registers dialog window procedure
//----------------------------------------------------------------------------------------
INT_PTR CALLBACK WndProcModeRegistersStatic(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	//Obtain the object pointer
	int state = (int)GetWindowLongPtr(hwnd, GWLP_USERDATA);

	//Process the message
	switch (msg)
	{
	case WM_INITDIALOG:
		//Set the object pointer
		state = (int)lparam;
		SetWindowLongPtr(hwnd, GWLP_USERDATA, (int)(state));

		//Pass this message on to the member window procedure function
		if (state != 0)
		{
			return WndProcModeRegisters(hwnd, msg, wparam, lparam);
		}
		break;
	case WM_DESTROY:
		if (state != 0)
		{
			//Pass this message on to the member window procedure function
			INT_PTR result = WndProcModeRegisters(hwnd, msg, wparam, lparam);

			//Discard the object pointer
			SetWindowLongPtr(hwnd, GWLP_USERDATA, (int)0);

			//Return the result from processing the message
			return result;
		}
		break;
	}

	//Pass this message on to the member window procedure function
	INT_PTR result = FALSE;
	if (state != 0)
	{
		result = WndProcModeRegisters(hwnd, msg, wparam, lparam);
	}
	return result;
}

LRESULT CALLBACK VDPRamProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    RECT r;
    RECT r2;
    int dx1, dy1, dx2, dy2;
    static int watchIndex = 0;

    switch (uMsg)
    {
    case WM_INITDIALOG: {
		HDC hdc = GetDC(hDlg);
        VDPRamMemDC = CreateCompatibleDC(hdc);
        MemBMPi.bmiHeader.biSize = sizeof(MemBMPi.bmiHeader);
        MemBMPi.bmiHeader.biWidth = 8 * 16;
        MemBMPi.bmiHeader.biHeight = (sizeof(VRam) / 32 / 16) * 8 + 8;
        MemBMPi.bmiHeader.biBitCount = 32;
        MemBMPi.bmiHeader.biPlanes = 1;
        MemBMPi.bmiHeader.biCompression = BI_RGB;
        MemBMPi.bmiHeader.biClrImportant = 0;
        MemBMPi.bmiHeader.biClrUsed = 0;
        MemBMPi.bmiHeader.biSizeImage = (MemBMPi.bmiHeader.biWidth*MemBMPi.bmiHeader.biHeight) * 4;
        MemBMPi.bmiHeader.biXPelsPerMeter = 0;
        MemBMPi.bmiHeader.biYPelsPerMeter = 0;
        //VDPRamMemBMP=CreateCompatibleBitmap(hdc,8*16,(sizeof(VRam)/32/16)*8+8);
        VDPRamMemBMP = CreateDIBSection(VDPRamMemDC, &MemBMPi, DIB_RGB_COLORS, (void **)&MemBMPBits, NULL, NULL);
        VDPRamLastBMP = (HBITMAP)SelectObject(VDPRamMemDC, VDPRamMemBMP);
        VDPRamPal = VDPRamTile = 0;
        //memset(&MemBMPi,0,sizeof(MemBMPi));
        //MemBMPi.bmiHeader.biSize=sizeof(MemBMPi.bmiHeader);
        //GetDIBits(VDPRamMemDC,VDPRamMemBMP,0,0,NULL,&MemBMPi,DIB_RGB_COLORS);
        //MemBMPBits = new COLORREF[MemBMPi.bmiHeader.biSizeImage/4+1];

		ShowVRAM = true;
		CheckRadioButton(hDlg, IDC_SHOW_VRAM, IDC_SHOW_M68K_RAM, IDC_SHOW_VRAM);

        VDPRamHWnd = hDlg;

        if (Full_Screen)
        {
            while (ShowCursor(false) >= 0);
            while (ShowCursor(true) < 0);
        }

        GetWindowRect(HWnd, &r);
        dx1 = (r.right - r.left) / 2;
        dy1 = (r.bottom - r.top) / 2;

        GetWindowRect(hDlg, &r2);
        dx2 = (r2.right - r2.left) / 2;
        dy2 = (r2.bottom - r2.top) / 2;

        // push it away from the main window if we can
        const int width = (r.right - r.left);
        const int width2 = (r2.right - r2.left);
        if (r.left + width2 + width < GetSystemMetrics(SM_CXSCREEN))
        {
            r.right += width;
            r.left += width;
        }
        else if ((int)r.left - (int)width2 > 0)
        {
            r.right -= width2;
            r.left -= width2;
        }

        SetWindowPos(hDlg, NULL, r.left, r.top, NULL, NULL, SWP_NOSIZE | SWP_NOZORDER | SWP_SHOWWINDOW);
        SetWindowPos(GetDlgItem(hDlg, IDC_SCROLLBAR1), NULL, 5 + 16 * 16, 5 + 16 * 4 + 5, 16, 16 * VDP_RAM_VCOUNT, SWP_NOZORDER | SWP_SHOWWINDOW);
        SetScrollRange(GetDlgItem(hDlg, IDC_SCROLLBAR1), SB_CTL, 0, sizeof(VRam) / 0x200 - VDP_RAM_VCOUNT, TRUE);

		// Exodus VDP Regs window init

		//Add our set of tab items to the list of tabs
		tabItems.push_back(TabInfo("Mode Registers", IDD_VDP_REGISTERS_MODEREGISTERS, WndProcModeRegistersStatic));
		//tabItems.push_back(TabInfo("Other Registers", IDD_VDP_REGISTERS_OTHERREGISTERS, WndProcOtherRegistersStatic));

		//Insert our tabs into the tab control
		for (unsigned int i = 0; i < (unsigned int)tabItems.size(); ++i)
		{
			TCITEM tabItem;
			tabItem.mask = TCIF_TEXT;
			tabItem.pszText = (LPSTR)tabItems[i].tabName.c_str();
			SendMessage(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), TCM_INSERTITEM, i, (LPARAM)&tabItem);
		}

		//Create each window associated with each tab, and calculate the required size of the
		//client area of the tab control to fit the largest tab window.
		int requiredTabClientWidth = 0;
		int requiredTabClientHeight = 0;
		for (unsigned int i = 0; i < (unsigned int)tabItems.size(); ++i)
		{
			//Create the dialog window for this tab
			DLGPROC dialogWindowProc = tabItems[i].dialogProc;
			LPCSTR dialogTemplateName = MAKEINTRESOURCE(tabItems[i].dialogID);
			tabItems[i].hwndDialog = CreateDialogParam(GetHInstance(), dialogTemplateName, GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), dialogWindowProc, (LPARAM)1);

			//Calculate the required size of the window for this tab in pixel units
			RECT rect;
			GetClientRect(tabItems[i].hwndDialog, &rect);
			int tabWidth = rect.right;
			int tabHeight = rect.bottom;

			//Increase the required size of the client area for the tab control to accommodate
			//the contents of this tab, if required.
			requiredTabClientWidth = (tabWidth > requiredTabClientWidth) ? tabWidth : requiredTabClientWidth;
			requiredTabClientHeight = (tabHeight > requiredTabClientHeight) ? tabHeight : requiredTabClientHeight;
		}

		//Save the original size of the tab control
		RECT tabControlOriginalRect;
		GetClientRect(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), &tabControlOriginalRect);
		int tabControlOriginalSizeX = tabControlOriginalRect.right - tabControlOriginalRect.left;
		int tabControlOriginalSizeY = tabControlOriginalRect.bottom - tabControlOriginalRect.top;

		//Calculate the exact required pixel size of the tab control to fully display the
		//content in each tab
		RECT tabControlRect;
		tabControlRect.left = 0;
		tabControlRect.top = 0;
		tabControlRect.right = requiredTabClientWidth;
		tabControlRect.bottom = requiredTabClientHeight;
		SendMessage(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), TCM_ADJUSTRECT, (WPARAM)TRUE, (LPARAM)&tabControlRect);
		int tabControlRequiredSizeX = tabControlRect.right - tabControlRect.left;
		int tabControlRequiredSizeY = tabControlRect.bottom - tabControlRect.top;

		//Resize the tab control
		SetWindowPos(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), NULL, 0, 0, tabControlRequiredSizeX, tabControlRequiredSizeY, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE);

		//Calculate the required pixel size and position of each tab window
		RECT currentTabControlRect;
		GetWindowRect(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), &currentTabControlRect);
		SendMessage(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), TCM_ADJUSTRECT, (WPARAM)FALSE, (LPARAM)&currentTabControlRect);
		POINT tabContentPoint;
		tabContentPoint.x = currentTabControlRect.left;
		tabContentPoint.y = currentTabControlRect.top;
		ScreenToClient(GetDlgItem(hDlg, IDC_VDP_REGISTERS_TABCONTROL), &tabContentPoint);
		int tabRequiredPosX = tabContentPoint.x;
		int tabRequiredPosY = tabContentPoint.y;
		int tabRequiredSizeX = currentTabControlRect.right - currentTabControlRect.left;
		int tabRequiredSizeY = currentTabControlRect.bottom - currentTabControlRect.top;

		//Position and size each tab window
		for (unsigned int i = 0; i < (unsigned int)tabItems.size(); ++i)
		{
			SetWindowPos(tabItems[i].hwndDialog, NULL, tabRequiredPosX, tabRequiredPosY, tabRequiredSizeX, tabRequiredSizeY, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
		}

		//Calculate the current size of the owning window
		RECT mainDialogRect;
		GetWindowRect(hDlg, &mainDialogRect);
		int currentMainDialogWidth = mainDialogRect.right - mainDialogRect.left;
		int currentMainDialogHeight = mainDialogRect.bottom - mainDialogRect.top;

		//Resize the owning window to the required size
		int newMainDialogWidth = currentMainDialogWidth + (tabControlRequiredSizeX - tabControlOriginalSizeX);
		int newMainDialogHeight = currentMainDialogHeight + (tabControlRequiredSizeY - tabControlOriginalSizeY);
		SetWindowPos(hDlg, NULL, 0, 0, newMainDialogWidth, newMainDialogHeight, SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOMOVE);

		//Explicitly select and show the first tab
		activeTabWindow = tabItems[0].hwndDialog;
		ShowWindow(activeTabWindow, SW_SHOWNA);

		// Exodus VDP Regs window init
        return true;
    }	break;

	case WM_NOTIFY:
	{
		NMHDR* nmhdr = (NMHDR*)lParam;
		if (nmhdr->idFrom == IDC_VDP_REGISTERS_TABCONTROL)
		{
			if ((nmhdr->code == TCN_SELCHANGE))
			{
				//Begin a session for processing this batch of window visibility changes.
				//Processing all the changes in a single operation in this manner gives the
				//best performance and appearance.
				HDWP deferWindowPosSession = BeginDeferWindowPos(2);

				//If another tab window is currently visible, hide it now.
				if (activeTabWindow != NULL)
				{
					DeferWindowPos(deferWindowPosSession, activeTabWindow, NULL, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_HIDEWINDOW);
					activeTabWindow = NULL;
				}

				//Show the window for the new selected tab on the tab control
				int currentlySelectedTab = (int)SendMessage(nmhdr->hwndFrom, TCM_GETCURSEL, 0, 0);
				if ((currentlySelectedTab < 0) || (currentlySelectedTab >= (int)tabItems.size()))
				{
					currentlySelectedTab = 0;
				}
				activeTabWindow = tabItems[currentlySelectedTab].hwndDialog;
				DeferWindowPos(deferWindowPosSession, activeTabWindow, NULL, 0, 0, 0, 0, SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_NOSIZE | SWP_NOMOVE | SWP_SHOWWINDOW);

				//Process all the window size and position changes involved in this update
				EndDeferWindowPos(deferWindowPosSession);
			}
		}
		return TRUE;
	}

    case WM_COMMAND:
    {
        switch (wParam)
        {
        case IDC_BUTTON1:
        {
            char fname[2048];
            strcpy(fname, "pal.bin");
            if (Change_File_S(fname, ".", "Save Dump Pal As...", "All Files\0*.*\0\0", "*.*", hDlg))
            {
                FILE *out = fopen(fname, "wb+");
                int i;
                for (i = 0; i < sizeof(CRam); ++i)
                {
                    fname[i & 2047] = ((char*)&CRam)[i ^ 1];
                    if ((i & 2047) == 2047)
                        fwrite(fname, 1, sizeof(fname), out);
                }
                fwrite(fname, 1, i & 2047, out);
                fclose(out);
            }
        }
        break;
        case IDC_BUTTON2:
        {
            char fname[2048];
            strcpy(fname, "pal.bin");
            if (Change_File_L(fname, ".", "Load Dump Pal As...", "All Files\0*.*\0\0", "*.*", hDlg))
            {
                FILE *in = fopen(fname, "rb");
                int i;
                for (i = 0; i < sizeof(CRam); ++i)
                {
                    if (!(i & 2047))
                        fread(fname, 1, sizeof(fname), in);
                    ((char*)&CRam)[i ^ 1] = fname[i & 2047];
                }
                fclose(in);
                if (Game)
                {
                    CRam_Flag = 1;
                    Show_Genesis_Screen(HWnd);
                }
            }
        }
        break;
#define swappp(x) ((((x)>>16)&0xff)|(((x)&0xff)<<16)|((x)&0xff00))
        case IDC_BUTTON3:
        {
            char fname[2048];
            strcpy(fname, "pal.pal");
            if (Change_File_S(fname, ".", "Save YY-CHR Pal As...", "All Files\0*.*\0\0", "*.*", hDlg))
            {
                FILE *out = fopen(fname, "wb+");
                int i;
                for (i = 0; i < 16 * 4; ++i)
                {
                    *((DWORD*)fname) = swappp(Palette32[CRam[i] | 0x4000]);
                    fwrite(fname, 1, 3, out);
                }
                *((DWORD*)fname) = 0;
                for (; i < 256; ++i)
                    fwrite(fname, 1, 3, out);
                fclose(out);
            }
        }
        break;
        case IDC_BUTTON4:
        {
            char buff[4 * 16 * 5];
            int i, j, o = 0;
            for (j = 0; j < 4; ++j)
            {
                for (i = 0; i < 16; ++i)
                {
                    sprintf(buff + o, "%03X ", (int)CRam[(j << 4) + i]);
                    o += 4;
                }
                sprintf(buff + o, "\r\n");
                o += 2;
            }
            SetClipboardText(buff);
        }
        break;

        case IDC_BUTTON5:
        {
            char buff[4 * 16 * 8];
            int i, j, o = 0;
            for (j = 0; j < 4; ++j)
            {
                for (i = 0; i < 16; ++i)
                {
                    sprintf(buff + o, "%06X ", Palette32[CRam[(j << 4) + i] | 0x4000] & 0xFFFFFF);
                    o += 7;
                }
                sprintf(buff + o, "\r\n");
                o += 2;
            }
            SetClipboardText(buff);
        }
        break;
        case IDC_BUTTON6:
        {
            char buff[4 * 16 * 8];
            int i, j, o = 0;
            for (j = 0; j < 4; ++j)
            {
                for (i = 0; i < 16; ++i)
                {
                    sprintf(buff + o, "%06X ", swappp(Palette32[CRam[(j << 4) + i] | 0x4000]));
                    o += 7;
                }
                sprintf(buff + o, "\r\n");
                o += 2;
            }
            SetClipboardText(buff);
        }
        break;
        case IDC_BUTTON7:
        {
            char buff[4 * 16 * 12];
            int i, j, o = 0;
            for (j = 0; j < 4; ++j)
            {
                for (i = 0; i < 16; ++i)
                {
                    sprintf(buff + o, "dc.w $%03X\r\n", (int)CRam[(j << 4) + i]);
                    o += 11;
                }
                sprintf(buff + o, "\r\n");
                o += 2;
            }
            SetClipboardText(buff);
        }
        break;
        case IDC_BUTTON8:
        {
            char fname[2048];
            strcpy(fname, "vram.bin");
            if (Change_File_S(fname, ".", "Save Dump VRAM As...", "All Files\0*.*\0\0", "*.*", hDlg))
            {
                FILE *out = fopen(fname, "wb+");
                int i;
                for (i = 0; i < sizeof(VRam); ++i)
                {
                    fname[i & 2047] = ((char*)&VRam)[i ^ 1];
                    if ((i & 2047) == 2047)
                        fwrite(fname, 1, sizeof(fname), out);
                }
                fwrite(fname, 1, i & 2047, out);
                fclose(out);
            }
        }
        break;
        case IDC_BUTTON9:
        {
            char fname[2048];
            strcpy(fname, "vram.bin");
            if (Change_File_L(fname, ".", "Load Dump VRAM As...", "All Files\0*.*\0\0", "*.*", hDlg))
            {
                FILE *in = fopen(fname, "rb");
                int i;
                for (i = 0; i < sizeof(VRam); ++i)
                {
                    if (!(i & 2047))
                        fread(fname, 1, sizeof(fname), in);
                    ((char*)&VRam)[i ^ 1] = fname[i & 2047];
                }
                fclose(in);
                if (Genesis_Started || _32X_Started || SegaCD_Started)
                    Show_Genesis_Screen(HWnd);
            }
        }
        break;
		case IDC_SHOW_VRAM:
		{
			ShowVRAM = true;
			InvalidateRect(hDlg, NULL, FALSE);
		}
		break;
		case IDC_SHOW_M68K_RAM:
		{
			ShowVRAM = false;
			InvalidateRect(hDlg, NULL, FALSE);
		}
		break;
        }
    }	break;
#undef swappp

    case WM_VSCROLL:
    {
        int CurPos = GetScrollPos(GetDlgItem(hDlg, IDC_SCROLLBAR1), SB_CTL);
        int nSBCode = LOWORD(wParam);
        int nPos = HIWORD(wParam);
        switch (nSBCode)
        {
        case SB_LEFT:      // Scroll to far left.
            CurPos = 0;
            break;

        case SB_RIGHT:      // Scroll to far right.
            CurPos = sizeof(VRam) / 0x200 - VDP_RAM_VCOUNT;
            break;

        case SB_ENDSCROLL:   // End scroll.
            break;

        case SB_LINELEFT:      // Scroll left.
            if (CurPos > 0)
                CurPos--;
            break;

        case SB_LINERIGHT:   // Scroll right.
            if (CurPos < sizeof(VRam) / 0x200 - VDP_RAM_VCOUNT)
                CurPos++;
            break;

        case SB_PAGELEFT:    // Scroll one page left.
            CurPos -= VDP_RAM_VCOUNT;
            if (CurPos < 0)
                CurPos = 0;
            break;

        case SB_PAGERIGHT:      // Scroll one page righ
            CurPos += VDP_RAM_VCOUNT;
            if (CurPos >= sizeof(VRam) / 0x200 - VDP_RAM_VCOUNT)
                CurPos = sizeof(VRam) / 0x200 - VDP_RAM_VCOUNT - 1;
            break;

        case SB_THUMBTRACK:   // Drag scroll box to specified position. nPos is the
        case SB_THUMBPOSITION: // Scroll to absolute position. nPos is the position
        {
            SCROLLINFO si;
            ZeroMemory(&si, sizeof(si));
            si.cbSize = sizeof(si);
            si.fMask = SIF_TRACKPOS;

            // Call GetScrollInfo to get current tracking
            //    position in si.nTrackPos

            if (!GetScrollInfo(GetDlgItem(hDlg, IDC_SCROLLBAR1), SB_CTL, &si))
                return 1; // GetScrollInfo failed
            CurPos = si.nTrackPos;
        }	break;
        }
        SetScrollPos(GetDlgItem(hDlg, IDC_SCROLLBAR1), SB_CTL, CurPos, TRUE);
        Update_VDP_RAM();
    }	break;
#define _GetPal(x) Palette32[CRam[x]|0x4000] //((MD_Palette32[x]>>16)|((MD_Palette32[x]&0xff)<<16)|(MD_Palette32[x]&0xff00))
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        BeginPaint(hDlg, &ps);
        SelectObject(VDPRamMemDC, VDPRamLastBMP);
		unsigned char *ptr = (unsigned char*)(ShowVRAM ? VRam : Ram_68k);
        int i, j, x, y, xx;
        for (i = 0; i < sizeof(VRam); ++i)
        {
            x = ((i >> 5) & 0xf) << 3;
            y = ((i >> 9) << 3);
            xx = (MemBMPi.bmiHeader.biHeight - 8 - y + 7 - ((i >> 2) & 7))*MemBMPi.bmiHeader.biWidth + (x + (i & 3 ^ 1) * 2);
            MemBMPBits[xx] = _GetPal(((ptr[i]) >> 4) + VDPRamPal);
            MemBMPBits[xx + 1] = _GetPal((ptr[i] & 0xf) + VDPRamPal);
        }
        for (j = 0; j < 4; ++j)
            for (i = 0; i < 16; ++i)
                MemBMPBits[(7 - j)*MemBMPi.bmiHeader.biWidth + i] = _GetPal(i + j * 16);
        SelectObject(VDPRamMemDC, VDPRamMemBMP);
#undef _GetPal
        int scroll = GetScrollPos(GetDlgItem(hDlg, IDC_SCROLLBAR1), SB_CTL);
        StretchBlt(ps.hdc, 5, 5 + 4 * 16 + 5, 8 * 16 * 2, VDP_RAM_VCOUNT * 8 * 2, VDPRamMemDC, 0, scroll * 8, 8 * 16, VDP_RAM_VCOUNT * 8, SRCCOPY); // VRAM
        StretchBlt(ps.hdc, 5, 5, 16 * 16, 4 * 16, VDPRamMemDC, 0, MemBMPi.bmiHeader.biHeight - 8, 16, 4, SRCCOPY); // CRAM
		StretchBlt(ps.hdc, 5 + 16 * 16 + 16 + 8, 295, 64, 64, VDPRamMemDC, (VDPRamTile % 16) << 3, (VDPRamTile >> 4) << 3, 8, 8, SRCCOPY); // Selected Tile

        r.left = 5;
		r.right = r.left + 16 * 16;
        r.top = 5 + VDPRamPal;
		r.bottom = r.top + 16;
        DrawFocusRect(ps.hdc, &r);

		r.left = 5 + ((VDPRamTile % 16) << 4);
		r.right = r.left + 16;
		int row = (VDPRamTile >> 4) - scroll;
		r.top = 5 + 4 * 16 + 5 + (row << 4);
		r.bottom = r.top + 16;

		if (row >= 0 && row < VDP_RAM_VCOUNT)
			DrawFocusRect(ps.hdc, &r);

        EndPaint(hDlg, &ps);

		msgModeRegistersWM_PAINT(activeTabWindow, wParam, lParam);
        return true;
    }	break;

    case WM_LBUTTONDOWN:
    {
        int x = LOWORD(lParam) - 5;
        int y = HIWORD(lParam) - 5;
        if (x >= 0 && x < 16 * 16 &&
            y >= 0 && y <= 16 * 4)
        {
            VDPRamPal = y & 0x30;
            Update_VDP_RAM();
        }
		else
		{
			x = LOWORD(lParam) - 5;
			y = HIWORD(lParam) - (5 + 4 * 16 + 5);

			if (x >= 0 &&
				x < 16 * 16 &&
				y >= 0 &&
				y < VDP_RAM_VCOUNT * 8 * 2)
			{
				int scroll = GetScrollPos(GetDlgItem(hDlg, IDC_SCROLLBAR1), SB_CTL);

				char buff[30];
				int offset = (((y >> 4) + scroll) << 9) + ((x >> 4) << 5);
				int id = VDPRamTile = (((y >> 4) + scroll) << 4) + (x >> 4);
				sprintf(buff, "Offset: %04X\r\nId: %03X", offset, id);
				SetDlgItemText(hDlg, IDC_TILE_INFO, buff);

				Update_VDP_RAM();
			}
		}
    }	break;

    case WM_CLOSE:
		if (activeTabWindow != NULL)
		{
			DestroyWindow(activeTabWindow);
			activeTabWindow = NULL;
		}
		
		if (Full_Screen)
        {
            while (ShowCursor(true) < 0);
            while (ShowCursor(false) >= 0);
        }
        SelectObject(VDPRamMemDC, VDPRamLastBMP);
        DeleteObject(VDPRamMemBMP);
        DeleteObject(VDPRamMemDC);
        DialogsOpen--;
        VDPRamHWnd = NULL;
        EndDialog(hDlg, true);
        return true;
    }

    return false;
}
