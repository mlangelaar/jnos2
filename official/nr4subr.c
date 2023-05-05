/*
 * nr4subr.c:  subroutines for net/rom transport layer.
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 */
  
#include "global.h"
#ifdef NETROM
#include "mbuf.h"
#include "timer.h"
#include "proc.h"
#include "ax25.h"
#include "netrom.h"
#include "nr4.h"
#include "lapb.h"
#include <ctype.h>

#define WZOC_FIX	/* 05Apr2023, Michael Ford (WZ0C) */
  
/* Get a free circuit table entry, and allocate a circuit descriptor.
 * Initialize control block circuit number and ID fields.
 * Return a pointer to the circuit control block if successful,
 * NULLNR4CB if not.
 */
  
struct nr4cb *
new_n4circ()
{
    int i ;
    struct nr4cb *cb ;
  
    for (i = 0 ; i <  NR4MAXCIRC ; i++)     /* find a free circuit */
        if (Nr4circuits[i].ccb == NULLNR4CB)
            break ;
  
    if (i == NR4MAXCIRC)    /* no more circuits */
        return NULLNR4CB ;

    cb = Nr4circuits[i].ccb =
    (struct nr4cb *)callocw(1,sizeof(struct nr4cb));
    cb->mynum = i ;
    cb->myid = Nr4circuits[i].cid ;
    return cb ;
}
  
  
/* Set the window size for a circuit and allocate the buffers for
 * the transmit and receive windows.  Set the control block window
 * parameter.  Return 0 if successful, -1 if not.
 */
  
int
init_nr4window(cb, window)
struct nr4cb *cb ;
unsigned window ;
{
    if (window == 0 || window > NR4MAXWIN) /* reject silly window sizes */
        return -1 ;
  
    cb->txbufs =
    (struct nr4txbuf *)callocw(window,sizeof(struct nr4txbuf));
  
    cb->rxbufs =
    (struct nr4rxbuf *)callocw(window,sizeof(struct nr4rxbuf));
  
    cb->window = window ;

#ifdef WZOC_FIX
/* 05Apr2023, Michael Ford (WZ0C) */
    cb->rxunacked = 0;
#endif
  
    return 0 ;
}
  
  
/* Free a circuit.  Deallocate the control block and buffers, and
 * increment the circuit ID.  No return value.
 */
  
void
free_n4circ(cb)
struct nr4cb *cb ;
{
    unsigned circ ;
/*
 * 31Oct2020, Maiko (VE4KLM), after getting a report of crashing
 * during a netrom disconnect, it might be an idea to validate the
 * control block before we start to free() things, wouldn't hurt.
 * 
    if (cb == NULLNR4CB)
        return ;
 */
	if (!nr4valcb (cb))
	{
		log (-1, "free_n4circ - (info) callback disappeared");
		return;
	}
  
    circ = cb->mynum ;
  
    if (cb->txbufs != (struct nr4txbuf *)0)
        free(cb->txbufs) ;
  
    if (cb->rxbufs != (struct nr4rxbuf *)0)
        free(cb->rxbufs) ;
  
    /* Better be safe than sorry: */
  
    free_q(&cb->txq) ;
    free_q(&cb->rxq) ;
  
    free(cb) ;
  
    if (circ > NR4MAXCIRC)      /* Shouldn't happen. */
	{
		/* 04Apr2023, Maiko (VE4KLM), Trying to nail down a nasty 'bug' ? */
		log (-1, "free_nr4circ exceeded nr4maxcirc");
        return ;
	}
  
    Nr4circuits[circ].ccb = NULLNR4CB ;
  
    Nr4circuits[circ].cid++ ;
}
  
/* See if any open circuit matches the given parameters.  This is used
 * to prevent opening multiple circuits on a duplicate connect request.
 * Returns the control block address if a match is found, or NULLNR4CB
 * otherwise.
 */
  
struct nr4cb *
match_n4circ(index, id, user, node)
int index ;                 /* index of remote circuit */
int id ;                    /* id of remote circuit */
char *user ;    /* address of remote user */
char *node ;    /* address of originating node */
{
    int i ;
    struct nr4cb *cb ;
  
    for (i = 0 ; i < NR4MAXCIRC ; i++) {
        if ((cb = Nr4circuits[i].ccb) == NULLNR4CB)
            continue ;      /* not an open circuit */

        if ((int)(cb->yournum) == index && (int)(cb->yourid) == id
            && addreq(cb->remote.user,user)
            && addreq(cb->remote.node,node))
            return cb ;
    }
    /* if we get to here, we didn't find a match */
  
    return NULLNR4CB ;
}
  
