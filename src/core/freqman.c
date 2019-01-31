
/* dist: public */

#include "asss.h"

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
typedef struct Freq 
{
	LinkedList players;
	int freqnum;
	int is_required;
	int is_remembered;
	int is_balanced_against;
} Freq;


local Imodman *mm;
local Iconfig *cfg = 0;
local Igame *game = 0;
local Ilogman *lm = 0;
local Iarenaman *aman = 0;
local Iplayerdata *pd = 0;

local void refresh_freq(Arena *arena, Freq *freq);
local Freq * create_freq(Arena *arena, int freqnum);
local Freq * get_freq(Arena *arena, int freqnum);
local void prune_freqs(Arena *arena);
local void freq_free_enum(const void *_freq);
local void update_freqs(Arena *arena, Player *p, int newFreqnum, int oldFreqnum);
local void add_player_to_freq(Player *p, Freq *freq);
local void remove_player_from_freq(Arena *arena, Player *p, Freq *freq);
local shipmask_t enforcers_get_allowable_ships(Arena *arena, Player *p, int ship, int freq, char *err_buf, int buf_len);
local int enforcers_can_change_to_freq(Arena *arena, Player *p, int new_freq, char *err_buf, int buf_len);
local int enforcers_can_enter_game(Arena *arena, Player *p, char *err_buf, int buf_len);
local int enforcers_is_unlocked(Arena *arena, Player *p, char *err_buf, int buf_len);
local int get_player_metric(Player *p, Ibalancer *balancer);
local int get_freq_metric(Freq *freq, Ibalancer *balancer);
local int find_entry_freq(Arena *arena, Player *p, char *err_buf, int buf_len);
local void Initial(Player *p, int *ship, int *freq);
local void ShipChange(Player *p, int workingShip, char *err_buf, int buf_len);
local void FreqChange(Player *p, int requestedFreqnum, char *err_buf, int buf_len);
local int default_GetPlayerMetric(Player *p);
local int default_GetMaxMetric(Arena *arena, int freqnum);
local int default_GetMaximumDifference(Arena *arena, int freqnum1, int freqnum2);
local int CanChangeToFreq(Player *p, int freqnum, char *err_buf, int buf_len);
local int CanEnterGame(Player *p, char *err_buf, int buf_len);
local void cbPreShipFreqChange(Player *p, int newShip, int oldShip, int newFreqnum, int oldFreqnum);
local void cbPlayerAction(Player *p, int action, Arena *arena);
local void cbArenaAction(Arena *arena, int action);
local int player_meets_resolution_requirements(Player *p, char *err_buf, int buf_len);
local int player_under_lag_limits(Player *p, char *err_buf, int buf_len);
local int max_freq_size(Arena *arena, int freqnum);
local int freq_not_full(Arena *arena, int freqnum, char *err_buf, int buf_len);
local int arena_not_full(Arena *arena, char *err_buf, int buf_len);
local int balancer_allows_change(Player *p, int newFreqnum, char *err_buf, int buf_len);
local void update_config(Arena *arena);

local Ifreqman freqman_interface =
{
	INTERFACE_HEAD_INIT(I_FREQMAN, "freqman-freqman")
	Initial,
	ShipChange,
	FreqChange,
};
local Ibalancer default_balancer_interface =
{
	INTERFACE_HEAD_INIT(I_BALANCER, "default-balancer")
	default_GetPlayerMetric,
	default_GetMaxMetric,
	default_GetMaximumDifference,
};
local Aenforcer aenforcer_adviser =
{
	ADVISER_HEAD_INIT(A_ENFORCER)
	NULL,
	CanChangeToFreq,
	CanEnterGame,
	NULL,
};

local int arenaDataKey = -1;
typedef struct arenadata 
{
	LinkedList freqs;
	int cfg_numberOfFrequencies;
	int cfg_requiredTeams;
	int cfg_rememberedTeams;
	int cfg_desiredTeams;
	int cfg_firstPrivateFreq;
	int cfg_firstBalancedFreq;
	int cfg_lastBalancedFreq;
	int cfg_disallowTeamSpectators;
	int cfg_alwaysStartInSpec;
	int cfg_maxPlaying;
	int cfg_maxPublicFreqSize;
	int cfg_maxPrivateFreqSize;
	int cfg_spectatorsCountForTeamSize;
	int cfg_maxXResolution;
	int cfg_maxYResolution;
	int cfg_maxResolutionPixels;
	int cfg_defaultBalancer_forceEvenTeams;
	int cfg_defaultBalancer_maxDifference;
} arenadata;

local int playerDataKey = -1;
typedef struct playerdata 
{
	Freq *freq;
} playerdata;

local pthread_mutex_t fm_mutex;

void refresh_freq(Arena *arena, Freq *freq)
{
	/* call with lock held */
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;
	freq->is_required = (freq->freqnum < ad->cfg_requiredTeams);
	freq->is_remembered = (freq->freqnum < ad->cfg_rememberedTeams);
	freq->is_balanced_against = (ad->cfg_firstBalancedFreq <= freq->freqnum && freq->freqnum < ad->cfg_lastBalancedFreq);
}

Freq * create_freq(Arena *arena, int freqnum)
{
	/* call with lock held */
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;
	Freq *freq = amalloc(sizeof(*freq));
	
	freq->freqnum = freqnum;
	LLInit(&freq->players);
	
	refresh_freq(arena, freq);
	LLAdd(&ad->freqs, freq);

	return freq;
}

