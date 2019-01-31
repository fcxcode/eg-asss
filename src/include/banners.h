
/* dist: public */

#ifndef __BANNERS_H
#define __BANNERS_H

#include "packets/banners.h"

/** called when the player sets a new banner
 * @threading normally called from main, also fired by the interface functions
 */ 
#define CB_SET_BANNER "setbanner"
typedef void (*SetBannerFunc)(Player *p, Banner *banner, int from_player);
/* pycb: player_not_none, banner, int */


#define I_BANNERS "banners-3"

typedef struct Ibanners
{
	INTERFACE_HEAD_DECL
	/* pyint: use */
	void (*SetBanner)(Player *p, Banner *banner);
	/* sets banner. NULL to remove. */
	/* pyint: player_not_none, banner -> void */
	void (*SetBannerFromPlayer)(Player *p, Banner *banner);
	/* sets banner. NULL to remove. Changes will be sent 
	 * on to the billing server. */
	/* pyint: player_not_none, banner -> void */
} Ibanners;

#endif

