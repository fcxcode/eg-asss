
/* dist: public */

/* Subgame compatible peer protocol.
 * The original implementation for ASSS was written by Sharvil Nanavati (Snrrrub) in C++.
 * I have rewritten this into a C module so that ASSS is not dependent upon C++ to compile. -JoWie
 *
 * This file is dual licensed under GPL 2 (or later) and MIT, you can choose under which of these two license you would
 * like to use, modify, distribute, et cetera, this code.
 */


#include "asss.h"
#include "rwlock.h"
#include "redirect.h"
#include "peer.h"
#include "packets/peer.h"
#include "zlib.h"

#define STALE_ARENA_TIMEOUT 3000

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>

#ifndef WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

#define RDLOCK() rwl_readlock(&peerLock)
#define WRLOCK() rwl_writelock(&peerLock)
#define RDUNLOCK() rwl_readunlock(&peerLock)
#define WRUNLOCK() rwl_writeunlock(&peerLock)
local rwlock_t peerLock;

local Imodman *mm;
local Inet *net;
local Iconfig *cfg;
local Ilogman *lm;
local Imainloop *ml;
local Ichat *chat;
local Iarenaman *aman;
local Iplayerdata *pd;
local Iredirect *redirect;
local Ipeer *peer;

local int arenaDataKey = -1;
local u32 nextArenaId = 1; // enough to create 100 arena's per second for the next 32 years

struct ArenaData
{
	u32 id;
};

local int PeriodicUpdate(void *);
local int RemoveStaleArenas(void* unused);

local void CleanupPeerArena(PeerArena* peerArena)
{
	LLEnum(&peerArena->players, afree);
	LLEmpty(&peerArena->players);

	afree(peerArena);
}

local void CleanupPeerZone(PeerZone* peerZone)
{
	if (!peerZone)
	{
		return;
	}

	LLEnum(&peerZone->config.arenas, afree);
	LLEmpty(&peerZone->config.arenas);

	LLEnum(&peerZone->config.renamedArenas, afree);
	LLEmpty(&peerZone->config.renamedArenas);

	LLEnum(&peerZone->config.sendDummyArenas, afree);
	LLEmpty(&peerZone->config.sendDummyArenas);

	LLEnum(&peerZone->config.relayArenas, afree);
	LLEmpty(&peerZone->config.relayArenas);

	LLEnum(&peerZone->arenas, (void (*)(const void *)) CleanupPeerArena);
	LLEmpty(&peerZone->arenas);
	HashDeinit(&peerZone->arenaTable);

	afree(peerZone->playerListBuffer);

	afree(peerZone);
}