Freq * get_freq(Arena *arena, int freqnum)
{
	/* call with lock held */
	Link *link;
	Freq *f, *result = NULL;
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;
	
	FOR_EACH(&ad->freqs, f, link)
	{
		if (f->freqnum == freqnum)
		{
			result = f;
			break;
		}
	}
	return result;
}

void prune_freqs(Arena *arena)
{
	Freq *freq;
	Link *link;
	int i;
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;

	pthread_mutex_lock(&fm_mutex);
	
	FOR_EACH(&ad->freqs, freq, link)
	{
		if (freq->freqnum >= ad->cfg_rememberedTeams)
		{
			freq->is_required = 0;
			if (LLIsEmpty(&freq->players))
			{
				/* remove empty, unrequired frequencies. */
				LLRemove(&ad->freqs, freq);
				afree(freq);
			}
		}
	}

	/* now make sure that the required teams exist */
	for (i = 0; i < ad->cfg_rememberedTeams; ++i)
	{
		freq = get_freq(arena, i);

		if (!freq)
			create_freq(arena, i);
	}
	
	pthread_mutex_unlock(&fm_mutex);
}

void freq_free_enum(const void *_freq)
{
	Freq *freq = (Freq *)_freq;
	LLEmpty(&freq->players);
	afree(freq);
}

void update_freqs(Arena *arena, Player *p, int newFreqnum, int oldFreqnum)
{
	Freq *freq;
	playerdata *pdat = PPDATA(p, playerDataKey);

	if (newFreqnum == oldFreqnum)
		return;
		
	/* we care not for these little hacks which cause misery to no end with
	 * lack of being treated like any other player. use a custom balancer if
	 * you need to account for fake players at this time. */
	if (p->type == T_FAKE)
		return;
		
	pd->Lock();
	pthread_mutex_lock(&fm_mutex);
	/* we don't need to bother storing who's on the spectator frequency, since
	 * we will never be balancing against it or checking it for fullness. */
	if (oldFreqnum != arena->specfreq)
	{
		freq = get_freq(arena, oldFreqnum);
		assert(freq == pdat->freq);
		remove_player_from_freq(arena, p, freq);
	}
	
	if (newFreqnum != arena->specfreq)
	{
		freq = get_freq(arena, newFreqnum);
		if (!freq)
			freq = create_freq(arena, newFreqnum);
		add_player_to_freq(p, freq);
	}

	pthread_mutex_unlock(&fm_mutex);
	pd->Unlock();
}

void add_player_to_freq(Player *p, Freq *freq)
{
	playerdata *pdat = PPDATA(p, playerDataKey);
	/* call with lock held inside playerdata lock */
	LLAdd(&freq->players, p);
	pdat->freq = freq;
}

void remove_player_from_freq(Arena *arena, Player *p, Freq *freq)
{
	playerdata *pdat = PPDATA(p, playerDataKey);
	/* call with lock held inside playerdata lock */
	LLRemove(&freq->players, p);
	pdat->freq = NULL;
	
	/* possibly disband the freq altogether, if it's not required */
	if (!freq->is_remembered && LLCount(&freq->players) == 0)
	{
		arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;
		afree(freq);
		LLRemove(&ad->freqs, freq);
	}
}

shipmask_t enforcers_get_allowable_ships(Arena *arena, Player *p, int ship, int freq, char *err_buf, int buf_len)
{
	/* checks the enforcers for allowable ships */
	LinkedList advisers;
	Link *link;
	Aenforcer *adviser;
	shipmask_t mask = SHIPMASK_ALL;

	if (freq == arena->specfreq)
	{
		return SHIPMASK_NONE;
	}
	
	LLInit(&advisers);

	mm->GetAdviserList(A_ENFORCER, arena, &advisers);

	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->GetAllowableShips)
		{
			mask &= adviser->GetAllowableShips(p, ship, freq, err_buf, buf_len);

			if (mask == 0)
			{
				/* the player can't use any ships, might as well stop looping */
				break;
			}
		}
	}

	mm->ReleaseAdviserList(&advisers);

	return mask;
}

int enforcers_can_change_to_freq(Arena *arena, Player *p, int new_freq, char *err_buf, int buf_len)
{
	/* checks the enforcers for a freq change */
	Aenforcer *adviser;
	LinkedList advisers;
	Link *link;
	int can_change = 1;

	LLInit(&advisers);

	mm->GetAdviserList(A_ENFORCER, arena, &advisers);

	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->CanChangeToFreq)
		{
			if (!adviser->CanChangeToFreq(p, new_freq, err_buf, buf_len))
			{
				can_change = 0;
				break;
			}
		}
	}

	mm->ReleaseAdviserList(&advisers);

	return can_change;
}

int enforcers_can_enter_game(Arena *arena, Player *p, char *err_buf, int buf_len)
{
	/* checks the enforcers for a freq change */
	Aenforcer *adviser;
	LinkedList advisers;
	Link *link;
	int can_enter = 1;

	LLInit(&advisers);

	mm->GetAdviserList(A_ENFORCER, arena, &advisers);

	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->CanEnterGame)
		{
			if (!adviser->CanEnterGame(p, err_buf, buf_len))
			{
				can_enter = 0;
				break;
			}
		}
	}

	mm->ReleaseAdviserList(&advisers);

	return can_enter;
}

