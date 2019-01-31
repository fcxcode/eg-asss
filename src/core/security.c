
#include <stdlib.h>
#include <stdio.h>
#ifndef WIN32
#include <dirent.h>
#else
#include <io.h>
#endif

#include "asss.h"
#include "clientset.h"
#include "packets/security.h"

struct adata
{
	/* shared checksums */
	u32 mapsum;
};

struct pdata
{
	/* whether we went a security request or not */
	int sent;
	/* individual checksums */
	u32 settingsum;
};


/* interface pointers */

local Imodman *mm;
local Iplayerdata *pd;
local Iarenaman *aman;
local Inet *net;
local Iconfig *cfg;
local Imainloop *ml;
local Ilogman *lm;
local Iclientset *clientset;
local Imapdata *mapdata;
local Icapman *capman;
local Ilagcollect *lagc;
local Iprng *prng;

local int cfg_kickoff;
local int adkey, pdkey;
local struct S2CSecurity packet;
local u32 contexechecksum, vieexechecksum;

#define SCRTY_SIZE 1000

local u32 scrty[SCRTY_SIZE];
local int has_scrty;


local pthread_mutex_t secmtx = PTHREAD_MUTEX_INITIALIZER;
#define LOCK() pthread_mutex_lock(&secmtx)
#define UNLOCK() pthread_mutex_unlock(&secmtx)


local void kick_off(Player *p)
{
	if (cfg_kickoff)
	{
		if (!capman->HasCapability(p, CAP_BYPASS_SECURITY))
		{
			lm->LogP(L_INFO, "security", p, "kicking off due to security violation");
			pd->KickPlayer(p);
		}
	}
}


local void p_sec_response(Player *p, byte *data, int len)
{
	int ok = 0, needkick = 0;
	Arena *arena = p->arena;
	struct adata *adata;
	struct pdata *pdata;
	struct C2SSecurity *pkt = (struct C2SSecurity*)data;

	if (len < 0 || (size_t) len < sizeof(struct C2SSecurity))
	{
		if (!capman->HasCapability(p, CAP_SUPRESS_SECURITY))
		{
			lm->LogP(L_MALICIOUS, "security", p, "bad packet len=%i", len);
		}
		return;
	}

	if (!arena)
	{
		if (!capman->HasCapability(p, CAP_SUPRESS_SECURITY))
		{
			lm->LogP(L_MALICIOUS, "security", p, "security response from bad arena");
		}
		return;
	}

	lm->LogP(L_DRIVEL, "security", p, "received security response");

	LOCK();

	pdata = PPDATA(p, pdkey);
	adata = P_ARENA_DATA(arena, adkey);

	pdata->sent = 0;

	if (pdata->settingsum != 0 && pkt->settingchecksum != pdata->settingsum)
	{
		if (!capman->HasCapability(p, CAP_SUPRESS_SECURITY))
		{
			lm->LogP(L_MALICIOUS, "security", p, "settings checksum mismatch");
		}
		needkick = 1;
	}

	if (adata->mapsum != 0 && pkt->mapchecksum != adata->mapsum)
	{
		if (!capman->HasCapability(p, CAP_SUPRESS_SECURITY))
		{
			lm->LogP(L_MALICIOUS, "security", p, "map checksum mismatch");
		}
		needkick = 1;
	}

	if (p->type == T_VIE)
	{
		if (vieexechecksum == pkt->exechecksum)
		{
			ok = 1;
		}
	}
	else if (p->type == T_CONT)
	{
		if (contexechecksum != 0)
		{
			if (contexechecksum == pkt->exechecksum)
			{
				ok = 1;
			}
		}
		else
		{
			ok = 1;
		}
	}
	else
	{
		ok = 1;
	}

	if (!ok)
	{
		if (!capman->HasCapability(p, CAP_SUPRESS_SECURITY))
		{
			lm->LogP(L_MALICIOUS, "security", p, "exe checksum mismatch");
		}
		needkick = 1;
	}

	if (needkick)
		kick_off(p);

	UNLOCK();

	if (lagc)
	{
		/* now submit info to the lag data collecter */
		struct ClientLatencyData cld;
#define A(f) cld.f = pkt->f
		A(weaponcount);
		A(s2cslowtotal); A(s2cfasttotal); A(s2cslowcurrent); A(s2cfastcurrent);
		A(unknown1);
		A(lastping); A(averageping); A(lowestping); A(highestping);
#undef A
		lagc->ClientLatency(p, &cld);
	}
}


