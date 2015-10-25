#include <Windows.h>
#include <algorithm>
#include <ida.hpp>
#include <idd.hpp>
#include <dbg.hpp>
#include <diskio.hpp>
#include <auto.hpp>
#include <funcs.hpp>

#include "G_main.h"
#include "G_ddraw.h"
#include "G_dsound.h"
#include "Star_68k.h"
#include "m68k_debugwindow.h"
#include "vdp_io.h"
#include "ram_search.h"
#include "resource.h"

#include "ida_debmod.h"

#include "ida_registers.h"
#include "ida_debug.h"

#include <vector>

int PASCAL WinMain(HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR lpCmdLine, int nCmdShow);

codemap_t g_codemap;
eventlist_t g_events;

bool stopped = true;
qthread_t gens_thread = NULL;

#define CHECK_FOR_START(x) {if (stopped) return x;}

static const char *const SRReg[] =
{
	"C",
	"V",
	"Z",
	"N",
	"X",
	NULL,
	NULL,
	NULL,
	"I",
	"I",
	"I",
	NULL,
	NULL,
	"S",
	NULL,
	"T"
};


register_info_t registers[] =
{
	{ "D0", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "D1", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "D2", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "D3", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "D4", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "D5", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "D6", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "D7", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },

	{ "A0", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "A1", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "A2", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "A3", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "A4", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "A5", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "A6", REGISTER_ADDRESS, RC_GENERAL, dt_dword, NULL, 0 },
	{ "A7", REGISTER_ADDRESS | REGISTER_SP, RC_GENERAL, dt_dword, NULL, 0 },

	{ "PC", REGISTER_ADDRESS | REGISTER_IP, RC_GENERAL, dt_dword, NULL, 0 },

	{ "SR", NULL, RC_GENERAL, dt_word, SRReg, 0xFFFF },

	{ "MODE1", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "MODE2", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "PLANE_A_ADDR", NULL, RC_VDP, dt_word, NULL, 0 },
	{ "WINDOW_ADDR", NULL, RC_VDP, dt_word, NULL, 0 },
	{ "PLANE_B_ADDR", NULL, RC_VDP, dt_word, NULL, 0 },
	{ "SPRITE_TBL_ADDR", NULL, RC_VDP, dt_word, NULL, 0 },
	{ "SPRITES_REBASE", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "BACK_COLOR", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "HBLANK_COUNTER", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "MODE3", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "MODE4", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "HSCROLL_TBL_ADDR", NULL, RC_VDP, dt_word, NULL, 0 },
	{ "PLANES_REBASE", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "AUTO_INC_VALUE", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "PLANES_SIZE", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "WINDOW_HPOS", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "WINDOW_VPOS", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "DMA_LEN", NULL, RC_VDP, dt_word, NULL, 0 },
	//{ "DMA_LEN_HIGH", NULL, RC_VDP, dt_byte, NULL, 0 },
	{ "DMA_SRC", NULL, RC_VDP, dt_3byte, NULL, 0 },
	//{ "DMA_SRC_MID", NULL, RC_VDP, dt_byte, NULL, 0 },
	//{ "DMA_SRC_HIGH", NULL, RC_VDP, dt_byte, NULL, 0 },
};

static const char *register_classes[] =
{
	"General Registers",
	"VDP Registers",
	NULL
};

static void prepare_codemap()
{
	g_codemap.resize(MAX_ROM_SIZE);
	for (size_t i = 0; i < MAX_ROM_SIZE; ++i)
	{
		g_codemap[i] = std::pair<uint32, bool>(BADADDR, false);
	}
}

static void apply_codemap()
{
	msg("Applying codemap...\n");
	for (size_t i = 0; i < MAX_ROM_SIZE; ++i)
	{
		std::pair<uint32, bool> _pair = g_codemap[i];
		if (_pair.second && _pair.first)
		{
			auto_make_code((ea_t)i);
			noUsed((ea_t)i);
		}
		showAddr((ea_t)i);
	}
	noUsed(0, MAX_ROM_SIZE);

	for (size_t i = 0; i < MAX_ROM_SIZE; ++i)
	{
		std::pair<uint32, bool> _pair = g_codemap[i];
		if (_pair.second && _pair.first && !get_func((ea_t)i))
		{
			add_func(i, BADADDR);
			add_cref(_pair.first, i, fl_CN);
			noUsed((ea_t)i);
		}
		showAddr((ea_t)i);
	}
	noUsed(0, MAX_ROM_SIZE);
	msg("Codemap applied.\n");
}