local void ReadConfig()
{
	ml->ClearTimer(PeriodicUpdate, NULL);
	ml->ClearTimer(RemoveStaleArenas, NULL);

	WRLOCK();

	LLEnum(&peer->peers, (void (*)(const void *)) CleanupPeerZone);
	LLEmpty(&peer->peers);

	// todo: limit packet size to around 1400-1500 bytes (like subgame)
	for (int i = 0; i < 255; ++i)
	{
		char peerSection[8];
		sprintf(peerSection, "Peer%u", i);

		/* cfghelp: Peer0:Address, global, string
		 * Send and receive peer packets to/from this IP address */
		const char *address = cfg->GetStr(GLOBAL, peerSection, "Address");

		/* cfghelp: Peer0:Port, global, int
		 * Send and receive peer packets to/from this UDP port */
		int port = cfg->GetInt(GLOBAL, peerSection, "Port", 0);

		if (!address || !port)
		{
			break;
		}

		u32 hash = 0xDEADBEEF;

		/* cfghelp: Peer0:Password, global, string
		 * Peers must agree upon a common password */
		const char *password = cfg->GetStr(GLOBAL, peerSection, "Password");
		if (password)
		{
			hash = ~crc32(0, (unsigned char *) password, strlen(password));
		}

		PeerZone* peerZone = amalloc(sizeof(PeerZone));
		HashInit(&peerZone->arenaTable);
		peerZone->playerListBufferSize = 512; // initial size

		peerZone->config.id = i;

		peerZone->config.sin.sin_family = AF_INET;
		peerZone->config.sin.sin_port = htons(port);
		if (!inet_aton(address, &peerZone->config.sin.sin_addr))
		{
			lm->Log(L_ERROR, "<peer> Invalid value for %s:Address", peerSection);
			CleanupPeerZone(peerZone);
			continue;
		}

		peerZone->config.passwordHash = hash;

		/* cfghelp: Peer0:SendOnly, global, boolean
		 * If set, we send data to our peer but we reject any that we might receive */
		peerZone->config.sendOnly = !!cfg->GetInt(GLOBAL, peerSection, "SendOnly", 0);

		/* cfghelp: Peer0:SendPlayerList, global, boolean
		 * If set, send a full arena and player list to the peer. Otherwise only send a summary of our population */
		peerZone->config.sendPlayerList = !!cfg->GetInt(GLOBAL, peerSection, "SendPlayerList", 1);

		/* cfghelp: Peer0:SendZeroPlayerCount, global, boolean
		 * If set and SendPlayerList is not set, always send a population count of 0 */
		peerZone->config.sendZeroPlayerCount = !!cfg->GetInt(GLOBAL, peerSection, "SendZeroPlayerCount", 0);

		/* cfghelp: Peer0:SendMessages, global, boolean
		 * If set, forward alert and zone (?z) messages to the peer */
		peerZone->config.sendMessages = !!cfg->GetInt(GLOBAL, peerSection, "SendMessages", 1);

		/* cfghelp: Peer0:ReceiveMessages, global, boolean
		 * If set, display the zone (*zone) and alert messages from this peer */
		peerZone->config.receiveMessages = !!cfg->GetInt(GLOBAL, peerSection, "ReceiveMessages", 1);

		/* cfghelp: Peer0:IncludeInPopulation, global, boolean
		 * If set, include the population count of this peer in the ping protocol. */
		peerZone->config.includeInPopulation  = !!cfg->GetInt(GLOBAL, peerSection, "IncludeInPopulation", 1);

		/* cfghelp: Peer0:ProvidesDefaultArenas, global, boolean
		 * If set, any arena that would normally end up as (default) will be redirected to this peer zone */
		peerZone->config.providesDefaultArenas  = !!cfg->GetInt(GLOBAL, peerSection, "ProvidesDefaultArenas", 0);


		char nameBuf[20];
		const char *tmp = NULL;

		/* cfghelp: Peer0:Arenas, global, string
		* A list of arena's that belong to the peer. This server will redirect players that try to ?go to
		* this arena. These arena's will also be used for ?find and will be shown in ?arena. If you are also
		* using Peer0:RenameArenas, you should put the local arena name here; this is the one you would see
		* in the ?arena list if you are in this zone */
		const char *arenas = cfg->GetStr(GLOBAL, peerSection, "Arenas");
		arenas = arenas ? arenas : "";
		tmp = NULL;
		while (strsplit(arenas, " \t:;,", nameBuf, sizeof(nameBuf), &tmp))
		{
			LLAdd(&peerZone->config.arenas, astrdup(nameBuf));
		}

		/* cfghelp: Peer0:SendDummyArenas, global, string
		* A list of arena's that we send to the peer with a single dummy player. Instead of the full
		* player list. This will keep the arena in the arena list of the peer with a fixed count of 1*/
		const char *sendDummyArenas = cfg->GetStr(GLOBAL, peerSection, "SendDummyArenas");
		sendDummyArenas = sendDummyArenas ? sendDummyArenas : "";
		tmp = NULL;
		while (strsplit(sendDummyArenas, " \t:;,", nameBuf, sizeof(nameBuf), &tmp))
		{
			LLAdd(&peerZone->config.sendDummyArenas, astrdup(nameBuf));
		}

		/* cfghelp: Peer0:RelayArenas, global, string
		* A list of arena's of this peer that will be relayed to other peers. */
		const char *relayArenas = cfg->GetStr(GLOBAL, peerSection, "RelayArenas");
		relayArenas = relayArenas ? relayArenas : "";
		tmp = NULL;
		while (strsplit(relayArenas, " \t:;,", nameBuf, sizeof(nameBuf), &tmp))
		{
			LLAdd(&peerZone->config.relayArenas, astrdup(nameBuf));
		}

		/* cfghelp: Peer0:RenameArenas, global, string
		* A list of arena's that belong to the peer which should be renamed to a different name locally.
		* For example `foo=bar,0=twpublic` will display the remote `foo` arena as `bar` instead */
		const char *renameArenas = cfg->GetStr(GLOBAL, peerSection, "RenameArenas");
		renameArenas = renameArenas ? renameArenas : "";

		PeerArenaName *name = NULL;
		tmp = NULL;
		for (int it = 0; strsplit(renameArenas, " \t:;,=", nameBuf, sizeof(nameBuf), &tmp); ++it)
		{
			// remote=local,remote=local
			if (it % 2 == 0)
			{
				name = amalloc(sizeof(PeerArenaName));
				astrncpy(name->remoteName, nameBuf, 20);
			}
			else
			{
				astrncpy(name->localName, nameBuf, 20);
				name->caseChange = strcasecmp(name->remoteName, name->localName) == 0;
				LLAdd(&peerZone->config.renamedArenas, name);
				name = NULL;
			}
		}

		if (name)
		{
			// unmatched remote=foo
			afree(name);
			name = NULL;
		}

		LLAdd(&peer->peers, peerZone);

		char ipbuf[INET_ADDRSTRLEN];
		inet_ntop(AF_INET, &peerZone->config.sin.sin_addr, ipbuf, INET_ADDRSTRLEN);
		lm->Log(L_INFO, "<peer> zone peer %d at %s:%d (%s)",
			i, ipbuf, port, peerZone->config.sendPlayerList ? "player list" : "player count");
	}

	WRUNLOCK();

	ml->SetTimer(PeriodicUpdate, 100, 100, NULL, NULL);
	ml->SetTimer(RemoveStaleArenas, 1000, 1000, NULL, NULL);
}

