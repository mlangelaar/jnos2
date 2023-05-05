#ifndef _TIMER_H
#define _TIMER_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
/* Software timers
 * There is one of these structures for each simulated timer.
 * Whenever the timer is running, it is on a linked list
 * pointed to by "Timers". The list is sorted in ascending order of
 * expiration, with the first timer to expire at the head. This
 * allows the timer process to avoid having to scan the entire list
 * on every clock tick; once it finds an unexpired timer, it can
 * stop searching.
 *
 * Stopping a timer or letting it expire causes it to be removed
 * from the list. Starting a timer puts it on the list at the right
 * place.
 *
 * 05Aug2010, Maiko (VE4KLM), the JNOS Clock is now 64 bits. Change the
 * 'expiration' variable here to 64 bits as well. This will fix the 25 day
 * negative timer problem that has plagued the linux JNOS from day one.
 */

struct timer {
    struct timer *next; /* Linked-list pointer */
    int32 duration;     /* Duration of timer, in ticks */
    int64 expiration;   /* Clock time at expiration */
    void (*func) __ARGS((void *));  /* Function to call at expiration */
    void *arg;      /* Arg to pass function */
    char state;     /* Timer state */
#define TIMER_STOP  0
#define TIMER_RUN   1
#define TIMER_EXPIRE    2
};
#define NULLTIMER   (struct timer *)0
#ifndef MSPTICK
#ifdef	SPEED_UP_TICK
#define MSPTICK     10      /* Milliseconds per tick */
#else
#define MSPTICK     55      /* Milliseconds per tick */
#endif
#endif
/* Useful user macros that hide the timer structure internals */
#define run_timer(t)    ((t)->state == TIMER_RUN)

extern volatile int Tick;

#ifndef UNIX
extern void (*Cfunc[])();   /* List of clock tick functions */
#endif
  
/*
 * In timer.c: - changed to j2alarm and j2pause - 02Apr2006, Maiko,
 * so that we no longer conflict with system calls, and we can get
 * rid of all those J_XXXX kludges, they're driving me nuts, and
 * making things complicated !
 */
void j2alarm __ARGS((int32 ms));
int j2pause __ARGS((int32 ms));

int32 read_timer __ARGS((struct timer *t));
int32 dur_timer __ARGS((struct timer *t));
void set_timer __ARGS((struct timer *t,uint32 x));
void start_timer __ARGS((struct timer *t));
void stop_timer __ARGS((struct timer *timer));
char *tformat __ARGS((int32 t));
  
int64 msclock (void);

int32 secclock __ARGS((void));
  
#endif  /* _TIMER_H */
