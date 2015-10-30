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

extern FILE* fp_trace;
extern FILE* fp_call;
extern FILE* fp_hook;

extern std::vector<HookList> rd_list, wr_list, ppu_list, pc_list;

extern bool trace_map;
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
    void hook_vdp_reg();

    void GensTrace();
};

extern char *mapped;
uint32 Current_PC;
int Debug = 1;

void DeInitDebug()
{
    if (fp_hook)
    {
        fclose(fp_hook);
    }

	pc_list.clear();
	rd_list.clear();
	wr_list.clear();
	ppu_list.clear();

    if (fp_trace)
    {
        delete[] mapped;
        fclose(fp_trace);
    }
}

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

void Print_Instruction(FILE *trace)
{
    char String[512];
    Current_PC = hook_pc;

    int PC;
    int OPC = M68K_RW(Current_PC);

    PC = Current_PC;
    sprintf(String, "%02X:%04X  %02X %02X  %-33s",
        PC >> 16, PC & 0xffff, OPC >> 8, OPC & 0xff,
        M68KDisasm2(Next_Word_T, Next_Long_T, hook_pc));
    fprintf(trace, "%s", String);

    sprintf(String, "A0=%.8X A1=%.8X A2=%.8X ", main68k_context.areg[0], main68k_context.areg[1], main68k_context.areg[2]);
    fprintf(trace, "%s", String);

    sprintf(String, "A3=%.8X A4=%.8X A5=%.8X ", main68k_context.areg[3], main68k_context.areg[4], main68k_context.areg[5]);
    fprintf(trace, "%s", String);

    sprintf(String, "A6=%.8X A7=%.8X D0=%.8X ", main68k_context.areg[6], main68k_context.areg[7], main68k_context.dreg[0]);
    fprintf(trace, "%s", String);

    sprintf(String, " ~~  ");

    sprintf(String, "D1=%.8X D2=%.8X D3=%.8X ", main68k_context.dreg[1], main68k_context.dreg[2], main68k_context.dreg[3]);
    fprintf(trace, "%s", String);

    sprintf(String, "D4=%.8X D5=%.8X D6=%.8X ", main68k_context.dreg[4], main68k_context.dreg[5], main68k_context.dreg[6]);
    fprintf(trace, "%s", String);

    sprintf(String, "D7=%.8X ", main68k_context.dreg[7]);
    fprintf(trace, "%s", String);

    fprintf(trace, "%c", (main68k_context.sr & 0x10) ? 'X' : 'x');
    fprintf(trace, "%c", (main68k_context.sr & 0x08) ? 'N' : 'n');
    fprintf(trace, "%c", (main68k_context.sr & 0x04) ? 'Z' : 'z');
    fprintf(trace, "%c", (main68k_context.sr & 0x02) ? 'V' : 'v');
    fprintf(trace, "%c", (main68k_context.sr & 0x01) ? 'C' : 'c');

    fprintf(trace, "\n", String);
}

static void GensTrace_trace()
{
    if (mapped[hook_pc] < 0x40)
    {
        Print_Instruction(fp_trace);
        mapped[hook_pc] ++;
    }
}
void GensTrace()
{
    // Trace.txt
    if (trace_map)
        GensTrace_trace();
    CallRegisteredLuaMemHook(hook_pc, 2, 0, LUAMEMHOOK_EXEC);
    M68kDW.TracePC(hook_pc);

    // Hook.txt
    if (hook_trace)
    {
        for (size_t lcv = 0; lcv < pc_list.size(); lcv++)
        {
            FILE *out;

            // start-stop
			if (pc_list[lcv].mode & 1)
            {
				if (hook_pc == pc_list[lcv].low)
					pc_list[lcv].start = 1;

				if (!pc_list[lcv].start) continue;

                out = fp_hook;
            }
            // low-high
            else
            {
                // fail: outside boundaries
				if (hook_pc < pc_list[lcv].low) continue;
				if (hook_pc > pc_list[lcv].high) continue;
            }

            // ------------------------------------------------------

			if (pc_list[lcv].mode >= 4 && pc_list[lcv].mode < 8)
            {
				pc_list[lcv].mode &= 3;
                trace_map = 1;
            }

            // output file mode
			out = (pc_list[lcv].mode <= 1) ? fp_hook : fp_trace;

            if (!out) continue;
            if (out == fp_trace)
                fprintf(out, "** ");

            Print_Instruction(out);

            // end formatting
			if (hook_pc == pc_list[lcv].high)
            {
				pc_list[lcv].start = 0;

				if (pc_list[lcv].low != pc_list[lcv].high)
                    fprintf(out, "\n");
            }
        } // end STATES
    } // end hook
}

