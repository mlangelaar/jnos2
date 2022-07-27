
/*
 * For Amateur Radio use only !!!
 *
 * Routines to track and resolve dynamic (dyndns type) gateways within
 * the IP Route table. This gives us the ability to assign dynamic gateways
 * to ENCAP routes. This feature was asked for by Bob (VE3TOK), something
 * he feels may become more popular over time. I can see this as well.
 *
 * 01Nov2004, Designed and Coded by Maiko Langelaar / VE4KLM
 *
 * $Author: ve4klm $
 *
 * $Date: 2010/11/04 15:58:49 $
 *
 * 04Apr2008, Added rtdyngw_remove, rtdyngw_display; Ronald Schubot / N8CML
 * 04Jan2009, Maiko (VE4KLM), Put Ron's changes into next official release.
 */

#include "global.h"

#include "ip.h"
#include "netuser.h"
#include "timer.h"

#ifdef	DYNGWROUTES

typedef struct gwnydtr {

	char *gwhost;	/* domain name of gateway */

	struct route *routerec;	/* pointer to routing table entry */

} RTDYNGW;

/* check domain names every 30 minutes */
#define	DGRESOLVTIMER	1800000

static RTDYNGW *rtdyngw;	/* list of routes having dynamic gateways */

static int maxdyngw = NAX25, numdyngw = 0;

static	struct timer resolv_t;	/* timer to re-resolve domain names */

static void startresolve (int32 interval);

void rtdyngw_add (char *hostname, struct route *ptr)
{
	if (numdyngw >= maxdyngw)
	{
		j2tputs ("exceeded maximum number of routes with dynamic gateways !");
		return;
	}

	rtdyngw[numdyngw].gwhost = j2strdup (hostname);

	rtdyngw[numdyngw].routerec = ptr;

	ptr->flags |= RTDYNGWROUTE; /* N8CML (Ron), set new flag */
    numdyngw++;

	log (-1, "rtdyngw: [%s] started at [%s]",
		hostname, inet_ntoa (ptr->gateway));

	startresolve (DGRESOLVTIMER);	/* make sure resolv timer is active */
}

/* ---- Nov2004, Maiko, New functions for dynamic gateways in routes */

static void dyngwresolve (int notused, void *uptr, void *uptr2)
{
	struct route *routerec;
	int32 ipaddr;
	RTDYNGW *ptr;
	int cnt;

	log (-1, "rtdyngw: resolving dynamic gateways");

	for (cnt = 0; cnt < numdyngw; cnt++)
	{
		ptr = &rtdyngw[cnt];

		routerec = ptr->routerec;

		if ((ipaddr = resolve (ptr->gwhost)))
		{
			if (ipaddr != routerec->gateway)
			{
				log (-1, "rtdyngw: [%s] changed to [%s]",
					ptr->gwhost, inet_ntoa (ipaddr));

				routerec->gateway = ipaddr;
			}
		}
		else log (-1, "rtdyngw: [%s] not resolved", ptr->gwhost);
	}

	start_timer (&resolv_t); /* restart the timer */
}

static void dyngwrtick (void *ptr)
{
	if (newproc ("rtgwdyn", 512, dyngwresolve, 0, ptr, NULL,0) == NULLPROC)
	{
		log (-1, "rtdyngw: could not start a resolver process");

		start_timer (&resolv_t);
	}
}

/* Main entry point for the resolver of remote host names for AXIP/AXUDP */

static void startresolve (int32 interval)
{
	static int oneshot = 0;

	if (oneshot)	/* do this only once !!! */
		return;

	log (-1, "rtdyngw: start timer to check routes with dynamic gateways");

	resolv_t.func = (void*)dyngwrtick;

	resolv_t.arg = NULL;

	set_timer (&resolv_t, interval);

	start_timer (&resolv_t);	/* start the timer */

	oneshot = 1;
}

void rtdyngw_init (int howmany)
{
	maxdyngw = NAX25;	/* set a default value */

	if (howmany > 0)
		maxdyngw = howmany;

	log (-1, "allocating maximum of %d dynamic gateways for routes",
		maxdyngw);

	rtdyngw = (RTDYNGW*)calloc ((unsigned) maxdyngw, sizeof (RTDYNGW));

	if (rtdyngw == (RTDYNGW*)0)
		exit (-1);
}

/*
 * 04Jan09, Maiko (VE4KLM), Added functions written and tested by N8CML (Ron)
 */

/* [n8cml, Ronald Schubot]
   routine to remove an entry from the rgdyngw table
   Each rtdyngw entry has a pointer to the route record
   it is related to. I pass the pointer to the route record
   I am trying to clean up and I search for that pointer in
   the rtdyngw table 
*/

void rtdyngw_remove (struct route *routeptr)
{
	char *ptr;
        int index=-1;

/* search for the route. The  */

	int cnt;
 
	for (cnt = 0; cnt < numdyngw; cnt++)
	{
	   if (rtdyngw[cnt].routerec==routeptr)
           {
              index = cnt;
              break;
           }
        }


	if (index < 0) return;
	
	/* remove the entry */ 

	ptr = rtdyngw[index].gwhost; /* save the one we are about to delete */

	log (-1, "deleting route %d",
		index);
	/* if more than one entry in the table,
        decrement numdyngw and copy the last
        entry to the entry we are deleting */

        if (numdyngw > 1) rtdyngw[index] = rtdyngw[--numdyngw];
	    else --numdyngw;   /* the last entry, no need to copy anything */
 
    /* clear out the deleted entry */
        rtdyngw[numdyngw].gwhost = NULL;
	    rtdyngw[numdyngw].routerec = NULL;
        free(ptr);       /* return the memory allocated by strdup */

	routeptr->flags &= ~RTDYNGWROUTE;

	return;
}

/* [n8cml, Ronald Schubot]
   routine to dumpthe rtdyngw table so we can see what is in it.
   invoked by the "route dyntrdump command" uses dumproute to display
   the route information
*/

extern void dumproute __ARGS((struct route *rp,char *p)); /* 04Jan09, Maiko */

void rtdyngw_dump (void)
{
	char temp[80];
  	int cnt;

	tprintf("Dynamic gateway count = %d of %d\n",numdyngw,maxdyngw);

	for (cnt = 0; cnt < numdyngw; cnt++)
	{
		tprintf("index %d dynroute is: %s\n",cnt,rtdyngw[cnt].gwhost);
		dumproute (rtdyngw[cnt].routerec,temp); /* get the route information */
        tprintf("%s\n",&temp[4]);
	}

}

#endif

