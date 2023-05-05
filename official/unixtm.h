/*
 * We simulate the DOS date/time stuff instead of using the Unix ones because
 * the Unix ones require a lot of changed code.
 */

#ifndef _LXTIME_H

struct date
{
    int da_year;
    int da_day;
    int da_mon;
};

struct time
{
    int ti_min;
    int ti_hour;
    int ti_sec;
    int ti_hund;
};

/* 10Mar2009, Maiko, No more #undef getdate, now unique (j2XXX) to JNOS */
extern void j2getdate __ARGS((struct date *));
extern void j2gettime __ARGS((struct time *));

extern long dostounix __ARGS((struct date *, struct time *));

#define _LXTIME_H
#endif
