
/* dist: public */

#ifndef __FG_WZ_H
#define __FG_WZ_H

/* called to calculate the points awarded for a warzone-type flag game
 * win. handlers should increment *points.
 * @threading called from main  
 */
#define CB_WARZONEWIN "warzonewin"
typedef void (*WarzoneWinFunc)(Arena *a, int freq, int *points);
/* pycb: arena_not_none, int, int inout */

#endif

