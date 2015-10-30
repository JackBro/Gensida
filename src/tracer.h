#ifndef tracer_h
#define tracer_h
extern "C" {
    void trace_read_byte();
    void trace_read_byte_cd();
    void trace_read_word();
    void trace_read_word_cd();
    void trace_read_dword();
    void trace_read_dword_cd();
    void trace_write_byte();
    void trace_write_byte_cd();
    void trace_write_word();
    void trace_write_word_cd();
    void trace_write_dword();
    void trace_write_dword_cd();

    void trace_write_vram_byte();
    void trace_write_vram_word();
    void trace_read_vram_byte();
    void trace_read_vram_word();

    void hook_dma();
    void hook_vdp_reg();

    void GensTrace();
    void GensTrace_cd();
}

void InitDebug();
void InitDebug_cd();
void DeInitDebug();
void DeInitDebug_cd();

struct HookList
{
	unsigned int mode = 0;
	unsigned int low = 0, high = 0;
	unsigned int start = 0;

	HookList(unsigned int _mode, unsigned int _low, unsigned int _high) : mode(_mode), low(_low), high(_high) {};
	HookList(unsigned int _mode, unsigned int _low, unsigned int _high, unsigned int _start) : mode(_mode), low(_low), high(_high), start(_start) {};
};
#endif
