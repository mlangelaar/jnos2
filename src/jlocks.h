#ifndef _JLOCKS_H_
#define _JLOCKS_H_

/* 06Feb2006, Maiko, New header file for new lock functions */

#define MAXLKEY 4

#define JLOCK_NR	0
#define JLOCK_TCBS	1
#define JLOCK_TIMERS	2
#define JLOCK_USOCKS	3

extern void jnos_lock (int key);
extern void jnos_unlock (int key);
extern void jnos_lockstats (void);

#endif
