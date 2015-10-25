#ifndef __REGISTERS_H__
#define __REGISTERS_H__

#define RC_GENERAL 1
#define RC_VDP 2

enum register_t
{
	R_D0,
	R_D1,
	R_D2,
	R_D3,
	R_D4,
	R_D5,
	R_D6,
	R_D7,

	R_A0,
	R_A1,
	R_A2,
	R_A3,
	R_A4,
	R_A5,
	R_A6,
	R_A7,

	R_PC,

	R_SR,

    R_DR00,
    R_DR01,
    R_DR02,
    R_DR03,
    R_DR04,
    R_DR05,
    R_DR06,
    R_DR07,
    R_DR08,
    R_DR09,
    R_DR10,
    R_DR11,
    R_DR12,
    R_DR13,
    R_DR14,
    R_DR15,
    R_DR16,
    R_DR17,
    R_DR18,
    R_DR19,
    R_DR20,
    R_DR21,
    R_DR22,
    R_DR23,
};

#endif