#include <stdio.h>
#include <memory.h>
#include <vector>

#include "Cpu_68k.h"
#include "M68KD.h"
#include "M68k_debugwindow.h"
#include "Mem_M68k.h"
#include "Mem_S68k.h"
#include "vdp_io.h"
#include "luascript.h"

#include "tracer.h"

extern bool hook_trace;

extern "C" {
    extern uint32 hook_address;
    extern uint32 hook_value;
    extern uint32 hook_pc;

    unsigned int dma_src, dma_len;

    void trace_read_byte();
    void trace_read_word();
    void trace_read_dword();
    void trace_write_byte();
    void trace_write_word();
    void trace_write_dword();

    void trace_write_vram_byte();
    void trace_write_vram_word();
    void trace_read_vram_byte();
    void trace_read_vram_word();

    void hook_dma();

	void trace_exec_pc();
};

uint32 Current_PC;
int Debug = 1;

unsigned short Next_Word_T(void)
{
    unsigned short val;

    if (Debug == 1) val = M68K_RW(Current_PC);
    else if (Debug >= 2) val = S68K_RW(Current_PC);

    Current_PC += 2;

    return(val);
}

unsigned int Next_Long_T(void)
{
    unsigned int val;

    if (Debug == 1)
    {
        val = M68K_RW(Current_PC);
        val <<= 16;
        val |= M68K_RW(Current_PC + 2);
    }
    else if (Debug >= 2)
    {
        val = S68K_RW(Current_PC);
        val <<= 16;
        val |= S68K_RW(Current_PC + 2);
    }

    Current_PC += 4;

    return(val);
}

void trace_exec_pc()
{
    M68kDW.TracePC(hook_pc);
	CallRegisteredLuaMemHook(hook_pc, 2, 0, LUAMEMHOOK_EXEC);
}

void trace_read_byte()
{
    M68kDW.TraceRead(hook_address, hook_address, false);
    CallRegisteredLuaMemHook(hook_address, 1, hook_value, LUAMEMHOOK_READ);
}

void trace_read_word()
{
	M68kDW.TraceRead(hook_address, hook_address + 1, false);
    CallRegisteredLuaMemHook(hook_address, 2, hook_value, LUAMEMHOOK_READ);
}

void trace_read_dword()
{
	M68kDW.TraceRead(hook_address, hook_address + 3, false);
    CallRegisteredLuaMemHook(hook_address, 4, hook_value, LUAMEMHOOK_READ);
}

void trace_write_byte()
{
	M68kDW.TraceWrite(hook_address, hook_address, false);
    CallRegisteredLuaMemHook(hook_address, 1, hook_value, LUAMEMHOOK_WRITE);
}

void trace_write_word()
{
	M68kDW.TraceWrite(hook_address, hook_address + 1, false);
    CallRegisteredLuaMemHook(hook_address, 2, hook_value, LUAMEMHOOK_WRITE);
}

void trace_write_dword()
{
	M68kDW.TraceWrite(hook_address, hook_address + 3, false);
    CallRegisteredLuaMemHook(hook_address, 4, hook_value, LUAMEMHOOK_WRITE);
}

static void hook_dma_internal()
{
    unsigned int start, stop;

    // VDP area
    hook_value &= 3;

    // Memory breakpoints
    start = VDP_Reg.DMA_Address << 1;
    stop = start + (VDP_Reg.DMA_Length << 1) - 1;

	M68kDW.TraceRead(start, stop, false);

    /**************************************************/
    /**************************************************/

    unsigned int start_l, stop_l;

    start = Ctrl.Address;
    stop = start + (VDP_Reg.DMA_Length << 1) - 1;

    // local linear
    if (hook_value <= 1)
    {
        // VRAM
        start_l = start + 0x00000;
        stop_l = stop + 0x00000;
    }
    else if (hook_value == 2)
    {
        // CRAM
        start_l = start + 0x10000;
        stop_l = stop + 0x10000;
    }
    else if (hook_value == 3)
    {
        // VSRAM
        start_l = start + 0x20000;
        stop_l = stop + 0x20000;
    }
    else
    {
        // Error
        return;
    }

	M68kDW.TraceWrite(start_l, stop_l, true);
}
void hook_dma()
{
    if (hook_trace)
        hook_dma_internal();
}

