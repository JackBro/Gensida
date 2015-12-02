#include "resource.h"
#include "gens.h"
#include "save.h"
#include "g_main.h"
#include "ramwatch.h"
#include "debugwindow.h"
#include "G_ddraw.h"
#include "Star_68k.h"
#include "vdp_io.h"
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
	DebugStop = false;
	HWnd = NULL;
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

	Show_Genesis_Screen(HWnd);
	Update_RAM_Watch();
    Clear_Sound_Buffer();

	if (!DebugStop)
    {
		DebugStop = true;
        MSG msg = { 0 };
		for (; Gens_Running && DebugStop;)
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
	auto range = Breakpoints.equal_range(BP_PC);
	for (auto i = range.first; i != range.second; ++i)
	{
		if (!(i->second.enabled)) continue;
		if (pc <= (int)(i->second.end) && pc >= (int)(i->second.start))
		{
			return !(i->second.is_forbid);
		}
	}
	return false;
}

extern uint32 break_regs[];

bool DebugWindow::BreakRegValue(int pc, uint8 reg_idx, uint32 value, bool is_vdp)
{
	int start_idx = (!is_vdp ? 0 : 16);
	int len = (!is_vdp ? 16 : 24);

	for (uint8 i = start_idx; i < (start_idx + len); ++i)
	{
		if ((reg_idx == i) && (value == break_regs[i]))
			return true;
	}

	return false;
}

bool DebugWindow::BreakRead(int pc, uint32 start, uint32 stop, bool is_vdp)
{
	bool brk = false;

	auto range = Breakpoints.equal_range(BP_READ);
	for (auto i = range.first; i != range.second; ++i)
	{
		if (!(i->second.enabled) || (i->second.is_vdp != is_vdp)) continue;
		if (start <= i->second.end && stop >= i->second.start)
		{
			brk = !(i->second.is_forbid);
			break;
		}
	}

	if (!brk) return false;
	
	range = Breakpoints.equal_range(BP_PC);
	for (auto i = range.first; i != range.second; ++i)
	{
		if (i->second.enabled && i->second.is_forbid)
		{
			if (pc <= (int)(i->second.end) && pc >= (int)(i->second.start))
				return false;
		}
	}

	return true;
}

bool DebugWindow::BreakWrite(int pc, uint32 start, uint32 stop, bool is_vdp)
{
	bool brk = false;

	auto range = Breakpoints.equal_range(BP_WRITE);
	for (auto i = range.first; i != range.second; ++i)
	{
		if (!(i->second.enabled) || (i->second.is_vdp != is_vdp)) continue;
		if (start <= i->second.end && stop >= i->second.start)
		{
			brk = !(i->second.is_forbid);
			break;
		}
	}

	if (!brk) return false;

	range = Breakpoints.equal_range(BP_PC);
	for (auto i = range.first; i != range.second; ++i)
	{
		if (i->second.enabled && i->second.is_forbid)
		{
			if (pc <= (int)(i->second.end) && pc >= (int)(i->second.start))
				return false;
		}
	}

	return true;
}