int enforcers_is_unlocked(Arena *arena, Player *p, char *err_buf, int buf_len)
{
	/* checks the enforcers for a freq change */
	Aenforcer *adviser;
	LinkedList advisers;
	Link *link;
	int is_unlocked = 1;

	LLInit(&advisers);

	mm->GetAdviserList(A_ENFORCER, arena, &advisers);

	FOR_EACH(&advisers, adviser, link)
	{
		if (adviser->IsUnlocked)
		{
			if (!adviser->IsUnlocked(p, err_buf, buf_len))
			{
				is_unlocked = 0;
				break;
			}
		}
	}

	mm->ReleaseAdviserList(&advisers);

	return is_unlocked;
}

int get_player_metric(Player *p, Ibalancer *balancer)
{
	int result;
	if (IS_HUMAN(p))
	{
		result = balancer->GetPlayerMetric(p);
	}
	else
	{
		result = 0;
	}
	return result;
}

int get_freq_metric(Freq *freq, Ibalancer *balancer)
{
	Player *p;
	Link *link;
	int result = 0;

	if (!freq)
	{
		return 0;
	}
	
	pd->Lock();
	pthread_mutex_lock(&fm_mutex);
	FOR_EACH(&freq->players, p, link)
	{
		result += get_player_metric(p, balancer);
	}
	pthread_mutex_unlock(&fm_mutex);
	pd->Unlock();
	
	return result;
}

int find_entry_freq(Arena *arena, Player *p, char *err_buf, int buf_len)
{
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;

	int i;
	int result = arena->specfreq;
	int resultMetric = -1;
	int max = ad->cfg_desiredTeams;
	Ibalancer *balancer = mm->GetInterface(I_BALANCER, arena);
	int playerMetric = get_player_metric(p, balancer);
	Freq *freq;
	int freqMetric;
		
	for (i = 0; i < max; ++i)
	{
		if (!enforcers_can_change_to_freq(arena, p, i, err_buf, buf_len))
			continue;

		freq = get_freq(arena, i);
		if (!freq)
		{
			result = i;
			break;
		}

		freqMetric = get_freq_metric(freq, balancer);
		if (freqMetric <= balancer->GetMaxMetric(arena, i) - playerMetric)
		{
			if (result == arena->specfreq
				|| freqMetric < resultMetric)
			{
				 /* if we have not found a freq yet or if this freq is better */
				result = i;
				resultMetric = freqMetric;
			}
		}
	}
	
	if (result == arena->specfreq) /* if we couldn't find a freq yet.. */
	{
		/* note: i is desiredTeams + 1 right now - this time we'll do things
		 * slightly differently. */
		while (i < ad->cfg_numberOfFrequencies)
		{
			freq = get_freq(arena, i);
			if (enforcers_can_change_to_freq(arena, p, i, err_buf, buf_len))
			{
				if (!freq)
				{
					result = i;
					break;
				}
				
				freqMetric = get_freq_metric(freq, balancer);
				if (freqMetric <= balancer->GetMaxMetric(arena, i) - playerMetric)
				{
					result = i;
					break;
				}
			}
			else if (!freq)
			{
				/* failed on an empty freq, abort, there is probably some sort
				 * of blocker that would be pointless to call repeatedly for
				 * other freqs. */
				break;
			}
			++i;
		}
	}
	
	if (result != arena->specfreq)
	{
		/* check one final time if we have a result. if so, clear any error
		 * messages that may have been set while checking other freqs. */
		if (err_buf)
			strcpy(err_buf, "");
	}

	mm->ReleaseInterface(balancer);
	
	return result;
}

void Initial(Player *p, int *ship, int *freq)
{
	Arena *arena = p->arena;
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;
	int workingShip = *ship;
	int workingFreqnum = *freq;
	
	if (!arena)
		return;
		
	if (ad->cfg_alwaysStartInSpec || !enforcers_can_enter_game(arena, p, NULL, 0))
	{
		workingShip = SHIP_SPEC;
	}
	
	if (workingShip == SHIP_SPEC)
	{
		workingFreqnum = arena->specfreq;
	}
	else
	{
		shipmask_t mask;

		/* find an initial freq using the balancer and enforcers*/
		workingFreqnum = find_entry_freq(arena, p, NULL, 0);

		if (workingFreqnum == arena->specfreq)
		{
			workingShip = SHIP_SPEC;
		}
		else
		{
			mask = enforcers_get_allowable_ships(arena, p, workingShip, workingFreqnum, NULL, 0);
			if ((mask & (1 << workingShip)) == 0)
			{
				int i;

				workingShip = SHIP_SPEC;

				for (i = SHIP_WARBIRD; i <= SHIP_SHARK; i++)
				{
					if (mask & (1 << i))
					{
						workingShip = i;
						break;
					}
				}
			}

			/* if the enforcers didn't let them take a ship, send them to spec */
			if (workingShip == SHIP_SPEC)
			{
				workingFreqnum = arena->specfreq;
			}
		}
	}
	
	*ship = workingShip;
	*freq = workingFreqnum;
}

