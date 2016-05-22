#ifndef IDA_DEBUG_H
#define IDA_DEBUG_H

HINSTANCE GetHInstance();
inline unsigned int mask(unsigned char bit_idx, unsigned char bits_cnt = 1)
{
    return (((1 << bits_cnt) - 1) << bit_idx);
}

#endif