local u32 get_vie_exe_checksum(u32 key)
{
	u32 part, sum = 0;

	part = 0xc98ed41f;
	part += 0x3e1bc | key;
	part ^= 0x42435942 ^ key;
	part += 0x1d895300 | key;
	part ^= 0x6b5c4032 ^ key;
	part += 0x467e44 | key;
	part ^= 0x516c7eda ^ key;
	part += 0x8b0c708b | key;
	part ^= 0x6b3e3429 ^ key;
	part += 0x560674c9 | key;
	part ^= 0xf4e6b721 ^ key;
	part += 0xe90cc483 | key;
	part ^= 0x80ece15a ^ key;
	part += 0x728bce33 | key;
	part ^= 0x1fc5d1e6 ^ key;
	part += 0x8b0c518b | key;
	part ^= 0x24f1a96e ^ key;
	part += 0x30ae0c1 | key;
	part ^= 0x8858741b ^ key;
	sum += part;

	part = 0x9c15857d;
	part += 0x424448b | key;
	part ^= 0xcd0455ee ^ key;
	part += 0x727 | key;
	part ^= 0x8d7f29cd ^ key;
	sum += part;

	part = 0x824b9278;
	part += 0x6590 | key;
	part ^= 0x8e16169a ^ key;
	part += 0x8b524914 | key;
	part ^= 0x82dce03a ^ key;
	part += 0xfa83d733 | key;
	part ^= 0xb0955349 ^ key;
	part += 0xe8000003 | key;
	part ^= 0x7cfe3604 ^ key;
	sum += part;

	part = 0xe3f8d2af;
	part += 0x2de85024 | key;
	part ^= 0xbed0296b ^ key;
	part += 0x587501f8 | key;
	part ^= 0xada70f65 ^ key;
	sum += part;

	part = 0xcb54d8a0;
	part += 0xf000001 | key;
	part ^= 0x330f19ff ^ key;
	part += 0x909090c3 | key;
	part ^= 0xd20f9f9f ^ key;
	part += 0x53004add | key;
	part ^= 0x5d81256b ^ key;
	part += 0x8b004b65 | key;
	part ^= 0xa5312749 ^ key;
	part += 0xb8004b67 | key;
	part ^= 0x8adf8fb1 ^ key;
	part += 0x8901e283 | key;
	part ^= 0x8ec94507 ^ key;
	part += 0x89d23300 | key;
	part ^= 0x1ff8e1dc ^ key;
	part += 0x108a004a | key;
	part ^= 0xc73d6304 ^ key;
	part += 0x43d2d3 | key;
	part ^= 0x6f78e4ff ^ key;
	sum += part;

	part = 0x45c23f9;
	part += 0x47d86097 | key;
	part ^= 0x7cb588bd ^ key;
	part += 0x9286 | key;
	part ^= 0x21d700f8 ^ key;
	part += 0xdf8e0fd9 | key;
	part ^= 0x42796c9e ^ key;
	part += 0x8b000003 | key;
	part ^= 0x3ad32a21 ^ key;
	sum += part;

	part = 0xb229a3d0;
	part += 0x47d708 | key;
	part ^= 0x10b0a91 ^ key;
	sum += part;

	part = 0x466e55a7;
	part += 0xc7880d8b | key;
	part ^= 0x44ce7067 ^ key;
	part += 0xe4 | key;
	part ^= 0x923a6d44 ^ key;
	part += 0x640047d6 | key;
	part ^= 0xa62d606c ^ key;
	part += 0x2bd1f7ae | key;
	part ^= 0x2f5621fb ^ key;
	part += 0x8b0f74ff | key;
	part ^= 0x2928b332;
	sum += part;

	sum += 0x62cf369a;

	return sum;
}


