/* General purpose software timer facilities
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "timer.h"
#include "proc.h"
#include "session.h"
#include "mbuf.h"
#include "commands.h"
#include "daemon.h"
#include "unix.h"
#include "socket.h"
  
/* Head of running timer chain.
 * The list of running timers is sorted in increasing order of expiration;
 * i.e., the first timer to expire is always at the head of the list.
 */
struct timer *Timers;
  
static void t_alarm __ARGS((void *x));
  
/* Process that handles clock ticks */
/* Fixed to solve some timing problems when multiple ticks
 * get handled at once... from Walt Corey, KZ1F
 */
void timerproc (int i, void *v1, void *v2)
{
    register struct timer *t;

    int i_state;
  
    for(;;)
    {
        /* Atomic read and decrement of Tick */
        i_state = dirps();
        while(Tick == 0)
            pwait(&Tick);
        Tick = 0;
        restore(i_state);
  
        if(!istate()){
            restore(1);
            tprintf("timer: ints were off!\n");
        }
  
        usflush(Current->output);  /* Flush current session output */

        pwait(NULL);    /* Let them all do their writes */
  
        if(Timers == NULLTIMER)
            continue;       /* No active timers, all done */
  
        /* Initialize null expired timer list */
        while((t=Timers)!=NULLTIMER && (t->expiration - JnosClock) <= 0) {
  
            Timers = t->next;
            t->state = TIMER_EXPIRE;
            if(t->func)
                (*t->func)(t->arg);
        }
        pwait(NULL);    /* Let them run before handling more ticks */
    }
}
/* Start a timer */
void
start_timer(t)
struct timer *t;
{
    register struct timer *tnext;
    struct timer *tprev = NULLTIMER;
  
    if(t == NULLTIMER)
        return;
    if(t->state == TIMER_RUN)
        stop_timer(t);
    if(t->duration == 0)
        return;         /* A duration value of 0 disables the timer */
  
    t->next = NULLTIMER;        /* insure forward chain is NULL */
    t->expiration = JnosClock + t->duration;
    t->state = TIMER_RUN;
  
    /* Find right place on list for this guy. Once again, note use
     * of subtraction and comparison with zero rather than direct
     * comparison of expiration times.
     */
    for(tnext = Timers;tnext != NULLTIMER;tprev=tnext,tnext = tnext->next){
        if((tnext->expiration - t->expiration) >= 0)
            break;
    }
    /* At this point, tprev points to the entry that should go right
     * before us, and tnext points to the entry just after us. Either or
     * both may be null.
     */
    if(tprev == NULLTIMER)
        Timers = t;             /* Put at beginning */
    else
        tprev->next = t;
  
    t->next = tnext;
}
/* Stop a timer */
void
stop_timer(timer)
struct timer *timer;
{
    register struct timer *t;
    struct timer *tlast = NULLTIMER;
  
    if(timer == NULLTIMER || timer->state != TIMER_RUN)
        return;
  
    /* Verify that timer is really on list */
    for(t = Timers;t != NULLTIMER;tlast = t,t = t->next)
        if(t == timer)
            break;
  
    if(t == NULLTIMER)
        return;         /* Should probably panic here */
  
    /* Delete from active timer list */
    if(tlast != NULLTIMER)
        tlast->next = t->next;
    else
        Timers = t->next;       /* Was first on list */
  
    t->state = TIMER_STOP;
}
/* Return milliseconds remaining on this timer */
int32
read_timer(t)
struct timer *t;
{
    int32 remaining;
  
    if(t == NULLTIMER || t->state != TIMER_RUN)
        return 0;
    remaining = (int32)(t->expiration - JnosClock);
    if(remaining <= 0) remaining = 0;       /* Already expired */
    else
    {
        long long i64;

        i64 = (long long)remaining * 720896L;  /* 65536 * 11 */
        i64 = i64 / 13125;
        remaining = (int32)(i64 & 0xFFFFFFFFL);
    }
    return remaining;
}

/* Return millisecond duration of this timer */
int32
dur_timer(t)
struct timer *t;
{
    int32 dur;
    long long i64;
  
    if(t == NULLTIMER)
        return 0;

    dur = t->duration;

    i64 = (long long)dur * 720896L;  /* 65536 * 11 */
    i64 = i64 / 13125;
    dur = (int32)(i64 & 0xFFFFFFFFL);

    return dur;
}

void
set_timer(t,interval)
struct timer *t;
uint32 interval;
{
    if(t == NULLTIMER)
        return;
    /* Round the interval up to the next full tick, and then
     * add another tick to guarantee that the timeout will not
     * occur before the interval is up. This is necessary because
     * we're asynchonous with the system clock.
     * N5KNX note: since MSPTICK is just the closest integer to the exact value
     * 54.92541, we must use the exact value to maintain accuracy.  Drawback is
     * that the computations take longer than just using MSPTICK.
     */
    if(interval == 0)
        t->duration = 0;
    else
    {
        long long i64;

        i64 = (long long)interval * 13125;
        i64 = i64 / 720896L;
        t->duration = 1 + (int32)(i64 & 0xFFFFFFFFL);
    }
}
/* Delay process for specified number of milliseconds.
 * Normally returns 0; returns -1 if aborted by alarm.
 *
 * 02Apr2006, Maiko, renamed 'pause' to 'j2pause' to avoid
 * any further conflicts with the long standardized system
 * call of the same name. Did the same with 'alarm' call.
 */
int j2pause (int32 ms)
{
    int val;
  
    if(Curproc == NULLPROC || ms == 0)
        return 0;
    j2alarm(ms);
    /* The actual event doesn't matter, since we'll be alerted */
    while(Curproc->alarm.state == TIMER_RUN){
        if((val = pwait(Curproc)) != 0)
            break;
    }
    j2alarm(0); /* Make sure it's stopped, in case we were killed */
    return (val == EALARM) ? 0 : -1;
}
static void
t_alarm(x)
void *x;
{
    alert((struct proc *)x,EALARM);
}
/* Send signal to current process after specified number of milliseconds */
void j2alarm (int32 ms)
{
    if(Curproc != NULLPROC){
        set_timer(&Curproc->alarm,ms);
        Curproc->alarm.func = t_alarm;
        Curproc->alarm.arg = (char *)Curproc;
        start_timer(&Curproc->alarm);
    }
}
/* Convert time count in seconds to printable days:hr:min:sec format */
char *
tformat(t)
int32 t;
{
    static char buf[17],*cp;
    unsigned int days,hrs,mins,secs;
    int minus;
  
    if(t < 0){
        t = -t;
        minus = 1;
    } else
        minus = 0;
  
    secs = (unsigned int)(t % 60);
    t /= 60;
    mins = (unsigned int)(t % 60);
    t /= 60;
    hrs = (unsigned int)(t % 24);
    t /= 24;
    days = (unsigned int) t;
    if(minus){
        cp = buf+1;
        buf[0] = '-';
    } else
        cp = buf;
    sprintf(cp,"%u:%02u:%02u:%02u",days,hrs,mins,secs);
  
    return buf;
}
  
