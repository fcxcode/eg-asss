
/* dist: public */

#ifndef __BILLING_H
#define __BILLING_H

typedef enum
{
	/* pyconst: enum, "BILLING_*" */
	BILLING_DISABLED = 0,
	BILLING_DOWN = 1,
	BILLING_UP = 2
} billing_state_t;

#define I_BILLING "billing-1"

typedef struct Ibilling
{
	INTERFACE_HEAD_DECL

	billing_state_t (*GetStatus)(void);
	/* returns one of the above constants */

	int (*GetIdentity)(byte *buf, int len);
	/* returns number of bytes written to buf, -1 on failure */

} Ibilling;

#define I_BILLING_FALLBACK "billing-fallback-1"

enum BillingFallbackResult
{
	BILLING_FALLBACK_MATCH,      /* player has entry, matches */
	BILLING_FALLBACK_MISMATCH,   /* player has entry, but wrong password */
	BILLING_FALLBACK_NOT_FOUND   /* player has no entry */
};

typedef struct Ibillingfallback
{
	INTERFACE_HEAD_DECL

	void (*Check)(Player *p, const char *name, const char *pwd,
	              void (*done)(void *clos, enum BillingFallbackResult result), void *clos);
} Ibillingfallback;

#endif

