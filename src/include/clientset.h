
/* dist: public */

#ifndef __CLIENTSET_H
#define __CLIENTSET_H

/* Iclientset
 *
 * this is the interface to the module that manages the client-side
 * settings. it loads them from disk when the arena is loaded and when
 * the config files change change. arenaman calls SendClientSettings as
 * part of the arena response procedure.
 */

typedef u32 override_key_t;

#define I_CLIENTSET "clientset-5"

typedef struct Iclientset
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	void (*SendClientSettings)(Player *p);
	/* pyint: player_not_none -> void */

	u32 (*GetChecksum)(Player *p, u32 key);

	int (*GetRandomPrize)(Arena *arena);
	/* pyint: arena_not_none -> int */

	override_key_t (*GetOverrideKey)(const char *section, const char *key);
	/* pyint: string, string -> int */
	/* zero return means failure */

	void (*ArenaOverride)(Arena *arena, override_key_t key, i32 val);
	/* pyint: arena_not_none, int, int -> void */
	void (*ArenaUnoverride)(Arena *arena, override_key_t key);
	/* pyint: arena_not_none, int -> void */
	
	/* The return value for these 2 is a boolean, 0 = failure */
	int (*GetArenaOverride)(Arena *arena, override_key_t key, int *value);
	/* pyint: arena_not_none, int, int out -> int */
	int (*GetArenaValue)(Arena *arena, override_key_t key);
	/* pyint: arena_not_none, int -> int */

	void (*PlayerOverride)(Player *p, override_key_t key, i32 val);
	/* pyint: player_not_none, int, int -> void */
	void (*PlayerUnoverride)(Player *p, override_key_t key);
	/* pyint: player_not_none, int -> void */

	/* The return value for these 2 is a boolean, 0 = failure */
	int (*GetPlayerOverride)(Player *p, override_key_t key, int *value);
	/* pyint: player_not_none, int, int out -> int */
	int (*GetPlayerValue)(Player *p, override_key_t key);
	/* pyint: player_not_none, int -> int */
} Iclientset;


#endif