static void trace_write_vram_byte_internal()
{
    unsigned int start, stop;
    unsigned int start_l, stop_l;

    hook_pc &= 0x00ffffff;
    hook_address &= 0x00ffffff;

    start = Ctrl.Address;
    stop = start + 0;

    // local linear
    if (Ctrl.Access == 9)
    {
        // VRAM
        start_l = start + 0x00000;
        stop_l = stop + 0x00000;
    }
    else if (Ctrl.Access == 10)
    {
        // CRAM
        start_l = start + 0x10000;
        stop_l = stop + 0x10000;
    }
    else if (Ctrl.Access == 11)
    {
        // VSRAM
        start_l = start + 0x20000;
        stop_l = stop + 0x20000;
    }
    else
    {
        // Error
        return;
    }

	M68kDW.TraceWrite(start_l, stop_l, true);
}
void trace_write_vram_byte()
{
    if (hook_trace)
        trace_write_vram_byte_internal();
}

static void trace_write_vram_word_internal()
{
    unsigned int start, stop;
    unsigned int start_l, stop_l;

    hook_pc &= 0x00ffffff;
    hook_address &= 0x00ffffff;

    start = Ctrl.Address;
    stop = start + 1;

    // local linear
    if (Ctrl.Access == 9)
    {
        // VRAM
        start_l = start + 0x00000;
        stop_l = stop + 0x00000;
    }
    else if (Ctrl.Access == 10)
    {
        // CRAM
        start_l = start + 0x10000;
        stop_l = stop + 0x10000;
    }
    else if (Ctrl.Access == 11)
    {
        // VSRAM
        start_l = start + 0x20000;
        stop_l = stop + 0x20000;
    }
    else
    {
        // Error
        return;
    }

	M68kDW.TraceWrite(start_l, stop_l, true);
}
void trace_write_vram_word()
{
    if (hook_trace)
        trace_write_vram_word_internal();
}

static void trace_read_vram_byte_internal()
{
    unsigned int start, stop;
    unsigned int start_l, stop_l;

    hook_pc &= 0x00ffffff;
    hook_address &= 0x00ffffff;

    start = Ctrl.Address;
    stop = start + 0;

    // local linear
    if (Ctrl.Access == 5)
    {
        // VRAM
        start_l = start + 0x00000;
        stop_l = stop + 0x00000;
    }
    else if (Ctrl.Access == 6)
    {
        // CRAM
        start_l = start + 0x10000;
        stop_l = stop + 0x10000;
    }
    else if (Ctrl.Access == 7)
    {
        // VSRAM
        start_l = start + 0x20000;
        stop_l = stop + 0x20000;
    }
    else
    {
        // Error
        return;
    }

	M68kDW.TraceRead(start_l, stop_l, true);
}
void trace_read_vram_byte()
{
    if (hook_trace)
        trace_read_vram_byte_internal();
}

static void trace_read_vram_word_internal()
{
    unsigned int start, stop;
    unsigned int start_l, stop_l;

    hook_pc &= 0x00ffffff;
    hook_address &= 0x00ffffff;

    start = Ctrl.Address;
    stop = start + 1;

    // local linear
    if (Ctrl.Access == 5)
    {
        // VRAM
        start_l = start + 0x00000;
        stop_l = stop + 0x00000;
    }
    else if (Ctrl.Access == 6)
    {
        // CRAM
        start_l = start + 0x10000;
        stop_l = stop + 0x10000;
    }
    else if (Ctrl.Access == 7)
    {
        // VSRAM
        start_l = start + 0x20000;
        stop_l = stop + 0x20000;
    }
    else
    {
        // Error
        return;
    }

	M68kDW.TraceRead(start_l, stop_l, true);
}
void trace_read_vram_word()
{
    if (hook_trace)
        trace_read_vram_word_internal();
}