void ShipChange(Player *p, int workingShip, char *err_buf, int buf_len)
{
	Arena *arena = p->arena;
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;
	int workingFreqnum = p->p_freq;
	
	/* setup the err_buf so we know if an enforcer wrote a message */
	if (err_buf && buf_len)
	{
		err_buf[0] = '\0';
	}
	
	if (workingShip >= SHIP_SPEC)
	{
		/* always allow switching to spec */
		game->SetShipAndFreq(p, SHIP_SPEC, arena->specfreq);
		return;
	}
	
	/* see if the person is allowed to change their ship/freq. */
	if (!enforcers_is_unlocked(arena, p, err_buf, buf_len))
	{
		/* passes along message if any. */
		return;
	}
	
	if (p->p_ship == SHIP_SPEC)
	{
		/* since they're coming out of spec, check to see if they can enter the
		 * game. */
		if (!enforcers_can_enter_game(arena, p, err_buf, buf_len))
		{
			/* passes along message if any. */
			return;
		}
	
		if (workingFreqnum == arena->specfreq)
		{
			/* they're coming from specfreq, give them a new freq */
			workingFreqnum = find_entry_freq(arena, p, err_buf, buf_len);
			if (workingFreqnum == arena->specfreq)
			{
				/* if we could not find an entry freq */
				if (err_buf && err_buf[0] != '\0')
				{
					/* and if an error message was left */
					char *tempBuffer = astrdup(err_buf);
					snprintf(err_buf, buf_len, "Couldn't find a frequency to place you on (error for freq %d: %s)", ad->cfg_numberOfFrequencies - 1, tempBuffer);
					afree(tempBuffer);
				}
				else
				{
					astrncpy(err_buf, "Couldn't find a frequency to place you on.", buf_len);
				}
			}
		}
		else if (!ad->cfg_spectatorsCountForTeamSize && !freq_not_full(arena, workingFreqnum, NULL, 0))
		{
			astrncpy(err_buf, "Your frequency already has the maximum number of players in the game.", buf_len);
			return;
		}
	}
	
	/* make sure their ship is legal */
	if (workingShip != SHIP_SPEC)
	{
		shipmask_t mask = enforcers_get_allowable_ships(arena, p, workingShip, workingFreqnum, err_buf, buf_len);
		if ((mask & (1 << workingShip)) == 0)
		{
			if (mask & (1 << p->p_ship))
			{
				/* default to the old ship */
				workingShip = p->p_ship;
			}
			else
			{
				int i;
				workingShip = SHIP_SPEC;
				for (i = SHIP_WARBIRD; i <= SHIP_SHARK; i++)
				{
					if (mask & (1 << i))
					{
						workingShip = i;
						break;
					}
				}
			}
		}
	}
	
	if (workingShip == SHIP_SPEC && ad->cfg_disallowTeamSpectators)
	{
		if (err_buf && err_buf[0] == '\0')
			snprintf(err_buf, buf_len, "You may only spectate on the spectator frequency.");
		workingFreqnum = arena->specfreq;
	}

	game->SetShipAndFreq(p, workingShip, workingFreqnum);
	/* updateFreqs will be called in the shipfreqchange callback */
}

void FreqChange(Player *p, int requestedFreqnum, char *err_buf, int buf_len)
{
	Arena *arena = p->arena;
	int workingShip = p->p_ship;
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;

	/* setup the err_buf so we know if an enforcer wrote a message */
	if (err_buf && buf_len)
	{
		err_buf[0] = '\0';
	}

	if (workingShip == SHIP_SPEC && requestedFreqnum == arena->specfreq)
	{
		/* always allow switching to specfreq if the person is in spec */
		game->SetFreq(p, arena->specfreq);
		return;
	}
	
	/* see if the person is allowed to change their ship/freq. */
	if (!enforcers_is_unlocked(arena, p, err_buf, buf_len))
	{
		/* passes along message if any. */
		return;
	}		

	if (requestedFreqnum < 0 || requestedFreqnum >= ad->cfg_numberOfFrequencies)
	{
		/* they requested a bad freq. */
		if (workingShip == SHIP_SPEC && p->p_freq != arena->specfreq)
		{
			/* send him back to spec freq */
			game->SetFreq(p, arena->specfreq);
		}
		else
		{
			if (err_buf)
				snprintf(err_buf, buf_len, "That frequency is not used in this arena.");
		}

		return;
	}
	
	/* at this point, we have a valid freq, now we just need to make sure that
	 * the user can change to it. first, make sure just changing to the freq is
	 * allowed. second, make sure that if spectators are only allowed on the
	 * spec freq, that they have a ship they can use on that freq. */
	
	if (!enforcers_can_change_to_freq(arena, p, requestedFreqnum, err_buf, buf_len))
	{
		/* passes along message if any */
		return;
	}

	if (workingShip == SHIP_SPEC && ad->cfg_disallowTeamSpectators)
	{
		int i;
		shipmask_t mask;
		/* since they must come out of spec immediately, we must find them a
		 * ship now. */
		if (!enforcers_can_enter_game(arena, p, err_buf, buf_len))
		{
			/* passes along message if any. */
			return;
		}
		
		mask = enforcers_get_allowable_ships(arena, p, SHIP_SPEC, requestedFreqnum, err_buf, buf_len);
		for (i = SHIP_WARBIRD; i <= SHIP_SHARK; ++i)
		{
			if (mask & (1 << i))
			{
				workingShip = i;
				break;
			}
		}
	}
	else if (workingShip != SHIP_SPEC)
	{
		/* since they're already in the game, we just need to make sure their
		 * ship is legal on their new freq, or find them a new ship if it's
		 * not */
		shipmask_t mask = enforcers_get_allowable_ships(arena, p, workingShip, requestedFreqnum, err_buf, buf_len);
		if (!(mask & (1 << workingShip)))
		{
			/* if their ship is not allowed, we must find them a new ship. */
			workingShip = SHIP_SPEC;
			int i;
			for (i = SHIP_WARBIRD; i <= SHIP_SHARK; ++i)
			{
				if (mask & (1 << i))
				{
					workingShip = i;
					break;
				}
			}
		}
	}
	
	if (ad->cfg_disallowTeamSpectators && workingShip == SHIP_SPEC && err_buf)
	{
		/* at this point, the person should have a ship other than ship spec
		 * if one is required. if they don't, then we fail and report the error,
		 * if error messages can be passed along. let's clarify why the person
		 * cannot change to the freq, since disallowTeamSpectators is a little
		 * weird. */
		if (err_buf)
		{
			if (*err_buf != '\0')			
			{
				/* if we have an error, let's append it. */
				char *tempBuffer = astrdup(err_buf);
				snprintf(err_buf, buf_len, "You cannot change to freq %d, because you could not enter a ship there (error: %s).", requestedFreqnum, tempBuffer);
				afree(tempBuffer);
			}
			else
			{
				/* no error was given, so just use a default message. */
				snprintf(err_buf, buf_len, "You cannot change to freq %d, because you could not enter a ship there.", requestedFreqnum);
			}
		}
	}
	else
	{
		/* the person passed all checks for being unlocked, being able to change
		 * to the target freq, and having a legal ship. so we're done! */
		game->SetShipAndFreq(p, workingShip, requestedFreqnum);
	}
	/* updateFreqs will be called in the shipfreqchange callback */
}