/* call with lock held */
local void switch_checksums(void)
{
	Arena *arena;
	struct adata *adata;
	Link *link;

	/* make new seeds */
	packet.greenseed = prng->Get32();
	packet.doorseed = prng->Get32();
	packet.timestamp = current_ticks();

	if (has_scrty)
	{
		/* the layout of scrty is
		 * u32 zero; u32 cont_code_checksum;
		 * struct { u32 seed; u32 checksum; } stuff[SCRTY_SIZE/2-1];
		 */
		int i = prng->Number(1, SCRTY_SIZE/2 - 1) * 2;
		packet.key = scrty[i];
		contexechecksum = scrty[i+1];
	}
	else
	{
		packet.key = prng->Get32();
		contexechecksum = 0;
	}

	/* calculate new checksums */
	aman->Lock();
	FOR_EACH_ARENA_P(arena, adata, adkey)
	{
		if (arena->status == ARENA_RUNNING)
		{
			adata->mapsum = mapdata->GetChecksum(arena, packet.key);
		}
		else
		{
			adata->mapsum = 0;
		}
	}
	aman->Unlock();

	vieexechecksum = get_vie_exe_checksum(packet.key);
}


local int check_timer(void *dummy)
{
	Player *p;
	struct pdata *pdata;
	Link *link;

	LOCK();

	pd->Lock();
	FOR_EACH_PLAYER_P(p, pdata, pdkey)
	{
		if (pdata->sent)
		{
			/* this person didn't return his security packet */
			if (!capman->HasCapability(p, CAP_SUPRESS_SECURITY))
			{
				lm->LogP(L_MALICIOUS, "security", p, "security packet not returned");
			}
			kick_off(p);
		}
	}
	pd->Unlock();

	UNLOCK();

	/* request not to run again */
	return FALSE;
}


local int send_timer(void *dummy)
{
	Player *p;
	struct pdata *pdata;
	Link *link;
	LinkedList reqset = LL_INITIALIZER;
	int count = 0;

	LOCK();

	switch_checksums();

	pd->Lock();
	FOR_EACH_PLAYER_P(p, pdata, pdkey)
	{
		if (p->status == S_PLAYING &&
		    IS_STANDARD(p) &&
		    p->flags.sent_ppk)
		{
			/* if he's sent a ppk it means he's got the map and
			 * settings */
			LLAdd(&reqset, p);
			++count;
			pdata->settingsum = clientset->GetChecksum(p, packet.key);
			pdata->sent = TRUE;
		}
		else
		{
			pdata->sent = FALSE;
		}
	}
	pd->Unlock();

	/* send requests first */
	net->SendToSet(&reqset, (byte*)&packet, sizeof(packet), NET_RELIABLE);

	UNLOCK();

	LLEmpty(&reqset);

	lm->Log(L_DRIVEL, "<security> sent security packet to %d players: green=%lx, door=%lx, timestamp=%lu",
		count,
		(long unsigned) packet.greenseed,
		(long unsigned) packet.doorseed,
		(long unsigned) packet.timestamp );

	/* set the check func to run in 15 seconds */
	ml->SetTimer(check_timer, 1500, 1500, NULL, NULL);

	return TRUE;
}