inline static void toggle_pause()
{
	HWND hwndGens = FindWindowEx(NULL, NULL, "Gens", NULL);

	if (hwndGens != NULL)
		SendMessage(hwndGens, WM_COMMAND, ID_EMULATION_PAUSED, 0);
}

static void pause_execution()
{
	M68kDW.DummyHWnd = (HWND)0;

	if (Paused) return;
	toggle_pause();
}

static void continue_execution()
{
	M68kDW.DummyHWnd = (HWND)0;

	if (!Paused) return;
	toggle_pause();
}

static void finish_execution()
{
	if (stopped) return;
	stopped = true;

	if (gens_thread != NULL)
	{
		qthread_join(gens_thread);
		qthread_free(gens_thread);
		qthread_kill(gens_thread);
	}

	apply_codemap();
}

// Initialize debugger
// Returns true-success
// This function is called from the main thread
static bool idaapi init_debugger(const char *hostname,
	int port_num,
	const char *password)
{
	return true;
}

// Terminate debugger
// Returns true-success
// This function is called from the main thread
static bool idaapi term_debugger(void)
{
	finish_execution();
	return true;
}

// Return information about the n-th "compatible" running process.
// If n is 0, the processes list is reinitialized.
// 1-ok, 0-failed, -1-network error
// This function is called from the main thread
static int idaapi process_get_info(int n, process_info_t *info)
{
	return 0;
}

HINSTANCE GetHInstance()
{
	MEMORY_BASIC_INFORMATION mbi;
	SetLastError(ERROR_SUCCESS);
	VirtualQuery(GetHInstance, &mbi, sizeof(mbi));

	return (HINSTANCE)mbi.AllocationBase;
}

char cmdline[2048];
static int idaapi gens_process(void *ud)
{
	SetCurrentDirectoryA(idadir("plugins"));
	
	int rc;

	rc = WinMain(GetHInstance(), (HINSTANCE)NULL, cmdline, SW_NORMAL);

	debug_event_t ev;
	ev.eid = PROCESS_EXIT;
	ev.pid = 1;
	ev.handled = true;
	ev.exit_code = rc;

	g_events.enqueue(ev, IN_BACK);

	return rc;
}

// Start an executable to debug
// 1 - ok, 0 - failed, -2 - file not found (ask for process options)
// 1|CRC32_MISMATCH - ok, but the input file crc does not match
// -1 - network error
// This function is called from debthread
static int idaapi start_process(const char *path,
	const char *args,
	const char *startdir,
	int dbg_proc_flags,
	const char *input_path,
	uint32 input_file_crc32)
{
	qsnprintf(cmdline, sizeof(cmdline), "-rom \"%s\"", path);

	stopped = false;
	prepare_codemap();
	gens_thread = qthread_create(gens_process, NULL);

	return 1;
}

// rebase database if the debugged program has been rebased by the system
// This function is called from the main thread
static void idaapi rebase_if_required_to(ea_t new_base)
{
}

// Prepare to pause the process
// This function will prepare to pause the process
// Normally the next get_debug_event() will pause the process
// If the process is sleeping then the pause will not occur
// until the process wakes up. The interface should take care of
// this situation.
// If this function is absent, then it won't be possible to pause the program
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi prepare_to_pause_process(void)
{
	CHECK_FOR_START(1);
	pause_execution();
	return 1;
}

// Stop the process.
// May be called while the process is running or suspended.
// Must terminate the process in any case.
// The kernel will repeatedly call get_debug_event() and until PROCESS_EXIT.
// In this mode, all other events will be automatically handled and process will be resumed.
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi mess_exit_process(void)
{
	CHECK_FOR_START(1);
	HWND hwndGens = FindWindowEx(NULL, NULL, "Gens", NULL);

	if (hwndGens != NULL)
		SendMessage(hwndGens, WM_CLOSE, 0, 0);

	return 1;
}

// Get a pending debug event and suspend the process
// This function will be called regularly by IDA.
// This function is called from debthread
static gdecode_t idaapi get_debug_event(debug_event_t *event, int timeout_ms)
{
	while (true)
	{
		// are there any pending events?
		if (g_events.retrieve(event))
		{
			if (event->eid != PROCESS_START && event->eid != STEP && event->eid != PROCESS_SUSPEND && event->eid != PROCESS_EXIT)
				pause_execution();
			return g_events.empty() ? GDE_ONE_EVENT : GDE_MANY_EVENTS;
		}
		if (g_events.empty())
			break;
	}
	return GDE_NO_EVENT;
}