int default_GetPlayerMetric(Player *p)
{
	return 1;
}

int default_GetMaxMetric(Arena *arena, int freqnum)
{
	return max_freq_size(arena, freqnum);
}

int default_GetMaximumDifference(Arena *arena, int freqnum1, int freqnum2)
{
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;

	if (ad->cfg_defaultBalancer_forceEvenTeams)
		return ad->cfg_defaultBalancer_maxDifference;
	else
		return INT_MAX;
}

int CanChangeToFreq(Player *p, int freqnum, char *err_buf, int buf_len)
{
	Arena *arena = p->arena;
	int result = TRUE;

	result = freq_not_full(arena, freqnum, err_buf, buf_len) && balancer_allows_change(p, freqnum, err_buf, buf_len);
	return result;
}

int CanEnterGame(Player *p, char *err_buf, int buf_len)
{
	return player_meets_resolution_requirements(p, err_buf, buf_len)
		&& arena_not_full(p->arena, err_buf, buf_len)
		&& player_under_lag_limits(p, err_buf, buf_len);
}

void cbPreShipFreqChange(Player *p, int newShip, int oldShip, int newFreqnum, int oldFreqnum)
{
	update_freqs(p->arena, p, newFreqnum, oldFreqnum);
}

void cbPlayerAction(Player *p, int action, Arena *arena)
{
	playerdata *pdat = PPDATA(p, playerDataKey);
	if (action == PA_PREENTERARENA)
	{
		pd->Lock();
		pthread_mutex_lock(&fm_mutex);
		pdat->freq = NULL;
		pthread_mutex_unlock(&fm_mutex);
		pd->Unlock();
	}
	if (action == PA_ENTERARENA)
	{
		update_freqs(arena, p, p->p_freq, arena->specfreq);
	}
	else if (action == PA_LEAVEARENA)
	{
		int freqnum = arena->specfreq;
		/* pretend all people leaving pass through the specfreq on their way
		 * out. */
		pd->Lock();
		pthread_mutex_lock(&fm_mutex);
		if (pdat->freq)
			freqnum = pdat->freq->freqnum;
		update_freqs(arena, p, arena->specfreq, freqnum);
		pthread_mutex_unlock(&fm_mutex);
		pd->Unlock();
	}
}

void cbArenaAction(Arena *arena, int action)
{
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;
	
	if (action == AA_CREATE)
	{
		LLInit(&ad->freqs);
		update_config(arena);
		prune_freqs(arena);
	}
	else if (action == AA_DESTROY)
	{
		pthread_mutex_lock(&fm_mutex);
		LLEnum(&ad->freqs, freq_free_enum);
		LLEmpty(&ad->freqs);
		pthread_mutex_unlock(&fm_mutex);
	}
	else if (action == AA_CONFCHANGED)
	{
		pthread_mutex_lock(&fm_mutex);
		update_config(arena);
		prune_freqs(arena);
		pthread_mutex_unlock(&fm_mutex);
	}
}

int player_meets_resolution_requirements(Player *p, char *err_buf, int buf_len)
{
	int result = TRUE;
	arenadata *ad = p->arena ? P_ARENA_DATA(p->arena, arenaDataKey) : NULL;
	if ((ad->cfg_maxXResolution && p->xres > ad->cfg_maxXResolution)
		|| (ad->cfg_maxYResolution && p->yres > ad->cfg_maxYResolution)
	)
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "The maximum resolution allowed in this arena is %d by %d pixels. Your resolution is too high (%d by %d.)",
				ad->cfg_maxXResolution, ad->cfg_maxYResolution, p->xres, p->yres);
		result = FALSE;
	}
	else if (ad->cfg_maxResolutionPixels && (p->xres * p->yres) > ad->cfg_maxResolutionPixels)
	{
		if (err_buf)
			snprintf(err_buf, buf_len, "The maximum display area allowed in this arena is %d pixels. Your display area is too big (%d.)",
				ad->cfg_maxResolutionPixels, (p->xres * p->yres));
		result = FALSE;
	}
	return result;
}

int player_under_lag_limits(Player *p, char *err_buf, int buf_len)
{
	int result = TRUE;
	
	/* this flag is kind of silly, an interface to a lag handler would be much
	 * cooler, or just having a lag module use its own enforcer. */
	if (p->flags.no_ship)
	{
		if (err_buf)
			astrncpy(err_buf, "You are too lagged to play in this arena.", buf_len);
		result = FALSE;
	}
	return result;
}