local void paction(Player *p, int action, Arena *arena)
{
	LOCK();
	if (action == PA_ENTERARENA)
	{
		/* send seeds, but not request checksum */
		lm->LogP(L_DRIVEL, "security", p, "send seeds: green=%lx, door=%lx, timestamp=%lu",
			(long unsigned) packet.greenseed,
			(long unsigned) packet.doorseed,
			(long unsigned) packet.timestamp );

		u32 key = packet.key;
		packet.key = 0;
		net->SendToOne(p, (byte*)&packet, sizeof(packet), NET_RELIABLE);
		packet.key = key;
	}
	else if (action == PA_LEAVEARENA)
	{
		/* reset sent so we don't expect a response from him */
		struct pdata *pdata = PPDATA(p, pdkey);
		pdata->sent = FALSE;
	}
	UNLOCK();
}


local void load_scrty(void)
{
	FILE *f;

	has_scrty = 0;

	if ((f = fopen("scrty", "rb")))
	{
		if (fread(scrty, 1, sizeof(scrty), f) == sizeof(scrty))
		{
			has_scrty = 1;
		}
		fclose(f);
	}
}

local void release_interfaces(void)
{
	mm->ReleaseInterface(pd);
	mm->ReleaseInterface(aman);
	mm->ReleaseInterface(net);
	mm->ReleaseInterface(cfg);
	mm->ReleaseInterface(ml);
	mm->ReleaseInterface(lm);
	mm->ReleaseInterface(clientset);
	mm->ReleaseInterface(mapdata);
	mm->ReleaseInterface(capman);
	mm->ReleaseInterface(lagc);
	mm->ReleaseInterface(prng);
}

EXPORT const char info_security[] = CORE_MOD_INFO("security");

EXPORT int MM_security(int action, Imodman *mm_, Arena *arena)
{
	if (action == MM_LOAD)
	{
		mm = mm_;

		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		net = mm->GetInterface(I_NET, ALLARENAS);
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		clientset = mm->GetInterface(I_CLIENTSET, ALLARENAS);
		mapdata = mm->GetInterface(I_MAPDATA, ALLARENAS);
		capman = mm->GetInterface(I_CAPMAN, ALLARENAS);
		lagc = mm->GetInterface(I_LAGCOLLECT, ALLARENAS);
		prng = mm->GetInterface(I_PRNG, ALLARENAS);

		if (!pd || !aman || !net || !cfg || !ml || !lm || !clientset ||
		    !mapdata || !capman || !lagc || !prng)
		{
			printf("<security> Missing interface(s)\n");
			release_interfaces();
			return MM_FAIL;
		}

		adkey = aman->AllocateArenaData(sizeof(struct adata));
		if (adkey == -1)
		{
			printf("<security> AllocateArenaData failed\n");
			release_interfaces();
			return MM_FAIL;
		}

		pdkey = pd->AllocatePlayerData(sizeof(struct pdata));
		if (pdkey == -1)
		{
			printf("<security> AllocatePlayerData failed\n");
			aman->FreeArenaData(adkey);
			release_interfaces();
			return MM_FAIL;
		}

		load_scrty();

		/* cfghelp: Security:SecurityKickoff, global, bool, def: 0
		 * Whether to kick players off of the server for violating
		 * security checks. */
		cfg_kickoff = cfg->GetInt(GLOBAL, "Security", "SecurityKickoff", 0);

		packet.type = S2C_SECURITY;
		switch_checksums();

		mm->RegCallback(CB_PLAYERACTION, paction, ALLARENAS);

		ml->SetTimer(send_timer, 2500, 6000, NULL, NULL);

		net->AddPacket(C2S_SECURITYRESPONSE, p_sec_response);

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		net->RemovePacket(C2S_SECURITYRESPONSE, p_sec_response);
		ml->ClearTimer(send_timer, NULL);
		ml->ClearTimer(check_timer, NULL);
		mm->UnregCallback(CB_PLAYERACTION, paction, ALLARENAS);
		aman->FreeArenaData(adkey);
		pd->FreePlayerData(pdkey);
		release_interfaces();
		return MM_OK;
	}
	else
	{
		return MM_FAIL;
	}
}