// Continue after handling the event
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi continue_after_event(const debug_event_t *event)
{
	switch (event->eid)
	{
	case BREAKPOINT:
	case STEP:
	case PROCESS_SUSPEND:
		continue_execution();
		break;
	case PROCESS_EXIT:
		finish_execution();
		break;
	}

	return 1;
}

// The following function will be called by the kernel each time
// when it has stopped the debugger process for some reason,
// refreshed the database and the screen.
// The debugger module may add information to the database if it wants.
// The reason for introducing this function is that when an event line
// LOAD_DLL happens, the database does not reflect the memory state yet
// and therefore we can't add information about the dll into the database
// in the get_debug_event() function.
// Only when the kernel has adjusted the database we can do it.
// Example: for imported PE DLLs we will add the exported function
// names to the database.
// This function pointer may be absent, i.e. NULL.
// This function is called from the main thread
static void idaapi stopped_at_debug_event(bool dlls_added)
{
}

// The following functions manipulate threads.
// 1-ok, 0-failed, -1-network error
// These functions are called from debthread
static int idaapi thread_suspend(thid_t tid) // Suspend a running thread
{
	return 0;
}

static int idaapi thread_continue(thid_t tid) // Resume a suspended thread
{
	return 0;
}

static int do_step(dbg_notification_t idx)
{
	switch (idx)
	{
	case dbg_step_into:
		M68kDW.StepInto = 1;
		M68kDW.DummyHWnd = (HWND)0;
		break;
	case dbg_step_over:
		M68kDW.DoStepOver();
		M68kDW.DummyHWnd = (HWND)0;
		break;
	}
	
	return 1;
}

static int idaapi thread_set_step(thid_t tid) // Run one instruction in the thread
{
	return do_step(get_running_notification());
}

static UINT32 mask(UINT8 bit_idx, UINT8 bits_cnt = 1)
{
	return (((1 << bits_cnt) - 1) << bit_idx);
}

// Read thread registers
//	tid	- thread id
//	clsmask- bitmask of register classes to read
//	regval - pointer to vector of regvals for all registers
//			 regval is assumed to have debugger_t::registers_size elements
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi read_registers(thid_t tid, int clsmask, regval_t *values)
{
	if (clsmask & RC_GENERAL)
	{
		values[R_D0].ival = main68k_context.dreg[R_D0 - R_D0];
		values[R_D1].ival = main68k_context.dreg[R_D1 - R_D0];
		values[R_D2].ival = main68k_context.dreg[R_D2 - R_D0];
		values[R_D3].ival = main68k_context.dreg[R_D3 - R_D0];
		values[R_D4].ival = main68k_context.dreg[R_D4 - R_D0];
		values[R_D5].ival = main68k_context.dreg[R_D5 - R_D0];
		values[R_D6].ival = main68k_context.dreg[R_D6 - R_D0];
		values[R_D7].ival = main68k_context.dreg[R_D7 - R_D0];

		values[R_A0].ival = main68k_context.areg[R_A0 - R_A0];
		values[R_A1].ival = main68k_context.areg[R_A1 - R_A0];
		values[R_A2].ival = main68k_context.areg[R_A2 - R_A0];
		values[R_A3].ival = main68k_context.areg[R_A3 - R_A0];
		values[R_A4].ival = main68k_context.areg[R_A4 - R_A0];
		values[R_A5].ival = main68k_context.areg[R_A5 - R_A0];
		values[R_A6].ival = main68k_context.areg[R_A6 - R_A0];
		values[R_A7].ival = main68k_context.areg[R_A7 - R_A0];

		values[R_PC].ival = M68kDW.last_pc;
		values[R_SR].ival = main68k_context.sr;
	}

	if (clsmask & RC_VDP)
	{
		unsigned int *vdpRegs = (unsigned int *)&VDP_Reg;
		values[R_DR00].ival = vdpRegs[R_DR00 - R_DR00];
		values[R_DR01].ival = vdpRegs[R_DR01 - R_DR00];
		values[R_DR02].ival = (vdpRegs[R_DR02 - R_DR00] & mask(3, 3)) << 10;
		values[R_DR03].ival = (vdpRegs[R_DR03 - R_DR00] & mask(1, 5)) << 10;
		values[R_DR04].ival = (vdpRegs[R_DR04 - R_DR00] & mask(0, 3)) << 13;
		values[R_DR05].ival = (vdpRegs[R_DR05 - R_DR00] & mask(0, 7)) << 9;
		values[R_DR06].ival = vdpRegs[R_DR06 - R_DR00];
		values[R_DR07].ival = vdpRegs[R_DR07 - R_DR00];
		values[R_DR08].ival = vdpRegs[R_DR10 - R_DR00];
		values[R_DR09].ival = vdpRegs[R_DR11 - R_DR00];
		values[R_DR10].ival = vdpRegs[R_DR12 - R_DR00];
		values[R_DR11].ival = (vdpRegs[R_DR13 - R_DR00] & mask(0, 6)) << 10;
		values[R_DR12].ival = vdpRegs[R_DR14 - R_DR00];
		values[R_DR13].ival = vdpRegs[R_DR15 - R_DR00];
		values[R_DR14].ival = vdpRegs[R_DR16 - R_DR00];
		values[R_DR15].ival = vdpRegs[R_DR17 - R_DR00];
		values[R_DR16].ival = vdpRegs[R_DR18 - R_DR00];
		values[R_DR17].ival = vdpRegs[R_DR19 - R_DR00] | (vdpRegs[R_DR20 - R_DR00] << 8);
		values[R_DR18].ival = vdpRegs[R_DR21 - R_DR00] | (vdpRegs[R_DR22 - R_DR00] << 8);

		UINT16 dma_high = vdpRegs[R_DR23 - R_DR00];
		if (!(dma_high & 0x80))
			values[R_DR18].ival |= ((vdpRegs[R_DR23 - R_DR00] & mask(0, 7)) << 16);
		else
			values[R_DR18].ival |= ((vdpRegs[R_DR23 - R_DR00] & mask(0, 6)) << 16);
		values[R_DR18].ival <<= 1;
	}

	return 1;
}