int max_freq_size(Arena *arena, int freqnum)
{
	int result;
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;

	if (freqnum >= ad->cfg_firstPrivateFreq)
		result = ad->cfg_maxPrivateFreqSize;
	else
		result = ad->cfg_maxPublicFreqSize;
	
	return result;
}

int freq_not_full(Arena *arena, int freqnum, char *err_buf, int buf_len)
{
	int max;
	int result = TRUE;
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;

	max = max_freq_size(arena, freqnum);

	if (max <= 0)
	{
		result = FALSE;
		if (err_buf)
			snprintf(err_buf, buf_len, "Frequency %d is not available.", freqnum);
	}
	else
	{
		int count = 0;
		Player *p;
		Link *link;
		pd->Lock();
		FOR_EACH_PLAYER_IN_ARENA(p, arena)
		{
			if (p->p_freq == freqnum
				&& IS_HUMAN(p)
				&& p->status == S_PLAYING
				&& (p->p_ship != SHIP_SPEC || ad->cfg_spectatorsCountForTeamSize)
			)
			{
				++count;
			}
		}
		pd->Unlock();
		
		if (count >= max)
		{
			result = FALSE;
			if (err_buf)
				snprintf(err_buf, buf_len, "Frequency %d is full.", freqnum);
		}
	}
	
	return result;
}

int arena_not_full(Arena *arena, char *err_buf, int buf_len)
{
	Player *p;
	Link *link;
	int count = 0;
	int result = TRUE;
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;

	pd->Lock();
	FOR_EACH_PLAYER_IN_ARENA(p, arena)
	{
		if (IS_HUMAN(p)
			&& p->status == S_PLAYING
			&& p->p_ship != SHIP_SPEC
		)
		{
			++count;
		}
	}
	pd->Unlock();
	
	if (count > ad->cfg_maxPlaying)
	{
		if (err_buf)
			astrncpy(err_buf, "There are already the maximum number of people playing allowed.", buf_len);
		result = FALSE;
	}

	return result;
}

int balancer_allows_change(Player *p, int newFreqnum, char *err_buf, int buf_len)
{
	int result, fastReturn;
	Arena *arena;
	int playerMetric;
	
	int oldFreqnum;
	Freq *oldFreq;
	Freq *newFreq;
	int oldFreqMetric, oldFreqMetricPotential;
	int newFreqMetric, newFreqMetricPotential;
	int maxMetric;
	
	arenadata *ad = p->arena ? P_ARENA_DATA(p->arena, arenaDataKey) : NULL;
	
	arena = p->arena;	
	oldFreqnum = p->p_freq;
	
	pthread_mutex_lock(&fm_mutex);
	
	oldFreq = get_freq(arena, oldFreqnum);
	newFreq = get_freq(arena, newFreqnum);

	/* a few short circuting checks here, we may not actually need to check
	 * the balancer itself. */
	fastReturn = FALSE;
	
	if (newFreq && newFreq->is_required && LLIsEmpty(&newFreq->players))
	{
		/* they're changing to an empty required team: always allow */
		result = TRUE;
		fastReturn = TRUE;
	}
	else if (oldFreq && oldFreq->is_required && LLCount(&oldFreq->players) == 1)
	{
		/* they cannot leave a required team if they were the only player on it,
		 * unless they are going to another required team that has no players on
		 * it. */
		if (err_buf)
			snprintf(err_buf, buf_len, "Your frequency requires at least one player.");
		
		result = FALSE;
		fastReturn = TRUE;
	}
	else
	{
		/* see if there are required teams that need to be filled */
		Freq *freq;
		Link *link;
		FOR_EACH(&ad->freqs, freq, link)
		{
			if (freq->is_required && LLIsEmpty(&freq->players))
			{
				/* they shouldn't be changing to a team that isn't required
				 * when there are still required teams to start up. */
				if (err_buf)
					snprintf(err_buf, buf_len, "Frequency %d needs players first.", freq->freqnum);

				result = FALSE;
				fastReturn = TRUE;
				break;
			}
		}
	}
	
	if (!fastReturn)
	{
		Ibalancer *balancer = mm->GetInterface(I_BALANCER, arena);
		
		oldFreqMetric = get_freq_metric(oldFreq, balancer);
		newFreqMetric = get_freq_metric(newFreq, balancer);
	
		playerMetric = get_player_metric(p, balancer);
		oldFreqMetricPotential = oldFreqMetric - playerMetric;
		newFreqMetricPotential = newFreqMetric + playerMetric;
		result = TRUE;
	
		maxMetric = balancer->GetMaxMetric(arena, newFreqnum);

		if (maxMetric && newFreqMetricPotential > maxMetric)
		{
			if (err_buf)
				astrncpy(err_buf, "Changing to that team would make it too powerful.", buf_len);
			result = FALSE;
		}
		else
		{
			if (oldFreq && oldFreqMetricPotential > 0 && balancer->GetMaximumDifference(arena, oldFreqnum, newFreqnum) < newFreqMetricPotential - oldFreqMetricPotential)
			{
				if (err_buf)
					astrncpy(err_buf, "Changing to that team would disrupt the balance between it and your current team.", buf_len);
				result = FALSE;
			}
			else
			{
				Freq *f;
				Link *link;
			
				FOR_EACH(&ad->freqs, f, link)
				{
					int freqMetric;
					if (!f->is_balanced_against)
						continue;
					if (f == oldFreq || f == newFreq)
						continue;
					freqMetric = get_freq_metric(f, balancer);
				
					if (balancer->GetMaximumDifference(arena, newFreqnum, f->freqnum) < newFreqMetricPotential - freqMetric)
					{
						if (err_buf)
							astrncpy(err_buf, "Changing to that team would make the teams too uneven.", buf_len);
						result = FALSE;
						break;
					}
				}
			}
		}
		
		mm->ReleaseInterface(balancer);
	}
	
	pthread_mutex_unlock(&fm_mutex);
	
	return result;
}

