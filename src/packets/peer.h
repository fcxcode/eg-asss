
/* dist: public */

#ifndef __PACKETS_PEER_H
#define __PACKETS_PEER_H

#pragma pack(push,1)

#define PEER_PLAYERLIST 0x01
#define PEER_CHAT 0x02
#define PEER_OP 0x03
#define PEER_POPULATION 0x04

struct PeerPacket
{
	u8 t1; // 0x00
	u8 t2; // 0x01
	u32 password; // crc32 of the peer hash
	u8 t3; // 0xFF
	u8 type; // peer packet type
	u32 timestamp;
	// payload
};

#pragma pack(pop)

#endif

