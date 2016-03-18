#include "m68k_debugwindow.h"
#include "mem_m68k.h"
#include "Star_68k.h"
#include "m68kd.h"
#include "ram_dump.h"
#include "resource.h"

#include "ida_debmod.h"
extern codemap_t g_codemap;
extern eventlist_t g_events;
extern bool handled_ida_event;

M68kDebugWindow M68kDW;

M68kDebugWindow::M68kDebugWindow()
{
}

M68kDebugWindow::~M68kDebugWindow()
{
}

extern "C"
{
    extern uint32 hook_pc;
}

extern uint32 Current_PC;

unsigned short Next_Word_T(void);
unsigned int Next_Long_T(void);

void M68kDebugWindow::TracePC(int pc)
{
	handled_ida_event = false;
	
	if (last_pc != 0 && hook_pc != 0 && hook_pc < MAX_ROM_SIZE)
		g_codemap[hook_pc] = std::pair<uint32, bool>(last_pc, true);
	
	prev_pc = last_pc;
    last_pc = hook_pc;

    Current_PC = last_pc;
    int OPC = Next_Word_T();

    bool br = false;
    if (StepInto || StepOver == hook_pc)
    {
        br = true;

		debug_event_t ev;
		ev.eid = STEP;
		ev.pid = 1;
		ev.tid = 1;
		ev.ea = hook_pc;
		ev.handled = true;
		g_events.enqueue(ev, IN_BACK);

		handled_ida_event = true;
    }

    if (!br)
    {
        br = BreakPC(last_pc);
        if (br)
        {
            char bwhy[30];
            sprintf(bwhy, "Breakpoint PC:%06X", last_pc & 0xFFFFFF);
            SetWhyBreak(bwhy);
        }
    }

    if (br)
    {
        StepInto = false;
        StepOver = -1;
        Breakpoint(last_pc);
    }
    //if ((OPC >> 12)==4&&!(OPC & 0x100)&&((OPC >> 6) & 0x3F)==58)
    //if((OPC >> 12)==6&&(OPC & 0xF00) == 0x100)
    if ((OPC & 0xFFC0) == 0x4E80 ||//jsr
        (OPC & 0xFF00) == 0x6100)//bsr
    {
		callstack.push_back(last_pc);
    }

    //(OPC & 0x7)==5 && ((OPC >> 3) & 0x7)==6 && ((OPC >> 6) & 0x3F)==57 && !(OPC & 0x100) && (OPC >> 12)==4
    if ((OPC & 0xFFFF) == 0x4E75)//rts
    {
        if (!callstack.empty())
            callstack.pop_back();
    }
}

void M68kDebugWindow::TraceRegValue(uint8 reg_idx, uint32 value, bool is_vdp)
{
	handled_ida_event = false;
	if (BreakRegValue(last_pc, reg_idx, value, is_vdp))
	{
		char bwhy[50];
		sprintf(bwhy, "Reg[%02d] value break: %08X [%s]", reg_idx, value, is_vdp ? "VDP" : "M68K");
		SetWhyBreak(bwhy);
		Breakpoint(last_pc);
	}
}

void M68kDebugWindow::TraceRead(uint32 start, uint32 stop, bool is_vdp)
{
	handled_ida_event = false;
	if (BreakRead(last_pc, start, stop, is_vdp))
    {
        char bwhy[33];
        sprintf(bwhy, "Read: %08X-%08X", start, stop);
        SetWhyBreak(bwhy);
        Breakpoint(last_pc);
    }
}

void M68kDebugWindow::TraceWrite(uint32 start, uint32 stop, bool is_vdp)
{
	handled_ida_event = false;
	if (BreakWrite(last_pc, start, stop, is_vdp))
    {
        char bwhy[33];
        sprintf(bwhy, "Write: %08X-%08X", start, stop);
        SetWhyBreak(bwhy);
        Breakpoint(last_pc);
    }
}

void M68kDebugWindow::DoStepOver()
{
    Current_PC = last_pc;
    char *dis = M68KDisasm2(Next_Word_T, Next_Long_T, last_pc);
    if (strncmp(dis, "JSR", 3) == 0
        || strncmp(dis, "BSR", 3) == 0)
        StepOver = Current_PC;
    else
    {
        StepInto = true;
        StepOver = -1;
    }
}