local int HasArenaConfigured(PeerZone* peerZone, const char *localName) /* call with lock */
{
	Link *link;
	const char *configuredArenaName;
	const char *arenaName;

	/* explicitly configured */
	FOR_EACH(&peerZone->config.arenas, configuredArenaName, link)
	{
		if (strcasecmp(configuredArenaName, localName) == 0)
		{
			return TRUE;
		}
	}

	if (peerZone->config.providesDefaultArenas)
	{
		/* find the base name. baa5aar123 -> baa5aar */
		size_t n = strlen(localName);
		for (; n > 0 && isdigit(localName[n - 1]); --n);
		if (!n)
		{
			localName = "(public)";
			n = 8;
		}

		aman->Lock();
		FOR_EACH(&aman->known_arena_names, arenaName, link)
		{
			if (strncasecmp(arenaName, localName, n) == 0)
			{
				aman->Unlock();
				return FALSE;
			}
		}
		aman->Unlock();

		return TRUE;
	}

	return FALSE;
}

local int HasArenaConfiguredAsSendDummy(PeerZone* peerZone, const char *localName) /* call with lock */
{
	Link *link;
	const char *configuredArenaName;

	FOR_EACH(&peerZone->config.sendDummyArenas, configuredArenaName, link)
	{
		if (strcasecmp(configuredArenaName, localName) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}

local int HasArenaConfiguredAsRelay(PeerZone* peerZone, const char *localName) /* call with lock */
{
	Link *link;
	const char *configuredArenaName;

	FOR_EACH(&peerZone->config.relayArenas, configuredArenaName, link)
	{
		if (strcasecmp(configuredArenaName, localName) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}

PeerArenaName* FindPeerArenaRename(PeerZone *peerZone, const char *arenaName, bool remote)  /* call with lock */
{
	Link *link;
	PeerArenaName *name;

	FOR_EACH(&peerZone->config.renamedArenas, name, link)
	{
		if (strcasecmp(arenaName, remote ? name->remoteName : name->localName) == 0)
		{
			return name;
		}
	}

	return NULL;
}

local void SendUpdatesToPeer(PeerZone *peerZone)
{
	Arena *arena;
	Link *link;
	ticks_t now = current_ticks();
	u8 playerCountBuffer[sizeof(struct PeerPacket) + 2] = { 0 };
	u8 playerCountZeroBuffer[sizeof(struct PeerPacket) + 2] = { 0 };
	u16 *totalPopulation = (u16*) (playerCountBuffer + sizeof(struct PeerPacket));

	if (!peerZone->playerListBuffer)
	{
		peerZone->playerListBuffer = amalloc(peerZone->playerListBufferSize);
	}

	ListenData *ld = NULL;;
	if (LLGetHead(&net->listening))
	{
		ld = LLGetHead(&net->listening)->data;
	}

	if (!ld)
	{
		return; // try again later
	}

	aman->Lock();

	int total;
	aman->GetPopulationSummary(&total, NULL);
	*totalPopulation = total;

	u8 *buf = peerZone->playerListBuffer + sizeof(struct PeerPacket);

	#define SKIP_PAST_STRING for (; *buf; ++buf); ++buf
	#define ENSURE_CAPACITY(amount) do { \
	    int offset = buf - peerZone->playerListBuffer; \
		while (peerZone->playerListBufferSize - offset < (amount)) \
		{ \
			int oldSize = peerZone->playerListBufferSize; \
			u8* old = peerZone->playerListBuffer; \
			\
			peerZone->playerListBufferSize *= 2; \
			peerZone->playerListBuffer = amalloc(peerZone->playerListBufferSize); \
			memcpy(peerZone->playerListBuffer, old, oldSize); \
			afree(old); \
		} \
		buf = peerZone->playerListBuffer + offset; \
	} while(0)

	// (x86 is little endian)
	FOR_EACH_ARENA(arena)
	{
		struct ArenaData* arenaData = P_ARENA_DATA(arena, arenaDataKey);
		Player *p;
		Link *link;

		if (!arenaData->id)
		{
			arenaData->id = nextArenaId++;
		}

		ENSURE_CAPACITY(4);
		*((u32*) buf) = arenaData->id;
		buf += 4;

		ENSURE_CAPACITY(20);
		astrncpy((char*) buf, arena->name, 20);
		SKIP_PAST_STRING;

		if (HasArenaConfiguredAsSendDummy(peerZone, arena->name))
		{
			ENSURE_CAPACITY(24);
			*buf = ':'; // : is not allowed in real player names
			// use the arena name as the player name to ensure it is unique
			astrncpy( (char*) buf + 1, arena->name, 23);
			SKIP_PAST_STRING;
		}
		else
		{
			pd->Lock();
			FOR_EACH_PLAYER_IN_ARENA(p, arena)
			{
				ENSURE_CAPACITY(24);
				astrncpy((char*) buf, p->name, 24);
				SKIP_PAST_STRING;
			}
			pd->Unlock();
		}

		ENSURE_CAPACITY(1);
		*buf = 0;
		++buf;
	}

	PeerZone *otherPeerZone;
	PeerArena *otherPeerArena;
	FOR_EACH_PEER_ZONE(otherPeerZone)
	{
		if (otherPeerZone == peerZone)
		{
			continue;
		}

		Link *link2;
		FOR_EACH(&otherPeerZone->arenas, otherPeerArena, link2)
		{
			if (!otherPeerArena->relay)
			{
				continue;
			}

#ifdef PEER_PRINT_OUTGOING_RELAY
			printf("%d: %d %s = ", peerZone->config.id, otherPeerArena->localId, otherPeerArena->name.localName);
#endif
			ENSURE_CAPACITY(4);
			*((u32*)buf) = otherPeerArena->localId;
			buf += 4;

			ENSURE_CAPACITY(20);
			astrncpy((char*)buf, otherPeerArena->name.localName, 20);
			SKIP_PAST_STRING;

			if (HasArenaConfiguredAsSendDummy(peerZone, otherPeerArena->name.localName))
			{
#ifdef PEER_PRINT_OUTGOING_RELAY
				printf(":%s ", otherPeerArena->name.localName);
#endif
				ENSURE_CAPACITY(24);
				*buf = ':';
				astrncpy((char*)buf + 1, otherPeerArena->name.localName, 23);
				SKIP_PAST_STRING;
			}
			else
			{
				Link *link3;
				const char *playerName;
				FOR_EACH(&otherPeerArena->players, playerName, link3)
				{
#ifdef PEER_PRINT_OUTGOING_RELAY
					printf("%s ", playerName);
#endif
					ENSURE_CAPACITY(24);
					astrncpy((char*)buf, playerName, 24);
					SKIP_PAST_STRING;
				}
			}

#ifdef PEER_PRINT_OUTGOING_RELAY
			printf("\n");
#endif
			ENSURE_CAPACITY(1);
			*buf = 0;
			++buf;
		}
	}

	int playerListSize = buf - peerZone->playerListBuffer;

	aman->Unlock();


	struct PeerPacket* packetHeader;
	if (peerZone->config.sendPlayerList)
	{
		packetHeader = (struct PeerPacket*) peerZone->playerListBuffer;
	}
	else
	{
		if (peerZone->config.sendZeroPlayerCount)
		{
			packetHeader = (struct PeerPacket*) playerCountZeroBuffer;
		}
		else
		{
			packetHeader = (struct PeerPacket*) playerCountBuffer;
		}
	}

	packetHeader->t1 = 0x00;
	packetHeader->t2 = 0x01;
	packetHeader->password = peerZone->config.passwordHash;
	packetHeader->t3 = 0xFF;
	packetHeader->type = peerZone->config.sendPlayerList ? 0x01 : 0x04;
	packetHeader->timestamp = now;

	if (peerZone->config.sendPlayerList)
	{
		net->ReallyRawSend(&peerZone->config.sin, peerZone->playerListBuffer, playerListSize, ld);
	}
	else
	{
		net->ReallyRawSend(&peerZone->config.sin, playerCountBuffer, sizeof(playerCountBuffer), ld);
	}
}

local int PeriodicUpdate(void* unused)
{
	RDLOCK();
	PeerZone *peerZone;
	Link *link;
	FOR_EACH_PEER_ZONE(peerZone)
	{
		SendUpdatesToPeer(peerZone);
	}
	RDUNLOCK();

	return TRUE; // keep running
}

local int RemoveStaleArenas(void* unused)
{
	ticks_t now = current_ticks();

	WRLOCK();

	Link *link;
	PeerZone *peerZone;

	FOR_EACH_PEER_ZONE(peerZone)
	{
		Link *link2;
		PeerArena *peerArena;

		LinkedList deleting = LL_INITIALIZER;

		FOR_EACH(&peerZone->arenas, peerArena, link2)
		{
			if (TICK_DIFF(now, peerArena->lastUpdate) > STALE_ARENA_TIMEOUT)
			{
				LLAdd(&deleting, peerArena);
			}
		}

		FOR_EACH(&deleting, peerArena, link2)
		{
			LLRemoveAll(&peerZone->arenas, peerArena);
			HashRemove(&peerZone->arenaTable, peerArena->name.remoteName, peerArena);
			afree(peerArena);
		}

		LLEmpty(&deleting);
	}

	WRUNLOCK();

	return TRUE; // keep running
}

PeerZone* FindZone(struct sockaddr_in *sin) /* call with lock */
{
	Link *link;
	PeerZone *peerZone;

	FOR_EACH_PEER_ZONE(peerZone)
	{
		if (ntohl(sin->sin_addr.s_addr) == ntohl(peerZone->config.sin.sin_addr.s_addr) &&
		    ntohs(sin->sin_port) == ntohs(peerZone->config.sin.sin_port))
		{
			return peerZone;
		}
	}

	return NULL;
}

local void SendMessageToPeer(u8 packetType, u8 messageType, const char *message)
{
	ticks_t now = current_ticks();

	u8 messagePacket[sizeof(struct PeerPacket) + 1 + 250];

	messagePacket[sizeof(struct PeerPacket)] = messageType;

	char *line = (char*) (messagePacket + sizeof(struct PeerPacket) + 1);
	astrncpy(line, message, 250);

	size_t messagePacketSize = sizeof(struct PeerPacket) + 1 + strlen(line) + 1;

	ListenData *ld = NULL;;
	if (LLGetHead(&net->listening))
	{
		ld = LLGetHead(&net->listening)->data;
	}

	if (!ld)
	{
		lm->Log(L_ERROR, "<peer> Unable to send message, there are no listening sockets");
		return;
	}

	RDLOCK();

	Link *link;
	PeerZone *peerZone;
	FOR_EACH_PEER_ZONE(peerZone)
	{
		if (!peerZone->config.sendMessages)
		{
			continue;
		}
		struct PeerPacket* packetHeader = (struct PeerPacket*) messagePacket;

		packetHeader->t1 = 0x00;
		packetHeader->t2 = 0x01;
		packetHeader->password = peerZone->config.passwordHash;
		packetHeader->t3 = 0xFF;
		packetHeader->type = packetType;
		packetHeader->timestamp = now;

		net->ReallyRawSend(&peerZone->config.sin, messagePacket, messagePacketSize, ld);
	}

	RDUNLOCK();
}

local int WalkPastNil(u8 **payload, int *len)
{
	while (**payload)
	{
		++(*payload);
		--(*len);
		if (*len < 1)
		{
			return FALSE;
		}
	}

	++(*payload);
	--(*len);

	return TRUE;
}

local void HandlePlayerList(PeerZone* peerZone, u8 *payloadStart, int payloadLen) /* call with lock */
{
	char ipbuf[INET_ADDRSTRLEN];
	int port = ntohs(peerZone->config.sin.sin_port);
	inet_ntop(AF_INET, &peerZone->config.sin.sin_addr, ipbuf, INET_ADDRSTRLEN);

	ticks_t now = current_ticks();

	u8 *payload = payloadStart;
	int len = payloadLen;

	peerZone->playerCount = -1;

	while (len >= 5)
	{
		u32 id = * (u32*) payload;
		payload += 4;
		len -= 4;

		const char *remoteNamePayload = (const char *) payload;
		if (!WalkPastNil(&payload, &len)) // unterminated string?
		{
			lm->Log(L_WARN, "<peer> zone peer %s:%d sent us a player list with an unterminated arena name. %d",
				ipbuf, port, payloadLen);
			break;
		}

		char remoteName[20];
		astrncpy(remoteName, remoteNamePayload, sizeof(remoteName));
		for (char *c = remoteName; *c; ++c)
		{
			// lower case remote arena name (just like normal arena's in asss)
			// this ensures proper sorting in continuum.
			// note: _renamed_ local arena names are kept in the character casing
			// of the config specification (PeerX:RenameArenas)
			*c = tolower(*c);
		}

		// if this arena exists as a rename target (the bar in foo=bar), ignore it
		// except if the rename is just a difference in the character case (foo=FOO)
		PeerArenaName *rename = FindPeerArenaRename(peerZone, remoteName, false);
		if (rename && !rename->caseChange)
		{
			while (len > 0)
			{
				if (!*payload)
				{
					++payload;
					--len;
					break; // end of player list
				}

				if (!WalkPastNil(&payload, &len)) // unterminated string?
				{
					lm->Log(L_WARN, "<peer> zone peer %s:%d sent us a player list with an unterminated player name. %d",
						ipbuf, port, payloadLen);
					return;
				}
			}

			// handle next arena
			continue;
		}

		PeerArena* peerArena = HashGetOne(&peerZone->arenaTable, remoteName);

		if (!peerArena)
		{
			peerArena = amalloc(sizeof(PeerArena));
			peerArena->localId = nextArenaId++;
			PeerArenaName *name = FindPeerArenaRename(peerZone, remoteName, true);
			if (name)
			{
				peerArena->name = *name;
			}
			else
			{
				name = &peerArena->name;
				astrncpy(name->remoteName, remoteName, sizeof(name->remoteName));
				astrncpy(name->localName, remoteName, sizeof(name->localName));
				name->caseChange = name->caseChange;
			}
			
			LLAdd(&peerZone->arenas, peerArena);
			HashAdd(&peerZone->arenaTable, peerArena->name.remoteName, peerArena);
		}

		peerArena->id = id;
		peerArena->configured = HasArenaConfigured(peerZone, peerArena->name.localName);
		peerArena->relay = HasArenaConfiguredAsRelay(peerZone, peerArena->name.localName);
		peerArena->playerCount = 0;
		peerArena->lastUpdate = now;

		LLEnum(&peerArena->players, afree);
		LLEmpty(&peerArena->players);

		while (len > 0)
		{
			if (!*payload)
			{
				++payload;
				--len;
				break; // end of player list: player\0player\0\0
			}

			const char* player = (const char*) payload;
			if (!WalkPastNil(&payload, &len)) // unterminated string?
			{
				lm->Log(L_WARN, "<peer> zone peer %s:%d sent us a player list with an unterminated player name. %d",
					ipbuf, port, payloadLen);
				return;
			}

			++peerArena->playerCount;
			LLAdd(&peerArena->players, astrdup(player));
		}
	}
}

local void HandleMessage(PeerZone* peerZone, u8 packetType, byte *payload, int len) /* call with lock */
{
	if (len < 2 || !peerZone->config.receiveMessages)
	{
		return;
	}

	payload[len - 1] = 0;
	u8 messageType = * (u8*) payload;
	const char *line = (const char*) payload + 1;

	char ipbuf[INET_ADDRSTRLEN];
	int port = ntohs(peerZone->config.sin.sin_port);
	inet_ntop(AF_INET, &peerZone->config.sin.sin_addr, ipbuf, INET_ADDRSTRLEN);

	switch (packetType)
	{
		case 0x02: // zone
			lm->Log(L_INFO, "<peer> zone peer %s:%d sent us a zone message (type=%x): %s",
				ipbuf, port, messageType, line);

			chat->SendArenaMessage(ALLARENAS, "%s", line);
			break;
		case 0x03: // alert
			lm->Log(L_INFO, "<peer> zone peer %s:%d sent us a alert message (type=%x): %s",
				ipbuf, port, messageType, line);

			chat->SendModMessage("%s", line);
			break;
	}
}

local void HandlePlayerCount(PeerZone* peerZone, u8 *payload, int len) /* call with lock */
{
	if (len < 2)
	{
		return;
	}

	LLEnum(&peerZone->arenas, (void (*)(const void *)) CleanupPeerArena);
	LLEmpty(&peerZone->arenas);
	HashDeinit(&peerZone->arenaTable);
	HashInit(&peerZone->arenaTable);

	peerZone->playerCount = *(u16*)(payload);
}

local void PeerConnInitCB(struct sockaddr_in *sin, u8 *pkt, int len, ListenData *ld)
{
	if (len < 12 || pkt[0] != 0x00 || pkt[1] != 0x01 || pkt[6] != 0xFF)
	{
		return; // not the peer protocol
	}

	struct PeerPacket* packetHeader = (struct PeerPacket*) pkt;

	#define FAIL(msg) do { \
		int port = ntohs(sin->sin_port); \
		char ipbuf[INET_ADDRSTRLEN]; \
		inet_ntop(AF_INET, &sin->sin_addr, ipbuf, INET_ADDRSTRLEN); \
		lm->Log(L_DRIVEL, "<peer> received something that looks like peer data at %s:%d, " msg, ipbuf, port); \
	} while(0)


	WRLOCK();
	PeerZone* peerZone = FindZone(sin);
	if (!peerZone)
	{
		WRUNLOCK();
		FAIL("however this address has not been configured");
		return;
	}

	if (peerZone->config.passwordHash != packetHeader->password)
	{
		WRUNLOCK();
		FAIL("however the password is incorrect");
		return;
	}

	if (peerZone->config.sendOnly)
	{
		WRUNLOCK();
		FAIL("however this peer is configured as SendOnly");
		return;
	}

	if (peerZone->timestamps[packetHeader->timestamp & 0xFF] == packetHeader->timestamp)
	{
		// already received this packet
		WRUNLOCK();
		return;
	}

	peerZone->timestamps[packetHeader->timestamp & 0xFF] = packetHeader->timestamp;

	u8* payload = pkt + sizeof(struct PeerPacket);
	int payloadLen = len - sizeof(struct PeerPacket);

	switch (packetHeader->type)
	{
		case 0x1:
			HandlePlayerList(peerZone, payload, payloadLen);
			break;
		case 0x2:
		case 0x3:
			HandleMessage(peerZone, packetHeader->type, payload, payloadLen);
			break;
		case 0x4:
			HandlePlayerCount(peerZone, payload, payloadLen);
			break;
	}

	WRUNLOCK();
}

local int GetPopulationSummary()
{
	RDLOCK();
	Link *link;
	int total = 0;
	PeerZone *peerZone;

	FOR_EACH_PEER_ZONE(peerZone)
	{
		PeerArena *peerArena;
		Link *link2;

		if (!peerZone->config.includeInPopulation)
		{
			continue;
		}

		if (peerZone->playerCount >= 0) // player count packet
		{
			total += peerZone->playerCount;
		}
		else // player list packet
		{
			FOR_EACH(&peerZone->arenas, peerArena, link2)
			{
				total += peerArena->playerCount;
			}
		}

	}
	RDUNLOCK();

	return total;
}

local int FindPlayer(const char *findName, int *score, char *name, char *arena, int bufLen)
{
	if (!findName) { findName = ""; }

	if (bufLen > 0)
	{
		if (name) { *name = 0; }
		if (arena) { *arena = 0; }
	}

	RDLOCK();

	int hasMatch = FALSE;
	const char *bestPlayerName = NULL;
	PeerArena *bestPeerArena = NULL;

	Link *link;
	PeerZone *peerZone;
	FOR_EACH_PEER_ZONE(peerZone)
	{
		Link *link2;
		PeerArena *peerArena;
		FOR_EACH(&peerZone->arenas, peerArena, link2)
		{
			if (!peerArena->configured)
			{
				continue;
			}

			Link *link3;
			const char *playerName;
			FOR_EACH(&peerArena->players, playerName, link3)
			{
				if (strcasecmp(playerName, findName) == 0)
				{
					/* exact mach */
					bestPlayerName = playerName;
					bestPeerArena = peerArena;
					*score = 0;
					hasMatch = TRUE;
					goto EXIT_OUTER_LOOP;
				}

				const char* pos = strcasestr(playerName, findName);
				if (pos)
				{
					int newScore = pos - playerName;
					if (newScore < *score)
					{
						bestPlayerName = playerName;
						bestPeerArena = peerArena;
						*score = newScore;
						hasMatch = TRUE;
					}
				}
			}
		}
	}
EXIT_OUTER_LOOP:

	if (name && bestPlayerName) { astrncpy(name, bestPlayerName, bufLen); }
	if (arena && bestPeerArena) { astrncpy(arena, bestPeerArena->name.localName, bufLen); }

	RDUNLOCK();

	return hasMatch;
}

local int ArenaRequest(Player *p, int arenaType, const char *arenaName)
{
	RDLOCK();

	Link *link;
	PeerZone *peerZone;

	FOR_EACH_PEER_ZONE(peerZone)
	{
		PeerArenaName *name = FindPeerArenaRename(peerZone, arenaName, false);

		if (HasArenaConfigured(peerZone, arenaName))
		{
			Target t;
			t.type = T_PLAYER;
			t.u.p = p;

			char ipbuf[INET_ADDRSTRLEN];
			int port = ntohs(peerZone->config.sin.sin_port);
			inet_ntop(AF_INET, &peerZone->config.sin.sin_addr, ipbuf, INET_ADDRSTRLEN);

			RDUNLOCK();
			const char *targetArena = (name ? name->remoteName : arenaName);
			if (targetArena[0] == '0' && targetArena[1] == '\0')
			{
				// workaround for subgame
				targetArena = "";
			}
			lm->LogP(L_INFO, "peer", p, "Redirecting to %s:%d : \"%s\"", ipbuf, port, targetArena);
			return redirect->RawRedirect(&t, ipbuf, port, arenaType, targetArena);
		}
	}

	RDUNLOCK();
	return 0;
}

local void SendZoneMessage(const char *format, ...)
{
	va_list args;
	char line[250];

	va_start(args, format);

	if (vsnprintf(line, 250, format, args) < 0)
	{
		lm->Log(L_ERROR, "<peer> Invalid format string in SendZoneMessage: %s", format);
		va_end(args);
		return;
	}

	SendMessageToPeer(0x02, 0x00, line);
	va_end(args);
}

local void SendAlertMessage(const char *alertName, const char *playerName, const char *arenaName, const char *format, ...)
{
	va_list args;
	char line[250];
	char line2[250];
	bool isPublic;
	const char *c;

	va_start(args, format);

	if (vsnprintf(line, 250, format, args) < 0)
	{
		lm->Log(L_ERROR, "<peer> Invalid format string in SendAlertMessage: %s", format);
		va_end(args);
		return;
	}
	line[249] = 0;

	isPublic = true;
	for (c = arenaName; *c; ++c)
	{
		if (!isdigit(*c))
		{
			isPublic = false;
			break;
		}
	}

	sprintf(
		line2, 
		isPublic ? "%s: (%s) (Public %s): %s" : "%s: (%s) (%s): %s",
		alertName, playerName, arenaName, line);
	line2[249] = 0;

	SendMessageToPeer(0x03, 0x00, line2);
	va_end(args);
}

local void Lock()
{
	RDLOCK();
}

local void Unlock()
{
	RDUNLOCK();
}

PeerArena* FindArena(const char *arenaName, bool remote) /* call with lock */
{
	Link *link;
	PeerZone *peerZone;

	FOR_EACH_PEER_ZONE(peerZone)
	{
		Link *link2;
		PeerArena *peerArena;

		FOR_EACH(&peerZone->arenas, peerArena, link2)
		{
			if (strcasecmp(arenaName, remote ? peerArena->name.remoteName : peerArena->name.localName) == 0)
			{
				return peerArena;
			}
		}
	}

	return NULL;
}

local Ipeer myint =
{
	INTERFACE_HEAD_INIT(I_PEER, "peer")
	GetPopulationSummary,
	FindPlayer,
	ArenaRequest,
	SendZoneMessage,
	SendAlertMessage,
	Lock, Unlock,
	FindZone, FindArena,
	LL_INITIALIZER
};

local void ReleaseInterfaces()
{
	mm->ReleaseInterface(redirect);
	mm->ReleaseInterface(chat);
	mm->ReleaseInterface(cfg);
	mm->ReleaseInterface(net);
	mm->ReleaseInterface(pd);
	mm->ReleaseInterface(aman);
	mm->ReleaseInterface(ml);
	mm->ReleaseInterface(lm);
}

EXPORT const char info_peer[] = CORE_MOD_INFO("peer");

EXPORT int MM_peer(int action, Imodman *mm_, Arena *arena)
{
	switch(action)
	{
		case MM_LOAD:
			mm = mm_;
			peer = &myint;
			lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
			ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
			aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
			pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
			net = mm->GetInterface(I_NET, ALLARENAS);
			cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
			chat = mm->GetInterface(I_CHAT, ALLARENAS);
			redirect = mm->GetInterface(I_REDIRECT, ALLARENAS);

			if (!lm || !ml || !aman || !pd || !net || !cfg || !chat || !redirect)
			{
				printf("<peer> Missing interfaces\n");
				ReleaseInterfaces();
				return MM_FAIL;
			}

			arenaDataKey = aman->AllocateArenaData(sizeof(struct ArenaData));
			if (arenaDataKey == -1)
			{
				ReleaseInterfaces();
				return MM_FAIL;
			}

			mm->RegCallback(CB_CONNINIT, PeerConnInitCB, ALLARENAS);
			mm->RegInterface(&myint, ALLARENAS);

			rwl_init(&peerLock);

			ReadConfig();

			return MM_OK;

		case MM_UNLOAD:
			if (mm->UnregInterface(&myint, ALLARENAS))
			{
				return MM_FAIL;
			}

			ml->ClearTimer(PeriodicUpdate, NULL);
			ml->ClearTimer(RemoveStaleArenas, NULL);
			mm->UnregCallback(CB_CONNINIT, PeerConnInitCB, ALLARENAS);

			aman->FreeArenaData(arenaDataKey);

			WRLOCK();
			LLEnum(&peer->peers, (void (*)(const void *)) CleanupPeerZone);
			LLEmpty(&peer->peers);
			WRUNLOCK();

			ReleaseInterfaces();

			rwl_destroy(&peerLock);

			return MM_OK;
	}

	return MM_FAIL;
}
