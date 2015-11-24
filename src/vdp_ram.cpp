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
#ifdef _WIN32
#include "BaseTsd.h"
typedef INT_PTR intptr_t;
#else
#include "stdint.h"
#endif

HDC VDPRamMemDC;
HBITMAP VDPRamMemBMP;
HBITMAP VDPRamLastBMP;
BITMAPINFO MemBMPi;
COLORREF *MemBMPBits;
int VDPRamPal, VDPRamTile;
#define VDP_RAM_VCOUNT 20

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
            , tableA_begin, tableA_end - 1
            , tableB_begin, tableB_end - 1
            , tableW_begin, tableW_end - 1
            , tableS_begin, tableS_end - 1
            , scroll_begin, scroll_end - 1
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
        return true;
    }	break;

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
        int i, j, x, y, xx;
        for (i = 0; i < sizeof(VRam); ++i)
        {
            x = ((i >> 5) & 0xf) << 3;
            y = ((i >> 9) << 3);
            xx = (MemBMPi.bmiHeader.biHeight - 8 - y + 7 - ((i >> 2) & 7))*MemBMPi.bmiHeader.biWidth + (x + (i & 3 ^ 1) * 2);
            MemBMPBits[xx] = _GetPal((((unsigned char)VRam[i]) >> 4) + VDPRamPal);
            MemBMPBits[xx + 1] = _GetPal((VRam[i] & 0xf) + VDPRamPal);
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