void update_config(Arena *arena)
{
	arenadata *ad = arena ? P_ARENA_DATA(arena, arenaDataKey) : NULL;
	ConfigHandle ch = arena->cfg;

	/* cfghelp: Team:MaxFrequency, arena, int, range: 1-10000, def: 10000
	 * One more than the highest frequency allowed. Set this below
	 * PrivFreqStart to disallow private freqs. */
	ad->cfg_numberOfFrequencies = cfg->GetInt(ch, "Team", "MaxFrequency", 10000);
	if (ad->cfg_numberOfFrequencies < 1 || ad->cfg_numberOfFrequencies > 10000)
		ad->cfg_numberOfFrequencies = 10000;

	/* cfghelp: Team:DesiredTeams, arena, int, def: 2
	 * The number of teams that the freq balancer will form as players
	 * enter. */
	ad->cfg_desiredTeams = cfg->GetInt(arena->cfg, "Team", "DesiredTeams", 2);
	if (ad->cfg_desiredTeams < 0 || ad->cfg_desiredTeams > ad->cfg_numberOfFrequencies)
		ad->cfg_desiredTeams = 0;

	/* cfghelp: Team:RequiredTeams, arena, int, def: 0
	 * The number of teams that the freq manager will require to exist. */
	ad->cfg_requiredTeams = cfg->GetInt(arena->cfg, "Team", "RequriedTeams", 0);
	if (ad->cfg_requiredTeams < 0 || ad->cfg_requiredTeams > ad->cfg_numberOfFrequencies)
		ad->cfg_requiredTeams = 0;

	/* cfghelp: Team:RequiredTeams, arena, int, def: 0
	 * The number of teams that the freq manager will keep in memory. Must be at least as high as RequiredTeams. */
	ad->cfg_rememberedTeams = cfg->GetInt(arena->cfg, "Team", "RememeberedTeams", 0);
	if (ad->cfg_rememberedTeams < 0 || ad->cfg_rememberedTeams > ad->cfg_numberOfFrequencies)
		ad->cfg_rememberedTeams = 0;
	else if (ad->cfg_rememberedTeams < ad->cfg_requiredTeams)
		ad->cfg_rememberedTeams = ad->cfg_requiredTeams;

	/* cfghelp: Team:PrivFreqStart, arena, int, range: 0-9999, def: 100
	 * Freqs above this value are considered private freqs. */
	ad->cfg_firstPrivateFreq = cfg->GetInt(ch, "Team", "PrivFreqStart", 100);
	if (ad->cfg_firstPrivateFreq < 0 || ad->cfg_firstPrivateFreq > 9999)
		ad->cfg_firstPrivateFreq = 100;

	/* cfghelp: Team:BalancedAgainstStart, arena, int, def: 1
	 * Freqs >= BalancedAgainstStart and < BalancedAgainstEnd will be
	 * checked for balance even when players are not changing to or from
	 * these freqs. Set End < Start to disable this check. */
	ad->cfg_firstBalancedFreq = cfg->GetInt(arena->cfg, "Team", "BalancedAgainstStart", 1);

	/* cfghelp: Team:BalancedAgainstEnd, arena, int, def: 0
	 * Freqs >= BalancedAgainstStart and < BalancedAgainstEnd will be
	 * checked for balance even when players are not changing to or from
	 * these freqs. Set End < Start to disable this check. */
	ad->cfg_lastBalancedFreq = cfg->GetInt(arena->cfg, "Team", "BalancedAgainstEnd", 0);

	/* cfghelp: Team:DisallowTeamSpectators, arena, bool, def: 0
	 * If players are allowed to spectate outside of the spectator
	 * frequency. */
	ad->cfg_disallowTeamSpectators = cfg->GetInt(ch, "Team", "DisallowTeamSpectators", 0);
	
	/* cfghelp: Team:InitialSpec, arena, bool, def: 0
	 * If players entering the arena are always assigned to spectator mode. */
	ad->cfg_alwaysStartInSpec = cfg->GetInt(ch, "Team", "InitialSpec", 0);

	/* cfghelp: General:MaxPlaying, arena, int, def: 100
	 * This is the most players that will be allowed to play in the arena at
	 * once. Zero means no limit. */
	ad->cfg_maxPlaying = cfg->GetInt(ch, "General", "MaxPlaying", 100);

	/* cfghelp: Team:MaxPerTeam, arena, int, def: 1000
	 * The maximum number of players on a public freq. Zero means these teams are not
	 * accessible */
	ad->cfg_maxPublicFreqSize = cfg->GetInt(ch, "Team", "MaxPerTeam", 1000);

	/* cfghelp: Team:MaxPerPrivateTeam, arena, int, def: 1000
	 * The maximum number of players on a private freq. Zero means these teams are not
	 * accessible. */
	ad->cfg_maxPrivateFreqSize = cfg->GetInt(ch, "Team", "MaxPerPrivateTeam", 1000);

	/* cfghelp: Team:IncludeSpectators, arena, bool, def: 0
	 * Whether to include spectators when enforcing maximum freq sizes. */
	ad->cfg_spectatorsCountForTeamSize = cfg->GetInt(ch, "Team", "IncludeSpectators", 0);

	/* cfghelp: Misc:MaxXres, arena, int, def: 0
	 * Maximum screen width allowed in the arena. Zero means no limit. */
	ad->cfg_maxXResolution = cfg->GetInt(ch, "Misc", "MaxXres", 0);

	/* cfghelp: Misc:MaxYres, arena, int, def: 0
	 * Maximum screen height allowed in the arena. Zero means no limit. */
	ad->cfg_maxYResolution = cfg->GetInt(ch, "Misc", "MaxYres", 0);

	/* cfghelp: Misc:MaxResArea, arena, int, def: 0
	 * Maximum screen area (x*y) allowed in the arena, Zero means no limit. */
	ad->cfg_maxResolutionPixels = cfg->GetInt(ch, "Misc", "MaxResArea", 0);

	/* cfghelp: Team:ForceEvenTeams, arena, int, def: 0
	 * Whether the default balancer will enforce even teams. Does not apply if
	 * a custom balancer module is used. */
	ad->cfg_defaultBalancer_forceEvenTeams = cfg->GetInt(ch, "Team", "ForceEvenTeams", 0);
	
	/* cfghelp: Team:MaxTeamDifference, arena, int, def: 1
	 * How many players difference the balancer should tolerate. Does not apply
	 * if a custom balancer module is used.*/
	ad->cfg_defaultBalancer_maxDifference = cfg->GetInt(ch, "Team", "MaxTeamDifference", 1);
	if (ad->cfg_defaultBalancer_maxDifference < 1)
		ad->cfg_defaultBalancer_maxDifference = 1;
}

