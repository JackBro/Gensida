#ifndef DEBUG_WINDOW_H
#define DEBUG_WINDOW_H
#include <windows.h>
#include <vector>
#include <forward_list>
#include <string>

#define WM_DEBUG_DUMMY_EXIT (WM_USER+1000)

#define BRK_PC     0x000001
#define BRK_READ   0x000002
#define BRK_WRITE  0x000004
#define BRK_VDP    0x000010
#define BRK_FORBID 0x000100

typedef unsigned int uint32;
typedef unsigned short ushort;

struct Breakpoint
{
    uint32 start;
    uint32 end;
    bool enabled;
	ushort type;

	Breakpoint(uint32 _start, uint32 _end, bool _enabled, ushort _type) : start(_start), end(_end), enabled(_enabled), type(_type) {};

	bool Breakpoint::operator ==(const Breakpoint &b) const
	{
		return (this->start >= b.start) && (this->end <= b.end) && ((this->type & b.type) != 0);
	}
};

struct DebugWindow
{
    DebugWindow();
    std::vector<uint32> callstack;
	std::forward_list<Breakpoint> Breakpoints;

    HWND DummyHWnd;

    bool StepInto;
    uint32 StepOver;

    void Breakpoint(int pc);
    void SetWhyBreak(LPCSTR lpString);

    bool BreakPC(int pc);
    bool BreakRead(int pc, uint32 start, uint32 stop, bool is_vdp);
	bool BreakWrite(int pc, uint32 start, uint32 stop, bool is_vdp);

    virtual void DoStepOver();
    virtual void TracePC(int pc);
    virtual void TraceRead(uint32 start, uint32 stop);
    virtual void TraceWrite(uint32 start, uint32 stop);
    virtual ~DebugWindow();
};

#endif