// Write one thread register
//	tid	- thread id
//	regidx - register index
//	regval - new value of the register
// 1-ok, 0-failed, -1-network error
// This function is called from debthread
static int idaapi write_register(thid_t tid, int regidx, const regval_t *value)
{
	if (regidx >= R_D0 && regidx <= R_D7)
	{
		main68k_context.dreg[regidx - R_D0] = value->ival;
	}
	else if (regidx >= R_A0 && regidx <= R_A7)
	{
		main68k_context.areg[regidx - R_A0] = value->ival;
	}
	else if (regidx == R_PC)
	{
		main68k_context.pc = value->ival;
	}
	else if (regidx == R_SR)
	{
		main68k_context.sr = value->ival;
	}
	else if (regidx >= R_DR00 && regidx < R_DR23)
	{
		unsigned int *vdpRegs = (unsigned int *)&VDP_Reg;
		regval_t val = *value;

		switch ((register_t)regidx)
		{
		case R_DR02:
			val.ival >>= 10;
			val.ival &= mask(3, 3);
			break;
		case R_DR03:
			val.ival >>= 10;
			val.ival &= mask(1, 5);
			break;
		case R_DR04:
			val.ival >>= 13;
			val.ival &= mask(0, 3);
			break;
		case R_DR05:
			val.ival >>= 9;
			val.ival &= mask(0, 7);
			break;
		case R_DR13:
			val.ival >>= 10;
			val.ival &= mask(0, 6);
			break;
		case R_DR17:
			val.ival >>= 8;
			val.ival &= 0xFF;
			vdpRegs[R_DR20 - R_DR00] = (unsigned int)val.ival;

			val = *value;
			val.ival &= 0xFF;
			regidx = R_DR19;
			break;
		case R_DR18:
			val.ival >>= 1;
			val.ival >>= 16;
			val.ival &= 0xFF;

			if (!(val.ival & 0x80))
				val.ival &= mask(0, 7);
			else
				val.ival &= mask(0, 6);

			vdpRegs[R_DR23 - R_DR00] = (unsigned int)val.ival;

			val = *value;
			val.ival >>= 1;
			val.ival >>= 8;
			val.ival &= 0xFF;
			vdpRegs[R_DR22 - R_DR00] = (unsigned int)val.ival;

			val = *value;
			val.ival >>= 1;
			val.ival &= 0xFF;
			regidx = R_DR21;
			break;
		}
		vdpRegs[regidx - R_DR00] = (unsigned int)val.ival;
	}

	return 1;
}