EXPORT int MM_freqman(int action, Imodman *_mm, Arena *arena)
{
	int failedLoad = FALSE;
	if (action == MM_LOAD)
	{
		pthread_mutexattr_t attr;

		mm = _mm;
		lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
		if (!lm)
		{
			fprintf(stderr, "<freqman> error obtaining required interface I_LOGMAN " I_LOGMAN);
			return MM_FAIL;
		}
		cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
		if (!cfg)
		{
			lm->Log(L_ERROR, "<freqman> error obtaining required interface I_CONFIG " I_CONFIG);
			failedLoad = TRUE;
			goto fail_load;
		}
		game = mm->GetInterface(I_GAME, ALLARENAS);
		if (!game)
		{
			lm->Log(L_ERROR, "<freqman> error obtaining required interface I_GAME " I_GAME);
			failedLoad = TRUE;
			goto fail_load;
		}
		aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
		if (!aman)
		{
			lm->Log(L_ERROR, "<freqman> error obtaining required interface I_ARENAMAN " I_ARENAMAN);
			failedLoad = TRUE;
			goto fail_load;
		}
		pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
		if (!pd)
		{
			lm->Log(L_ERROR, "<freqman> error obtaining required interface I_PLAYERDATA " I_PLAYERDATA);
			failedLoad = TRUE;
			goto fail_load;
		}
		arenaDataKey = aman->AllocateArenaData(sizeof(arenadata));
		if (arenaDataKey == -1)
		{
			lm->Log(L_ERROR, "<freqman> unable to register arena-data");
			failedLoad = TRUE;
			goto fail_load;
		}
		playerDataKey = pd->AllocatePlayerData(sizeof(playerdata));
		if (playerDataKey == -1)
		{
			lm->Log(L_ERROR, "<freqman> unable to register player-data");
			failedLoad = TRUE;
			goto fail_load;
		}

		pthread_mutexattr_init(&attr);
		pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
		pthread_mutex_init(&fm_mutex, &attr);
		pthread_mutexattr_destroy(&attr);
		mm->RegCallback(CB_PRESHIPFREQCHANGE, cbPreShipFreqChange, ALLARENAS);
		mm->RegCallback(CB_PLAYERACTION, cbPlayerAction, ALLARENAS);
		mm->RegCallback(CB_ARENAACTION, cbArenaAction, ALLARENAS);
		mm->RegAdviser(&aenforcer_adviser, ALLARENAS);

		mm->RegInterface(&freqman_interface, ALLARENAS);
		mm->RegInterface(&default_balancer_interface, ALLARENAS);
		return MM_OK;
	}
	else if (action == MM_UNLOAD)
	{
		if (mm->UnregInterface(&freqman_interface, ALLARENAS))
		{
			lm->Log(L_ERROR, "<freqman> unable to unregister freqman_interface");
			return MM_FAIL;
		}
		if (mm->UnregInterface(&default_balancer_interface, ALLARENAS))
		{
			lm->Log(L_ERROR, "<freqman> unable to unregister default_balancer_interface");
			return MM_FAIL;
		}

		mm->UnregAdviser(&aenforcer_adviser, ALLARENAS);
		mm->UnregCallback(CB_PRESHIPFREQCHANGE, cbPreShipFreqChange, ALLARENAS);
		mm->UnregCallback(CB_PLAYERACTION, cbPlayerAction, ALLARENAS);
		mm->UnregCallback(CB_ARENAACTION, cbArenaAction, ALLARENAS);
		pthread_mutex_destroy(&fm_mutex);
fail_load:

		if (playerDataKey != -1)
			pd->FreePlayerData(playerDataKey);
		if (arenaDataKey != -1)
			aman->FreeArenaData(arenaDataKey);
		mm->ReleaseInterface(cfg);
		mm->ReleaseInterface(game);
		mm->ReleaseInterface(lm);
		mm->ReleaseInterface(aman);
		mm->ReleaseInterface(pd);
		if (failedLoad)
			return MM_FAIL;
		else
			return MM_OK;
	}
	return MM_FAIL;
}


