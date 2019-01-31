
/* dist: public */

#ifndef __PACKETS_SECURITY_H
#define __PACKETS_SECURITY_H

#pragma pack(push,1)

struct C2SSecurity
{
	u8 type; /* 0x1A */
	u32 weaponcount;
	u32 settingchecksum;
	u32 exechecksum;
	u32 mapchecksum;
	u32 s2cslowtotal;
	u32 s2cfasttotal;
	u16 s2cslowcurrent;
	u16 s2cfastcurrent;
	u16 unknown1;
	u16 lastping;
	u16 averageping;
	u16 lowestping;
	u16 highestping;
	u8 slowframe;
};

struct S2CSecurity
{
	u8 type;        /* 0x18 */
	u32 greenseed;  /* seed for greens */
	u32 doorseed;   /* seed for doors */
	u32 timestamp;  /* timestamp */
	u32 key;        /* key for checksum use */
};

#pragma pack(pop)

#endif