/* Validate the index and id of a local circuit, returning the control
 * block if it is valid, or NULLNR4CB if it is not.
 */
  
struct nr4cb *
get_n4circ(index, id)
int index ;             /* local circuit index */
int id ;                /* local circuit id */
{
    struct nr4cb *cb ;
  
    if (index >= NR4MAXCIRC)
        return NULLNR4CB ;
  
    if ((cb = Nr4circuits[index].ccb) == NULLNR4CB)
        return NULLNR4CB ;

    if ((int)(cb->myid) == id)
        return cb ;
    else
        return NULLNR4CB ;
}
  
/* Return 1 if b is "between" (modulo the size of an unsigned char)
 * a and c, 0 otherwise.
 */
  
int
nr4between(a, b, c)
unsigned a, b, c ;
{
    if ((a <= b && b < c) || (c < a && a <= b) || (b < c && c < a))
        return 1 ;
    else
        return 0 ;
}
  
/* Set up default timer values, etc., in newly connected control block.
 */
  
void
nr4defaults(cb)
struct nr4cb *cb ;
{
    int i ;
    struct timer *t ;
  
    if (cb == NULLNR4CB)
        return ;
  
    /* Set up the ACK and CHOKE timers */
  
    set_timer(&cb->tack,Nr4acktime) ;
    cb->tack.func = nr4ackit ;
    cb->tack.arg = cb ;
  
    set_timer(&cb->tchoke,Nr4choketime) ;
    cb->tchoke.func = nr4unchoke ;
    cb->tchoke.arg = cb ;
  
    cb->rxpastwin = cb->window ;
  
    /* Don't actually set the timers, since this is done */
    /* in nr4sbuf */
  
    for (i = 0 ; i < (int)(cb->window) ; i++) {
        t = &cb->txbufs[i].tretry ;
        t->func = nr4txtimeout ;
        t->arg = cb ;
    }
}

/*
 * 10Mar2021, Maiko (VE4KLM), Similar to what I did for ax25cmd
 * way back in 2007, retrieve netrom CB using a remote call, or
 * continue to allow using the hex value from 'netrom status'.
 */

struct nr4cb *getnr4cb (char *nrcbid)
{
   register struct nr4cb *nr4p, *cb = NULLNR4CB;

   char nr4rcall[AXALEN];

   int i;

   if (isdigit (*nrcbid))
   {
      nr4p = (void*)htol (nrcbid);

      if (nr4p != NULLNR4CB)
      {
        for (i = 0 ; i < NR4MAXCIRC ; i++)
		{
           if ((cb = Nr4circuits[i].ccb) == NULLNR4CB)
            	continue ;      /* not an open circuit */

           if (cb == nr4p)
             break;
		}
      }
    }
    else
    {
        setcall (nr4rcall, nrcbid);

        for (i = 0 ; i < NR4MAXCIRC ; i++)
        {
           if ((cb = Nr4circuits[i].ccb) == NULLNR4CB)
            	continue ;      /* not an open circuit */

          if (addreq (cb->remote.user, nr4rcall))
             break;
		}
    }

    return cb;
}

/* See if this control block address is valid */
  
int
nr4valcb(cb)
struct nr4cb *cb ;
{
    int i ;
  
    if (cb == NULLNR4CB)
        return 0 ;
  
    for (i = 0 ; i < NR4MAXCIRC ; i++)
        if (Nr4circuits[i].ccb == cb)
            return 1 ;
  
    return 0 ;
}
  
#ifndef UNIX
  
void
nr_garbage(red)
int red;
{
    int i;
    struct nr4cb *ncp;
  
    for(i=0;i<NR4MAXCIRC;i++){
        ncp = Nr4circuits[i].ccb;
        if(ncp != NULLNR4CB)
            mbuf_crunch(&ncp->rxq);
    }
}
#endif /* UNIX */
  
#endif /* NETROM */
  