static void trace_read_byte_internal()
{
    unsigned int start, stop;

    start = hook_address;
    stop = start + 0;

	for (size_t lcv = 0; lcv < rd_list.size(); lcv++)
    {
        FILE *out;

        // linear map
		if (rd_list[lcv].mode & 1)
        {
            // fail: outside boundaries
			if (stop < rd_list[lcv].low) continue;
			if (start > rd_list[lcv].high) continue;
        }
        // shadow map
        else
        {
            // fail: outside boundaries
			if ((stop & 0xffff) < (rd_list[lcv].low & 0xffff)) continue;
			if ((start & 0xffff) > (rd_list[lcv].high & 0xffff)) continue;

			if ((stop >> 16) < (rd_list[lcv].low >> 16)) continue;
			if ((start >> 16) > (rd_list[lcv].high >> 16)) continue;
        }

        // ------------------------------------------------------

        // auto-trace
		if (rd_list[lcv].mode >= 4 && rd_list[lcv].mode < 8)
        {
			rd_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (rd_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s = %02X [%06X]\n",
            hook_pc >> 16, hook_pc & 0xffff, "R08",
            hook_value & 0xff, hook_address);
    }
}

void trace_read_byte()
{
    if (hook_trace)
        trace_read_byte_internal();
    M68kDW.TraceRead(hook_address, hook_address);
    CallRegisteredLuaMemHook(hook_address, 1, hook_value, LUAMEMHOOK_READ);
}

static void trace_read_word_internal()
{
    unsigned int start, stop;

    start = hook_address;
    stop = start + 1;

	for (size_t lcv = 0; lcv < rd_list.size(); lcv++)
    {
        FILE *out;

        // linear map
		if (rd_list[lcv].mode & 1)
        {
            // fail: outside boundaries
			if (stop < rd_list[lcv].low) continue;
			if (start > rd_list[lcv].high) continue;
        }
        // shadow map
        else
        {
            // fail: outside boundaries
			if ((stop & 0xffff) < (rd_list[lcv].low & 0xffff)) continue;
			if ((start & 0xffff) > (rd_list[lcv].high & 0xffff)) continue;

			if ((stop >> 16) < (rd_list[lcv].low >> 16)) continue;
			if ((start >> 16) > (rd_list[lcv].high >> 16)) continue;
        }

        // ------------------------------------------------------

        // auto-trace
		if (rd_list[lcv].mode >= 4 && rd_list[lcv].mode < 8)
        {
			rd_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (rd_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s = %04X [%06X]\n",
            hook_pc >> 16, hook_pc & 0xffff, "R16",
            hook_value & 0xffff, hook_address);
    }
}
void trace_read_word()
{
    if (hook_trace)
        trace_read_word_internal();
    M68kDW.TraceRead(hook_address, hook_address + 1);
    CallRegisteredLuaMemHook(hook_address, 2, hook_value, LUAMEMHOOK_READ);
}

static void trace_read_dword_internal()
{
    unsigned int start, stop;

    start = hook_address;
    stop = start + 3;

	for (size_t lcv = 0; lcv < rd_list.size(); lcv++)
    {
        FILE *out;

        // linear map
		if (rd_list[lcv].mode & 1)
        {
            // fail: outside boundaries
			if (stop < rd_list[lcv].low) continue;
			if (start > rd_list[lcv].high) continue;
        }
        // shadow map
        else
        {
            // fail: outside boundaries
			if ((stop & 0xffff) < (rd_list[lcv].low & 0xffff)) continue;
			if ((start & 0xffff) > (rd_list[lcv].high & 0xffff)) continue;

			if ((stop >> 16) < (rd_list[lcv].low >> 16)) continue;
			if ((start >> 16) > (rd_list[lcv].high >> 16)) continue;
        }

        // ------------------------------------------------------

        // auto-trace
		if (rd_list[lcv].mode >= 4 && rd_list[lcv].mode < 8)
        {
			rd_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (rd_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s = %08X [%06X]\n",
            hook_pc >> 16, hook_pc & 0xffff, "R32",
            hook_value & 0xffffffff, hook_address);
    }
}
void trace_read_dword()
{
    if (hook_trace)
        trace_read_dword_internal();
    M68kDW.TraceRead(hook_address, hook_address + 3);
    CallRegisteredLuaMemHook(hook_address, 4, hook_value, LUAMEMHOOK_READ);
}

static void trace_write_byte_internal()
{
    unsigned int start, stop;

    if (hook_address >= 0x00e00000) hook_address |= 0x00ff0000;

    start = hook_address;
    stop = start + 0;

	for (size_t lcv = 0; lcv < wr_list.size(); lcv++)
    {
        FILE *out;

        // linear map
		if (wr_list[lcv].mode & 1)
        {
            // fail: outside boundaries
			if (stop < wr_list[lcv].low) continue;
			if (start > wr_list[lcv].high) continue;
        }
        // shadow map
        else
        {
            // fail: outside boundaries
			if ((stop & 0xffff) < (wr_list[lcv].low & 0xffff)) continue;
			if ((start & 0xffff) > (wr_list[lcv].high & 0xffff)) continue;

			if ((stop >> 16) < (wr_list[lcv].low >> 16)) continue;
			if ((start >> 16) > (wr_list[lcv].high >> 16)) continue;
        }

        // ------------------------------------------------------

        // auto-trace
		if (wr_list[lcv].mode >= 4 && wr_list[lcv].mode < 8)
        {
			wr_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (wr_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        if (hook_pc)
            fprintf(out, "[%02X:%04X] %s = %02X [%06X]\n",
            hook_pc >> 16, hook_pc & 0xffff, "W08",
            hook_value & 0xff, hook_address);
        else
            fprintf(out, "[GGWRITE] %s = %02X [%06X]\n",
            "W08", hook_value & 0xff, hook_address);
    }
}
void trace_write_byte()
{
    if (hook_trace)
        trace_write_byte_internal();
    M68kDW.TraceWrite(hook_address, hook_address);
    CallRegisteredLuaMemHook(hook_address, 1, hook_value, LUAMEMHOOK_WRITE);
}

static void trace_write_word_internal()
{
    unsigned int start, stop;

    start = hook_address;
    stop = start + 1;

	for (size_t lcv = 0; lcv < wr_list.size(); lcv++)
    {
        FILE *out;

        // linear map
		if (wr_list[lcv].mode & 1)
        {
            // fail: outside boundaries
			if (stop < wr_list[lcv].low) continue;
			if (start > wr_list[lcv].high) continue;
        }
        // shadow map
        else
        {
            // fail: outside boundaries
			if ((stop & 0xffff) < (wr_list[lcv].low & 0xffff)) continue;
			if ((start & 0xffff) > (wr_list[lcv].high & 0xffff)) continue;

			if ((stop >> 16) < (wr_list[lcv].low >> 16)) continue;
			if ((start >> 16) > (wr_list[lcv].high >> 16)) continue;
        }

        // ------------------------------------------------------

        // auto-trace
		if (wr_list[lcv].mode >= 4 && wr_list[lcv].mode < 8)
        {
			wr_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (wr_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s = %04X [%06X]\n",
            hook_pc >> 16, hook_pc & 0xffff, "W16",
            hook_value & 0xffff, hook_address);
    }
}
void trace_write_word()
{
    if (hook_trace)
        trace_write_word_internal();
    M68kDW.TraceWrite(hook_address, hook_address + 1);
    CallRegisteredLuaMemHook(hook_address, 2, hook_value, LUAMEMHOOK_WRITE);
}

static void trace_write_dword_internal()
{
    unsigned int start, stop;

    start = hook_address;
    stop = start + 3;

	for (size_t lcv = 0; lcv < wr_list.size(); lcv++)
    {
        FILE *out;

        // linear map
		if (wr_list[lcv].mode & 1)
        {
            // fail: outside boundaries
			if (stop < wr_list[lcv].low) continue;
			if (start > wr_list[lcv].high) continue;
        }
        // shadow map
        else
        {
            // fail: outside boundaries
			if ((stop & 0xffff) < (wr_list[lcv].low & 0xffff)) continue;
			if ((start & 0xffff) > (wr_list[lcv].high & 0xffff)) continue;

			if ((stop >> 16) < (wr_list[lcv].low >> 16)) continue;
			if ((start >> 16) > (wr_list[lcv].high >> 16)) continue;
        }

        // ------------------------------------------------------

        // auto-trace
		if (wr_list[lcv].mode >= 4 && wr_list[lcv].mode < 8)
        {
			wr_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (wr_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s = %08X [%06X]\n",
            hook_pc >> 16, hook_pc & 0xffff, "W32",
            hook_value & 0xffffffff, hook_address);
    }
}
void trace_write_dword()
{
    if (hook_trace)
        trace_write_dword_internal();
    M68kDW.TraceWrite(hook_address, hook_address + 3);
    CallRegisteredLuaMemHook(hook_address, 4, hook_value, LUAMEMHOOK_WRITE);
}

static void hook_dma_internal()
{
    unsigned int start, stop;
	size_t lcv;

    // VDP area
    hook_value &= 3;

    // Memory breakpoints
    start = VDP_Reg.DMA_Address << 1;
    stop = start + (VDP_Reg.DMA_Length << 1) - 1;

    for (lcv = 0; lcv < rd_list.size(); lcv++)
    {
        FILE *out;

        // linear map
		if (rd_list[lcv].mode & 1)
        {
            // fail: outside boundaries
			if (stop < rd_list[lcv].low) continue;
			if (start > rd_list[lcv].high) continue;
        }
        // shadow map
        else
        {
            // fail: outside boundaries
			if ((stop & 0xffff) < (rd_list[lcv].low & 0xffff)) continue;
			if ((start & 0xffff) > (rd_list[lcv].high & 0xffff)) continue;

			if ((stop >> 16) < (rd_list[lcv].low >> 16)) continue;
			if ((start >> 16) > (rd_list[lcv].high >> 16)) continue;
        }

        // ------------------------------------------------------

        // auto-trace
		if (rd_list[lcv].mode >= 4 && rd_list[lcv].mode < 8)
        {
			rd_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (rd_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s DMA from %06X to %04X [%04X]\n",
            hook_pc >> 16, hook_pc & 0xffff,
            (hook_value <= 1) ? "VRAM" : (hook_value == 2) ? "CRAM" : "VSRAM",
            start & 0xffffff,
            Ctrl.Address & 0xffff,
            (VDP_Reg.DMA_Length << 1) & 0xffff);

        fprintf(out, "- SRC_H = [%02X:%04X]  ~~  LEN_H = [%02X:%04X]\n",
            dma_src >> 16, dma_src & 0xffff,
            dma_len >> 16, dma_len & 0xffff);

        fprintf(out, "\n");
    }

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

    // breakpoints
    for (lcv = 0; lcv < ppu_list.size(); lcv++)
    {
        FILE *out;

        // VDP memory only
		if ((ppu_list[lcv].mode & 1) != 1) continue;

        // fail case: outside range
		if (stop_l < ppu_list[lcv].low) continue;
		if (start_l > ppu_list[lcv].high) continue;

        // ------------------------------------------------------

        // auto-trace
		if (ppu_list[lcv].mode >= 4 && ppu_list[lcv].mode < 8)
        {
			ppu_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (ppu_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s DMA from %06X to %04X [%04X]\n",
            hook_pc >> 16, hook_pc & 0xffff,
            (hook_value <= 1) ? "VRAM" : (hook_value <= 2) ? "CRAM" : "VSRAM",
            (VDP_Reg.DMA_Address << 1) & 0xffffff,
            start & 0xffff, (VDP_Reg.DMA_Length << 1) & 0xffff);

        fprintf(out, "- SRC_H = $%02X:%04X  ~~  LEN_H = $%02X:%04X\n",
            dma_src >> 16, dma_src & 0xffff,
            dma_len >> 16, dma_len & 0xffff);

        fprintf(out, "\n");
    }
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

    // breakpoints
	for (size_t lcv = 0; lcv < ppu_list.size(); lcv++)
    {
        FILE *out;

        // VRAM only
		if ((ppu_list[lcv].mode & 1) != 1) continue;

        // fail case: outside range
		if (stop_l < ppu_list[lcv].low) continue;
		if (start_l > ppu_list[lcv].high) continue;

        // ------------------------------------------------------

        // auto-trace
		if (ppu_list[lcv].mode >= 4 && ppu_list[lcv].mode < 8)
        {
			ppu_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (ppu_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s W08 = %02X [%04X]\n",
            hook_pc >> 16, hook_pc & 0xffff,
            (Ctrl.Access == 9) ? "VRAM" : (Ctrl.Access == 10) ? "CRAM" : "VSRAM",
            hook_value & 0xff, Ctrl.Address & 0xffff);
    }
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

    // breakpoints
	for (size_t lcv = 0; lcv < ppu_list.size(); lcv++)
    {
        FILE *out;

        // VRAM only
		if ((ppu_list[lcv].mode & 1) != 1) continue;

        // fail case: outside range
		if (stop_l < ppu_list[lcv].low) continue;
		if (start_l > ppu_list[lcv].high) continue;

        // ------------------------------------------------------

        // auto-trace
		if (ppu_list[lcv].mode >= 4 && ppu_list[lcv].mode < 8)
        {
			ppu_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (ppu_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s W16 = %04X [%04X]\n",
            hook_pc >> 16, hook_pc & 0xffff,
            (Ctrl.Access == 9) ? "VRAM" : (Ctrl.Access == 10) ? "CRAM" : "VSRAM",
            hook_value & 0xffff, Ctrl.Address & 0xffff);
    }
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

    // breakpoints
	for (size_t lcv = 0; lcv < ppu_list.size(); lcv++)
    {
        FILE *out;

        // VRAM only
		if ((ppu_list[lcv].mode & 1) != 1) continue;

        // fail case: outside range
		if (stop_l < ppu_list[lcv].low) continue;
		if (start_l > ppu_list[lcv].high) continue;

        // ------------------------------------------------------

        // auto-trace
		if (ppu_list[lcv].mode >= 4 && ppu_list[lcv].mode < 8)
        {
			ppu_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (ppu_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s R08 = %02X [%04X]\n",
            hook_pc >> 16, hook_pc & 0xffff,
            (Ctrl.Access == 5) ? "VRAM" : (Ctrl.Access == 6) ? "CRAM" : "VSRAM",
            hook_value & 0xff, Ctrl.Address & 0xffff);
    }
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

    // breakpoints
	for (size_t lcv = 0; lcv < ppu_list.size(); lcv++)
    {
        FILE *out;

        // VRAM only
		if ((ppu_list[lcv].mode & 1) != 1) continue;

        // fail case: outside range
		if (stop_l < ppu_list[lcv].low) continue;
		if (start_l > ppu_list[lcv].high) continue;

        // ------------------------------------------------------

        // auto-trace
		if (ppu_list[lcv].mode >= 4 && ppu_list[lcv].mode < 8)
        {
			ppu_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (ppu_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] %s R16 = %04X [%04X]\n",
            hook_pc >> 16, hook_pc & 0xffff,
            (Ctrl.Access == 5) ? "VRAM" : (Ctrl.Access == 6) ? "CRAM" : "VSRAM",
            hook_value & 0xffff, Ctrl.Address & 0xffff);
    }
}
void trace_read_vram_word()
{
    if (hook_trace)
        trace_read_vram_word_internal();
}

static void hook_vdp_reg_internal()
{
    unsigned int start, stop;

    hook_pc &= 0x00ffffff;
    hook_address &= 0x00ffffff;

    start = hook_address;
    stop = start + 0;

    // VRAM breakpoints
	for (size_t lcv = 0; lcv < ppu_list.size(); lcv++)
    {
        FILE *out;

        // VDP registers only
		if ((ppu_list[lcv].mode & 1) != 0) continue;

        // fail case: outside range
		if (stop < ppu_list[lcv].low) continue;
		if (start > ppu_list[lcv].high) continue;

        // ------------------------------------------------------

        // auto-trace
		if (ppu_list[lcv].mode >= 4 && ppu_list[lcv].mode < 8)
        {
			ppu_list[lcv].mode &= 3;
            //trace_map = 1;

            hook_trace = 0;
            GensTrace();
            hook_trace = 1;
        }

        // output file mode
		out = (ppu_list[lcv].mode <= 1) ? fp_hook : fp_trace;

        if (!out) continue;
        if (out == fp_trace)
            fprintf(out, "** ");

        fprintf(out, "[%02X:%04X] VDP REG W08 = %02X [%02X]\n",
            hook_pc >> 16, hook_pc & 0xffff,
            hook_value & 0xff, hook_address & 0xff);
    }
}
void hook_vdp_reg()
{
    if (hook_trace)
        hook_vdp_reg_internal();
}
