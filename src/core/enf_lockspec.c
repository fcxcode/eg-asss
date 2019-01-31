
/* dist: public */

#include <stdio.h>

#include "asss.h"

local shipmask_t GetAllowableShips(Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	if (err_buf)
	{
		snprintf(err_buf, buf_len, "This arena does not allow players to leave spectator mode.");
	}

	return 0; // allow only spec
}

local Aenforcer my_adv =
{
	ADVISER_HEAD_INIT(A_ENFORCER)
	GetAllowableShips, NULL, NULL, NULL
};

EXPORT const char info_enf_lockspec[] = CORE_MOD_INFO("enf_lockspec");

EXPORT int MM_enf_lockspec(int action, Imodman *mm, Arena *arena)
{
	if (action == MM_LOAD)
	{
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		return MM_OK;
	}
	else if (action == MM_ATTACH)
	{
		mm->RegAdviser(&my_adv, arena);
		return MM_OK;
	}
	else if (action == MM_DETACH)
	{
		mm->UnregAdviser(&my_adv, arena);
		return MM_OK;
	}
	return MM_FAIL;
}

