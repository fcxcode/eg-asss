
/* dist: public */

#ifndef __OBJECTS_H
#define __OBJECTS_H

/* Iobjects
 * this module will handle all object-related packets
 */

#define I_OBJECTS "objects-4"

typedef struct Iobjects
{
	INTERFACE_HEAD_DECL
	/* pyint: use */

	/* sends the current LVZ object state to a player */
	void (*SendState)(Player *p);
	/* pyint: player_not_none -> void */

	/* if target is an arena, the defaults are changed */
	void (*Toggle)(const Target *t, int id, int on);
	/* pyint: target, int, int -> void */
	void (*ToggleSet)(const Target *t, short *id, char *ons, int size);

	/* use last two parameters for rel_x, rel_y when it's a screen object */
	void (*Move)(const Target *t, int id, int x, int y, int rx, int ry);
	/* pyint: target, int, int, int, int, int -> void */
	void (*Image)(const Target *t, int id, int image);
	/* pyint: target, int, int -> void */
	void (*Layer)(const Target *t, int id, int layer);
	/* pyint: target, int, int -> void */
	void (*Timer)(const Target *t, int id, int time);
	/* pyint: target, int, int -> void */
	void (*Mode)(const Target *t, int id, int mode);
	/* pyint: target, int, int -> void */
	
	// toggle's the lvz off and remove's any modified info like its position
	void (*Reset)(Arena *arena, int id);
	/* pyint: arena_not_none, int -> void */

	int (*InfoDefault)(Arena *arena, int id, int *off, int *image, int *layer, int *mode, int *mapobj, int *x, int *y, int *rx, int *ry);

	//the same as ?objinfo
	int (*InfoCurrent)(Arena *arena, int id, int *off, int *image, int *layer, int *mode, int *mapobj, int *x, int *y, int *rx, int *ry);
} Iobjects;

#endif

