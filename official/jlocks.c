/*
 * New locking mechanism for JNOS - experimental !!!
 *
 * 03Feb2006, Maiko Langelaar / VE4KLM
 */

#include "proc.h"

#include "jlocks.h"

typedef struct lockj {
	int conflicts;
	int locked;
} JLOCK;

static JLOCK jlocks[MAXLKEY];

void jnos_lock (int key)
{
	JLOCK *ptr = &jlocks[key];

	if (ptr->locked)
	{
		ptr->conflicts++;

		while (ptr->locked)
			pwait (NULL);
	}

	ptr->locked = 1;
}

void jnos_unlock (int key)
{
	JLOCK *ptr = &jlocks[key];

	ptr->locked = 0;
}

void jnos_lockstats (void)
{
	tprintf ("Locking conflicts => netrom %d tcbs %d timers %d usocks %d\n",
		jlocks[JLOCK_NR].conflicts, jlocks[JLOCK_TCBS].conflicts,
			jlocks[JLOCK_TIMERS].conflicts, jlocks[JLOCK_USOCKS].conflicts);
}

