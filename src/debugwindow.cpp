#include "resource.h"
#include "gens.h"
#include "save.h"
#include "g_main.h"
#include "ramwatch.h"
#include "debugwindow.h"
#include "G_ddraw.h"
#include <vector>

#include <dbg.hpp>
#include "ida_debmod.h"
#include "ida_debug.h"
extern eventlist_t g_events;
bool handled_ida_event;

void Handle_Gens_Messages();
LRESULT CALLBACK EditBreakProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
extern int Gens_Running;
extern "C" int Clear_Sound_Buffer(void);

DebugWindow::DebugWindow()
{
    DummyHWnd = HWnd = NULL;
    DLGPROC DebugProc = NULL;

    StepInto = false;
    StepOver = -1;
}

DebugWindow::~DebugWindow()
{
}

void DebugWindow::TracePC(int pc) {}
void DebugWindow::TraceRead(uint32 start, uint32 stop) {}
void DebugWindow::TraceWrite(uint32 start, uint32 stop) {}
void DebugWindow::DoStepOver() {}

void DebugWindow::Breakpoint(int pc)
{	
	if (!handled_ida_event)
	{
		debug_event_t ev;
		ev.pid = 1;
		ev.tid = 1;
		ev.ea = pc;
		ev.handled = true;
		ev.eid = PROCESS_SUSPEND;
		g_events.enqueue(ev, IN_BACK);
	}

	Update_RAM_Watch();
    Clear_Sound_Buffer();

    if (!DummyHWnd)
    {
        DummyHWnd = (HWND)1;
        MSG msg = { 0 };
        for (; Gens_Running&&DummyHWnd;)
        {
            Handle_Gens_Messages();
        }
        //DebugDummyHWnd=(HWND)0;
    }
}

void DebugWindow::SetWhyBreak(LPCSTR lpString)
{
    msg("%s\n", lpString);
}

bool DebugWindow::BreakPC(int pc)
{
    std::vector<::Breakpoint>::iterator i, n;
    i = Breakpoints.begin();
    n = Breakpoints.end();
    bool r = false;
    for (; i != n; ++i)
    {
		if (!(i->enabled) || !(i->type & BRK_PC))
            continue;
        if (pc >= (int)i->start &&
            pc <= (int)i->end)
        {
			if (i->type & BRK_FORBID)
                return false;
            r = true;
        }
    }
    return r;
}

bool DebugWindow::BreakRead(int pc, uint32 star, uint32 stop, bool is_vdp)
{
    std::vector<::Breakpoint>::iterator i, n;
    i = Breakpoints.begin();
    n = Breakpoints.end();
    bool r = false;
    for (; i != n; ++i)
    {
		if ((i->enabled) && (i->type & BRK_FORBID))
        {
			if ((i->type & BRK_PC) &&
                pc >= (int)i->start &&
                pc <= (int)i->end)
                return false;
			if ((i->type & BRK_READ) &&
                star >= i->start &&
                stop <= i->end)
                return false;
        }
		if (!is_vdp && (i->type & BRK_VDP))
			continue;
		if (!(i->enabled) || !(i->type & BRK_READ))
            continue;
        if (stop >= i->start &&
            star <= i->end)
            r = true;
    }
    return r;
}

bool DebugWindow::BreakWrite(int pc, uint32 star, uint32 stop, bool is_vdp)
{
    std::vector<::Breakpoint>::iterator i, n;
    i = Breakpoints.begin();
    n = Breakpoints.end();
    bool r = false;
    for (; i != n; ++i)
    {
		if ((i->enabled) && (i->type & BRK_FORBID))
        {
			if ((i->type & BRK_PC) &&
                pc >= (int)i->start &&
                pc <= (int)i->end)
                return false;
			if ((i->type & BRK_WRITE) &&
                star >= i->start &&
                stop <= i->end)
                return false;
        }
		if (!(i->enabled) || !(i->type & BRK_WRITE))
            continue;
        if (stop >= i->start &&
            star <= i->end)
            r = true;
    }
    return r;
}
