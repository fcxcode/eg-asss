
/* dist: public */

#include <stdio.h>

#include "asss.h"

local Iconfig *cfg;
local Ilogman *lm;
local Iplayerdata *pd;

local shipmask_t GetAllowableShips(Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	int i;
	Link *link;
	Player *x;
	int count[8] = {0, 0, 0, 0, 0, 0, 0, 0};

	shipmask_t mask = SHIPMASK_NONE;

	pd->Lock();
	FOR_EACH_PLAYER_IN_ARENA(x, p->arena)
	{
		if (x->p_freq != freq)
			continue;
		if (x->p_ship == SHIP_SPEC)
			continue;
		if (x == p)
			continue;
		++count[x->p_ship];
	}
	pd->Unlock();

	for (i = 0; i < SHIP_SPEC; ++i)
	{
		/* cfghelp: All:LimitPerTeam, arena, int, def: -1, mod: enf_shipcount
		 * The maximum number of this ship on any given frequency. -1 means no limit. */
		int limit = cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[i], "LimitPerTeam", -1);

		if (limit == -1)
		{
			mask |= (1 << i);
			continue;
		}

		if (count[i] < limit)
		{
			mask |= (1 << i);
		}
		else
		{
			if (ship == i && err_buf)
			{
				if (limit > 0)
					snprintf(err_buf, buf_len, "There are already the maximum number of ship-%i pilots on your team.", i+1);
				else
					snprintf(err_buf, buf_len, "No ship-%is are allowed in this arena.", i+1);
			}
		}
	}

	return mask;
}

local Aenforcer enforceradv =
{
	ADVISER_HEAD_INIT(A_ENFORCER)
	GetAllowableShips, NULL, NULL, NULL
};

EXPORT const char info_enf_shipcount[] = CORE_MOD_INFO("enf_shipcount");

EXPORT int MM_enf_shipcount(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!lm)
			return MM_FAIL;

		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!cfg)
		{
			lm->Log(L_ERROR, "<enf_shipcount> unable to get cfg interface %s", I_CONFIG);
			mm->ReleaseInterface(lm);
			return MM_FAIL;
		}

		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!pd)
		{
			lm->Log(L_ERROR, "<enf_shipcount> unable to get pd interface %s", I_PLAYERDATA);
			mm->ReleaseInterface(lm);
			mm->ReleaseInterface(cfg);
			return MM_FAIL;
		}

		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(pd);

		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegAdviser(&enforceradv, arena);

		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregAdviser(&enforceradv, arena);

		return MM_OK;
	}

	return MM_FAIL;
}