//
// The following functions manipulate bytes in the memory.
//
// Get information on the memory areas
// The debugger module fills 'areas'. The returned vector MUST be sorted.
// Returns:
//   -3: use idb segmentation
//   -2: no changes
//   -1: the process does not exist anymore
//	0: failed
//	1: new memory layout is returned
// This function is called from debthread
static int idaapi get_memory_info(meminfo_vec_t &areas)
{
	return -3;
}

extern bool IsHardwareAddressValid(unsigned int address);

// Read process memory
// Returns number of read bytes
// 0 means read error
// -1 means that the process does not exist anymore
// This function is called from debthread
static ssize_t idaapi read_memory(ea_t ea, void *buffer, size_t size)
{
	CHECK_FOR_START(0);
	for (size_t i = 0; i < size; ++i)
	{
		if (IsHardwareAddressValid(ea + i))
		{
			unsigned char value = (unsigned char)(ReadValueAtHardwareAddress(ea + i, 1) & 0xFF);
			((UINT8*)buffer)[i] = value;
		}
		else // else leave the value nil
		{
			i = i;
		}
	}

	return size;
}
// Write process memory
// Returns number of written bytes, -1-fatal error
// This function is called from debthread
static ssize_t idaapi write_memory(ea_t ea, const void *buffer, size_t size)
{
	return 0;
}

// Is it possible to set breakpoint?
// Returns: BPT_...
// This function is called from debthread or from the main thread if debthread
// is not running yet.
// It is called to verify hardware breakpoints.
static int idaapi is_ok_bpt(bpttype_t type, ea_t ea, int len)
{
	if (ea % 2 != 0)
		return BPT_BAD_ALIGN;

	switch (type)
	{
		//case BPT_SOFT:
	case BPT_EXEC:
	case BPT_READ: // there is no such constant in sdk61
	case BPT_WRITE:
	case BPT_RDWR:
		return BPT_OK;
	}

	return BPT_BAD_TYPE;
}

// Add/del breakpoints.
// bpts array contains nadd bpts to add, followed by ndel bpts to del.
// returns number of successfully modified bpts, -1-network error
// This function is called from debthread
static int idaapi update_bpts(update_bpt_info_t *bpts, int nadd, int ndel)
{
	CHECK_FOR_START(0);

	return (ndel + nadd);
}

//--------------------------------------------------------------------------
//
//	  DEBUGGER DESCRIPTION BLOCK
//
//--------------------------------------------------------------------------

debugger_t debugger =
{
	IDD_INTERFACE_VERSION,
	"GensIDA", // Short debugger name
	124, // Debugger API module id
	"m68k", // Required processor name
	DBG_FLAG_NOHOST | DBG_FLAG_CAN_CONT_BPT | DBG_FLAG_FAKE_ATTACH | DBG_FLAG_SAFE | DBG_FLAG_NOPASSWORD | DBG_FLAG_NOSTARTDIR | DBG_FLAG_LOWCNDS | DBG_FLAG_CONNSTRING | DBG_FLAG_ANYSIZE_HWBPT,

	register_classes, // Array of register class names
	RC_GENERAL, // Mask of default printed register classes
	registers, // Array of registers
	qnumber(registers), // Number of registers

	0x1000, // Size of a memory page

	NULL, // bpt_bytes, // Array of bytes for a breakpoint instruction
	NULL, // bpt_size, // Size of this array
	0, // for miniidbs: use this value for the file type after attaching
	0, // reserved

	init_debugger,
	term_debugger,

	process_get_info,

	start_process,
	NULL, // vamos_attach_process,
	NULL, // vamos_detach_process,
	rebase_if_required_to,
	prepare_to_pause_process,
	mess_exit_process,

	get_debug_event,
	continue_after_event,

	NULL, // set_exception_info
	stopped_at_debug_event,

	thread_suspend,
	thread_continue,
	thread_set_step,

	read_registers,
	write_register,

	NULL, // thread_get_sreg_base

	get_memory_info,
	read_memory,
	write_memory,

	is_ok_bpt,
	update_bpts,
	NULL, // update_lowcnds,

	NULL, // open_file
	NULL, // close_file
	NULL, // read_file

	NULL, // map_address,

	NULL, // set_dbg_options
	NULL, // get_debmod_extensions
	NULL, // update_call_stack

	NULL, // appcall
	NULL, // cleanup_appcall

	NULL, // eval_lowcnd

	NULL, // write_file

	NULL, // send_ioctl
};