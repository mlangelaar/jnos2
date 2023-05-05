/*
 * JNOS 2.0
 *
 * $Id: newinp.c,v 1.1 2015/04/22 01:51:45 root Exp root $
 *
 * New INP support - continuation of the work I originally started several
 * years ago (around 2005) but subsequently shelved out of frustration and
 * instability of the code at the time. This new code seems to be working
 * quite well, and is very stable (so far). I spent a considerable amount
 * of my time from August 21 to August 29 getting this new code working.
 *
 * This first release is what I call a 'Read Only' implementation of INP,
 * since it has no provision yet to transmit RIF frames to neighbours. I
 * have listed below what it currently does and the main *features* :
 *
 * 1. JNOS will send regular L3RTT queries at 5 minute intervals to any netrom
 *    neighbours it knows about. Those neighbours supporting INP will reflect
 *    the query back at us, allowing us to calculate a round trip time for the
 *    neighbour and subsequently update the quality, hops, and round trip time
 *    for all routes to nodes going through that particular neighbour.
 *
 * 2. JNOS will respond to any L3RTT queries it receives from INP neighbours
 *    by doing the same - reflecting the query back at them, allowing them to
 *    calculate and update routes and similar information on their side.
 *
 * 3. JNOS will process incoming RIF frames from INP neighbours, calculating
 *    quality values based on 'number of hops' and 'trip times' from the RIP
 *    entries within the RIF frames, then updating, adding, deleting entries
 *    from our netrom node tables based on these calculated quality values.
 *
 *    Note : corrupt alias values are ignored, and routes (bindings) with
 *    too low quality are deleted if they exist in our netrom node tables.
 *
 *    So it is NOT uncommon to see constant changes to numbers of nodes.
 *
 * 4. This release of JNOS does NOT transmit RIF frames to neighbours, and
 *    their is no provision for so called 'negative node information'. That
 *    stuff will be dealt with in a subsequent release. That's why I refer
 *    to this preliminary version as a 'Read Only' implemenation.
 *
 * Note : use 'set ts=4' if using the VI(M) editor
 *
 * (C)opyright, 2005-2011, by Maiko Langelaar (VE4KLM)
 *
 * with ideas and code snippets from the PE1RXQ 2.6.4 kernel patch
 *
 */

#include <ctype.h>

#include "global.h"

#if defined (NETROM) && defined (INP2011)

#include "ax25.h"

#include <sys/time.h>

#include "mbuf.h"
#include "netrom.h"
#include "cmdparse.h"

#ifndef OPTIONAL
#define OPTIONAL
#endif

#define L3RTT_INTERVAL	300000	/* 5 minutes (5 x 60 x 1000) */
#define	TT_HORIZON 60000	/* 13Nov2013, Maiko, Bad mistake, was 6000 */
#define	NR_INFO 0x05

/* Define INP state constants */

enum {
	NR_INP_STATE_0,		/* Not recognized as an INP neighbour */
	NR_INP_STATE_RTT,	/* Got RTT back, but no RIPs yet... */
	NR_INP_STATE_INP,	/* Recognized as a full INP neighbour */
};

struct timer INPtimer;

static char L3RTTcall[AXALEN] = {
    'L' << 1, '3' << 1, 'R' << 1, 'T' << 1, 'T' << 1, ' ' << 1, '0' << 1
};

/* 12Sep2011, Maiko (VE4KLM), Check for NOCALL values in RIF frames */
static char Nocall[AXALEN] = {
    'N' << 1, 'O' << 1, 'C' << 1, 'A' << 1, 'L' << 1, 'L' << 1, '0' << 1
};

/* 21Sep2011, Maiko, No longer static, need it outside this module */
int INP_active = 0;

extern char Nralias[];

extern unsigned Obso_init;	/* 05Sep2011, Maiko, in nr3.c */

extern unsigned Nr_maxroutes;

extern struct nr_bind *find_bind (struct nr_bind*, struct nrnbr_tab*);

extern void nr_unbind (struct nrroute_tab*,
		struct nrnbr_tab*, struct nr_bind*);

/* 28Aug2011, Maiko (VE4KLM), function prototypes are important here */
extern struct nrroute_tab *add_nrroute (char*, char*);
extern struct nr_bind *add_binding (struct nrnbr_tab*, struct nrroute_tab*);

/* 07Sep2011, Maiko (VE4KLM), better prototype this */
extern int nr_direct (struct mbuf*, char*, struct iface*, struct nr3hdr*);

/* 01Sep2011, Maiko (VE4KLM), couple more prototypes from nr3.c module */
extern void nr_routedrop_postread (char*, char*, struct nrroute_tab*,
	struct nrnbr_tab*, struct nr_bind*);
extern int nr_finally_route (struct mbuf*, struct nrroute_tab*,
	struct ax25_cb*, struct nr3hdr*);

#define	IGNORE_POUND_TYPE_ALIAS	/* 12Sep2011, Maiko (VE4KLM), Strict */

/*
 * 03Nov2013, Maiko, need the ability to better configure INP3 functionality,
 * first of all, the need for the sysop to maintain a list of INP3 capable
 * stations to which we will actually use the INP3 protocol with, instead of
 * blindly doing INP3 with every netrom interface we have configured. This to
 * replace the 'vhf' kludge I created back on 20Feb2012, see code further on.
 *
 * Note this is a classic way of how I do lists of strings, pointer to pointer
 * programming, very dynamic in nature, no preconfigured arrays and such, and
 * it works quite well. I've been trying to do it this way for the past few
 * years now. Try it out yourself, you might just like the elegance of it :)
 *
 */

static char **inp3_ifaces = (char**)0;	/* 04Nov2013, Maiko */

static void usageINPifaces ()
{
	tprintf ("usage: inp ifaces *[if1 if2 if3 ... ifN]\n");
}

int isINPiface (char *ifcname)
{
	char **tptr;

	if (inp3_ifaces)
	{
		tptr = inp3_ifaces;

		while (*tptr)
		{
			if (!strcmp (*tptr, ifcname))
			{
				// log (-1, "inp3 [%s] iface", *tptr);
				return 1;
			}

			tptr++;
		}
	}

	return 0;	/* default is we are NOT an INP3 iface */
}

int doINPifaces (int argc, char **argv, void *p)
{
	char **tptr;
	int cnt;

	/* if no stations are given, then just show the existing list */

	if (argc < 2)
	{
		if (inp3_ifaces)
		{
			tptr = inp3_ifaces;

			while (*tptr)
				tprintf ("%s ", *tptr++);

			tprintf ("\n");
		}
		else usageINPifaces ();

		return 0;
	}

	/* if list exists already, then replace it */

	if (inp3_ifaces)
	{
		tptr = inp3_ifaces;

		while (*tptr)
			free (*tptr++);

		free (inp3_ifaces);

		inp3_ifaces = (char**)0;
	}

	/* now build the list */

	inp3_ifaces = (char**)calloc (argc+2, sizeof(char*));

	if (inp3_ifaces == (char**)0)
	{
		log (-1, "[inp %s] no memory", argv[0]);

		return -1;
	}

	for (tptr = inp3_ifaces, cnt = 0; cnt < (argc - 1); cnt++)
	{
		*tptr++ = j2strdup (argv[cnt+1]);
	}

	*tptr = (char*)0;	/* null terminate the list !!! */

	return 0;
}

static struct cmds DFAR INPcmds[] = {
    { "ifaces",      doINPifaces,    0, 0,   NULLCHAR },
    { NULLCHAR,	NULL,	0, 0, NULLCHAR }
};

int doinp (int argc, char **argv, void *p)
{
    return subcmd (INPcmds, argc, argv, p);
}
  

/* 22Sep2011, Maiko, Function to check for NOCALL nodes */
int callnocall (char *nodecall)
{
	if (addreq (nodecall, Nocall))
	{
		if (Nr_debug)
			log (-1, "ignore NOCALL node");

		return 1;
	}

	return 0;
}

/* 22Sep2011, Maiko (VE4KLM), New function to check for tmp entries */
int alias2ignore (char *alias)
{
    char Ualias[AXALEN];

#ifdef IGNORE_POUND_TYPE_ALIAS
	/* 12Sep2011, Maiko, Ignore alias starting with # character */
	if (*alias == '#')
	{
		if (Nr_debug)
			log (-1, "ignoring [%s] alias", alias);

		return 1;
	}

#endif
	strncpy (Ualias, alias, 6);

	strupr (Ualias);

	if (strstr (Ualias, "#TMP") || strstr (Ualias, "#TEMP"))
	{
		if (Nr_debug)
			log (-1, "ignore [%s] alias", alias);

		return 1;
	}

	return 0;	
}

/* #define	ROOT_QUALITY 254 */

#define	ROOT_QUALITY 230

/* 23Aug2011, Maiko (VE4KLM), needed to process incoming RIP, from orig code */
static int rtt2qual (int rtt, int hops)
{
	int qual;

	if (rtt >= TT_HORIZON)
		return 0;

	if (hops >= 255)
		return 0;
	
	qual = ROOT_QUALITY - (rtt / 20);

	if (qual > ROOT_QUALITY + 2 - hops)
		qual = ROOT_QUALITY - hops;

	if (qual < 1)
		qual = 0;

	return qual;
}

/* 21Aug2011, Maiko (VE4KLM), new version of RIF/RIP receive function,
 * by evening it seems to be working quite nicely, just have to adjust
 * a few items (ie, how things are logged). Next step is to use the data
 * contained in the RIF/RIP entries to adjust node tables, etc ...
 *
 * 23Aug2011, Maiko (VE4KLM), basic update of node tables from RIF frames
 * is technically complete, just need to look at negative node stuff now,
 * which I am not familiar with yet, need to read up on that first. I've
 * decided for now to just find NR neighbour ONCE (see below), see how
 * stable INP code is and then make a decision further down the road.
 */


int inp_rif_recv (struct mbuf *bp, struct ax25_cb *ax25)
{
	unsigned char nr_data[300], *dptr;	/* pullup & unsigned vars, grrr */
	int quality, transportime, hops, dlen;
	char nodecall[AXALEN], tmp[AXBUF];
	int opt_field_len, opt_field_type;
	struct nrroute_tab *nr_node;
	struct nrnbr_tab *nr_neigh;
	char logdata[150], *lptr;
	struct nr_bind *nrbp;

	int alias_corrupt;	/* 25Aug2011, Maiko, More alias protection systems */

	char alias[ALEN+1]; /* 25Aug2011, Maiko, Use ALEN, add 1 for terminator */

	dlen = len_p (bp) - 1;	/* skip 0xff */

	if (dlen > 298)
		log (-1, "warning: inp_rif_recv nr_data overflow !!!");

	dptr = nr_data + 1;		/* skip 0xff */

 	/* read all of it, 300 is just a max */
	pullup (&bp, (char*)nr_data, 300);

	/*
	 * 30Sep2011, Maiko (VE4KLM), Playing around with stack size checks. All
	 * of these new INP features seems to have pushed us into stack overflow,
	 * so bumping it up in config.c seems to have resolved alot of problems.
	 *
	if ((Curproc->stksize - stkutil (Curproc)) < 500)
	{
		log (-1, "WARNING : increase stacksize for [%s] proc !!!",
			Curproc->name);
	}
	 */

	/*
	 * 27Aug2011, Maiko (VE4KLM), Interesting, just like back in 05Nov2005 in
	 * my original attempt to do the INP stuff, why is it for only Xnet traffic
	 * that the PTR to ax25 becomes NULL and crashes the damn system ?
	 *
	 * Okay, after looking at the trace data it would appear that Xnet sends
	 * us TWO (2) identical netrom node broadcasts one after the other, the
	 * first one to NODES, the second one to VE4KLM-3 (JNOS netrom call). I
	 * think JNOS treats the second one like it is not a multicast (it has no
	 * way of knowing actually), and tries to pass it through as a connected
	 * netrom circuit which there is no level 2 connection for, so we get
	 * the NULL ptr situation. I'm pretty certain of this now, just need
	 * to fix it somehow. The fix below works, nothing is lost, and the
	 * system is stable for now.
	 *
	 * Thanks to VE2PKT-4 (Xrouter) and I0OJSS (Xnet) for letting me abuse
	 * their systems (via direct axip/axudp links I have with them), while
	 * testing this code, and probably messing up their node tables from
	 * time to time - hi hi < grin >
	 *
	 */

	/* 03Sep2011, Maiko, Decided to put this code into config.c instead */
#ifdef	DONT_COMPILE
	/* 05Nov2005, Maiko, gdb (debugger) is showing NULL value for ax25 ptr */
	if (ax25 == NULL)
	{
		if (Nr_debug)
			log (-1, "rif_recv - NULL ax25 - Xnet netrom bc to non-multicast ?");
		return 0;
	}
#endif

	if ((nr_neigh = find_nrnbr (ax25->remote, ax25->iface)) == NULLNTAB)
	{
		/* 24Aug2011, Maiko, New function separated out in nr3.c */
		if ((nr_neigh = add_nrnbr (ax25->remote, ax25->iface)) == NULLNTAB)
		{
			if (Nr_debug)
			{
				log (-1, "add neighbor [%s] failed",
					pax25 (tmp, ax25->remote));
			}

			return 0;
		}
		else if (Nr_debug)
		{
			log (-1, "add neighbor [%s]", pax25 (tmp, ax25->remote));
		}
	}

	while (dlen > 0)
	{
		memcpy (nodecall, dptr, AXALEN);
		dptr += AXALEN;
		hops = *dptr++;
		transportime = (*dptr++ << 8);
		transportime += *dptr++;
		dlen = dlen - AXALEN - 3;

		alias_corrupt = 0;	/* 25Aug2011, Maiko, Assume alias will be fine */

		*alias = 0;		/* 25Aug2011, Maiko, very important for alias check */

		if (Nr_debug)
		{
			lptr = logdata;	/* important */

			lptr += sprintf (lptr, "node [%s] hops [%d] tt [%d]",
				pax25 (tmp, nodecall), hops, transportime);
		}

		while (1)
		{
			opt_field_len = *dptr++;
			dlen--;

			if (!opt_field_len)
				break;

			if (dlen < opt_field_len)
			{
				log (-1, "Whoa ! dlen %d optlen %d", dlen, opt_field_len);
				break;
			}

			opt_field_type = *dptr++;
			dlen--;

			opt_field_len -= 2;

			switch (opt_field_type)
			{
				case 0:
				/*
				 * 28Sep2011, Maiko, I am again such an idiot. Believe it
				 * or not, if I had netrom debugging switched off, the alias
				 * was never being processed, which in turn would generate
				 * tons of ##TEMP entries - completely slipped my mind :(
				 *
					if (Nr_debug)
					{
				 */
					{
					/* 25Aug2011, Maiko, Alias must be space padded */
					int tlen = opt_field_len, plen = ALEN - tlen;

					unsigned char *aptr = (unsigned char*)alias, *tptr = dptr;
				/*
				 * 21Aug2011, Maiko, noticing corrupt alias values from time
				 * to time, which locks up any 'tail -f' of log file, better
				 * screen for non printables, and just replace those values
				 * with '?' instead to prevent this from happening - done.
				 * 24Aug2011, Maiko, need alias for route add later on.
				 */
					while (tlen > 0)
					{
						if (isprint (*tptr))
							*aptr = *tptr;
						else
						{
							*aptr = '?';

							/*
							 * 25Aug2011, Maiko, I still want to log this,
							 * however we have to enforce the fact that the
							 * alias is now corrupt, and no route added.
							 */

							alias_corrupt = 1;
						}

						tptr++;
						aptr++;
						tlen--;
					}

					/* 25Aug2011, Maiko, Alias must be space padded */
					while (plen > 0)
					{
						*aptr = ' ';
						aptr++;
						plen--;
					}

					*aptr = 0;

					}

					/* 25Aug2011, Maiko, Lastly, upcase it !!! */
					strupr (alias);

					if (Nr_debug)
						lptr += sprintf (lptr, " alias [%s]", alias);

					break;

				case 1:
					if (Nr_debug)
					{
						/* 25Aug2011, Maiko, ip address is straight forward */
						lptr += sprintf (lptr, " ip [%u.%u.%u.%u/%u]", *dptr,
							*(dptr+1), *(dptr+2), *(dptr+3), *(dptr+4));
						/*
						lptr += sprintf (lptr, " ip [%.15s]",
							inet_ntoa ((int32)(*dptr)));
						*/
					}
					break;

				default:
					if (Nr_debug)
					{
						lptr += sprintf (lptr, " unknown field type [%d]",
							opt_field_type);
					}
					break;
			}

			dptr += opt_field_len;

			dlen -= opt_field_len;
		}

		if (Nr_debug)
			log (-1, "rif [%s] %s", ax25->iface->name, logdata);

		nr_neigh->inp_state = NR_INP_STATE_INP;	/* rifs means active INP */

		/* 25Aug2011, Maiko, Ignore records containing a corrupt alias */
		if (*alias && alias_corrupt)
		{
			if (Nr_debug)
				log (-1, "ignore corrupt alias");

			continue;
		}

		/* 22Sep2011, Maiko (VE4KLM), ignore tmp and such entries */
		if (*alias && alias2ignore (alias))
			continue;

		/*
		 * 05Sep2011, Maiko (VE4KLM), I've decided NOT to trust any RIF
		 * info for a node call that is also my immediate neighbour. I am
		 * seeing ZERO transport times in some cases, which I originally
		 * thought can possibly over inflate the quality of a neighbour.
		 * I've decided instead to use l3rtt responses to MY queries to
		 * do this with my immedidate neighbours - I trust that more.
		 *
		 * 07Sep2011, I am an idiot ! The INP specification quite clearly
		 * says NOT to use the TT of immediate neighbour nodes but rather
		 * use the RTT time calculated from l3rtt mechanism.
		 * 
		 */

		if (addreq (nodecall, nr_neigh->call))
		{
			if (Nr_debug)
				log (-1, "ignore RIP to oneself");

			continue;
		}

		/* 12Sep2011, Maiko, Ignore any NOCALL nodes */
		/* 22Sep2011, Maiko (VE4KLM), Moved into a new function */
		if (callnocall (nodecall))
			continue;

		/* are we over the horizon ? */

		if (transportime + nr_neigh->rtt > TT_HORIZON || hops == 255)
			transportime = TT_HORIZON;

		quality = rtt2qual (nr_neigh->rtt + transportime, hops);

		/*
		 * 28Aug2011, Maiko, Okay, lets drop routes on low quality, instead
		 * of just ignoring them, I think that's a cleaner approach, and alot
		 * more realtime in nature, the whole point of the RIF system is to
		 * have quick and dynamic route changes.
		 *
		 */

#define	DROP_LOW_QUAL_ROUTES	/* 29Aug2011 */

#ifndef	DROP_LOW_QUAL_ROUTES

		/* 25Aug2011, ignore routes below the minimum quality threshhold */
		if (quality < Nr_autofloor)
		{
			/*
			 * 28Aug2011, Maiko, I wonder if we should actually let it go
			 * to the point where if we find a BINDING, then drop the route
			 * at that stage (drop the binding via this neighbour). I think
			 * that's how the INP spec actually reads come to think of it.
			 */
			if (Nr_debug)
				log (-1, "ignore low quality");

			continue;
		}
#endif
		/*
		 * 24Aug2011, Maiko (VE4KLM), I want to try something different and
		 * avoid using nr_routeadd() simply because it contains an extra call
		 * to find_nrnbr() which I already did earlier on in this function.
		 *
		 * That just creates more work for the CPU, although maybe I am being
		 * too picky about this, but at least I have more control this way.
		 *
		nrbp = nr_routeadd (alias, nodecall, ax25->iface,
			quality, nr_neigh->call, 0, 0);
		 *
		 */

		/* get the node entry for this particular RIP record */
		if ((nr_node = find_nrroute (nodecall)) == NULLNRRTAB)
		{

/* 12Sep2011, Maiko, I am seeing way too many '##TEMP' entries */
#define	ADD_NODE_EVEN_LOW_QUALITY

#ifndef	ADD_NODE_EVEN_LOW_QUALITY

			/* 28Aug2011, Maiko, No point adding a low quality node */
			if (quality < Nr_autofloor)
			{
		/*
		 * 12Sep2011, Maiko, Add node to get the alias if it's there. Just
		 * make sure not to bind this record. I need to do this, because at
		 * times it appears I have far too many '##TEMP' entries in the nodes
		 * listing. So far, this fix appears to be working fairly well ...
		 *
		 * After running this for a while, I've noticed something interesting
		 * in that a node may now be listed if you enter 'nodes', but shows no
		 * routes if you enter a 'nodes <callsign>'. Actually, I think I like
		 * this alot more, because it advertises the fact that a node exists
		 * and people will notice - even if no routes may exist at the time. 
		 *
		 * 22Sep2011, Maiko, Bottom line, in order to preserve ALIAS I will
		 * need to add node to nrroute table no matter what quality it is,
		 * that may add a bit of overhead, but the benefit is there.
		 */
				if (Nr_debug)
					log (-1, "don't add node, too low quality");

				continue;
			}
#endif
			/* 28Aug2011, Maiko, Add route as a ##TEMP alias instead */
			if (!(*alias))
				strcpy (alias, "##TEMP");

			/* 24Aug2011, Maiko, New function separated out in nr3.c */
			if ((nr_node = add_nrroute (alias, nodecall)) == NULLNRRTAB)
			{
				if (Nr_debug)
					log (-1, "add node failed");

				continue;
			}
			else if (Nr_debug)
			{
				/* 01Sep2011, Maiko, cleanup debug info, make it more useful */
				if (strcmp (alias, "##TEMP"))
					log (-1, "add node");
				else
					log (-1, "add node, temporary alias");
			}
		}
		else if (*alias && !strnicmp (nr_node->alias, "##TEMP", 6))
		{
			/* 28Aug2011, Maiko, update alias if it was a ##TEMP entry */
			if (Nr_debug)
				log (-1, "update node, new alias");

			strncpy (nr_node->alias, alias, 6);
		}

	 	/* find the route that binds the node to current neighbour */
		if ((nrbp = find_bind (nr_node->routes, nr_neigh)) == NULLNRBIND)
		{
			/* 28Aug2011, Maiko, No point binding a low quality node */
			if (quality < Nr_autofloor)
			{
				if (Nr_debug)
					log (-1, "don't bind node, too low quality");

			/*
			 * 06Sep2011, Maiko (VE4KLM), Interesting, a rif comes in from que
			 * for node ve3mch-8, we don't bind, yet when I do 'n ve3mch-8' I
		 	 * see an obsolete and RECORDED route with tt, but NO hops. It
			 * should not be visible. Do I need to delete something else ?
			 */
				continue;
			}

			/* 24Aug2011, Maiko, New function separated out in nr3.c */
			if ((nrbp = add_binding (nr_neigh, nr_node)) == NULLNRBIND)
			{
				if (Nr_debug)
					log (-1, "bind node failed");

				continue;
			}
			else if (Nr_debug)
				log (-1, "bind node");
		}

		/* 24Aug2011, Maiko (VE4KLM), nr_routeadd() checks if we have too
		 * many bindings, we should probably do the same, out of curiosity
		 * I am just going to log a warning to watch how often it happens.
		 *
		 * 04Jan2014, Maiko, Noticed warnings, better deal with it now.
		 */
		if (nr_node->num_routes > Nr_maxroutes)
		{
			log (-1, "WARNING : Nr_maxroutes (%d) exceeded", Nr_maxroutes);

#ifdef TEST_CODE
        	/* since find_worst never returns permanent entries, the */
        	/* limitation on number of routes is circumvented for    */
        	/* permanent routes */

        	if ((bp = find_worst (nr_node->routes)) != NULLNRBIND)
			{
            	nr_routedrop (nodecall, bp->via->call, bp->via->iface);

				if (bp == rbp)	/* Should check for this, you never know */
				{
					return (struct nr_bind*)0;
				}
        	}
#endif    	
		}

		/* 28Aug2011, Maiko, If quality is low, then drop this route */
		if (quality < Nr_autofloor)
		{
			if (Nr_debug)
				log (-1, "unbind node, too low quality");

			/* 28Aug2011, Maiko, Comment here, I may split out the find_XXX
			 * calls within nr_routedrop() in nr3.c. I just need the *last*
			 * part of the function. Using the full nr_routedrop() again uses
			 * up unnecessary CPU since I already have the node, binding, and
			 * neighbour memory structures (see above find_XXX calls). Done!
			 *
			if (nr_routedrop (nodecall, nrbp->via->call, nrbp->via->iface))
				log (-1, "rif_recv - nr_routedrop failed");
			 */
		
			/*
			 * 28Aug2011, Maiko, new function (more efficient this way)
			 *
			 * 12Sep2011, Maiko, Just an observation regarding the excessive
			 * number of ##TEMP entries in the node list. See the change made
			 * when adding a node (earlier in this code). I wonder if perhaps
			 * the node entry should be kept as well when deleting a route. In
			 * other words only remove the binding here, not the node itself,
			 * which effectively deletes the alias too. Nr_routedrop_postread
			 * takes out the node (and therefore alias) if no more bindings,
			 * so I may have to function split again, let's see how it goes.
			 *
			 * 13Sep2011, Maiko (VE4KLM), let just drop the binding, and leave
			 * the node entry as a *routeless* node, less work for JNOS/CPU in
			 * end, and having the ability to track *routeless* nodes may not
			 * be a bad idea at all, I can always filter them out on display,
			 * this will probably be the best I can do to *limit* the amount
			 * of '##TEMP' entries in the nodes list. We're done with this.
			 *
			nr_routedrop_postread (nodecall, nrbp->via->call,
				nr_node, nr_neigh, nrbp);
			 */

			/* 13Sep2011, Maiko (VE4KLM), new function */
			nr_unbind (nr_node, nr_neigh, nrbp);

			continue;
		}

		if (Nr_debug)
		{
			log (-1, "update (old/new) tt %d/%d hops %d/%d qual %d/%d",
				nrbp->tt, transportime, nrbp->hops, hops,
					nrbp->quality, quality);
		}

		nrbp->tt = transportime;
		nrbp->hops = hops;
		nrbp->quality = quality;
	}

	return 1;
}

/* 18/19 August 2011 */

int inp_l3rtt (char *dest)
{
	/* 21Aug2011, Maiko, If INP timer is off, don't process incoming L3RTT */
	return (INP_active && addreq (L3RTTcall, dest));
}

/*
 * 18/19Aug2011, Maiko, revamped from the original 2005 version,
 *
 * 21Aug2011, Maiko, payload size tune up, next step - use RTT value.
 *
 * 22Aug2011, Maiko, Let's do this netrom table by table, so first let's
 * get all the changes made that utilized the nrnbr_tab structure. The 2nd
 * parameter is no longer 'remote', it is now 'struct ax25_cb', since I want
 * both remote and iface now, might as well just stick to one argument.
 *
 */

int inp_l3rtt_recv (char *src, struct ax25_cb *iaxp, struct mbuf *bp)
{
	struct nrnbr_tab *nr_neigh = NULL;	/* 22Aug2011, Maiko */
	struct nrroute_tab *nr_node = NULL;	/* 26Aug2011, Maiko */
	struct nr_bind *nrbp;				/* 26Aug2011, Maiko */

	char tmp[AXBUF], nr_data[100], *dptr;
	int del, cnt, quality, chain;		/* 26Aug2011, Maiko */
	struct timeval tv, tvret;
	struct timezone tz;
	long rtt;

	if (len_p (bp) > 98)
		log (-1, "warning: inp_l3rtt_recv nr_data overflow !!!");

	pullup (&bp, nr_data, 100); /* read all of it, 80 is just a max */

	dptr = nr_data + 11;	/* skip dummy and 0x05 identifier */
				/* 01Sep2011, Change from 5 to 11 */

	while (*dptr == 0x20) dptr++;
	tvret.tv_sec = strtoul (dptr, (char**)(&dptr), 0);
	while (*dptr == 0x20) dptr++;
	strtoul (dptr, (char**)(&dptr), 0);
	while (*dptr == 0x20) dptr++;
	strtoul (dptr, (char**)(&dptr), 0);
	while (*dptr == 0x20) dptr++;
	tvret.tv_usec = strtoul (dptr, (char**)(&dptr), 0);

	gettimeofday (&tv, &tz);
/*
	log (-1, "incoming %ld %ld current %ld %ld",
		tvret.tv_sec, tvret.tv_usec, tv.tv_sec, tv.tv_usec);
*/
	rtt = (long)(((tv.tv_sec - tvret.tv_sec) * 1000
			+ (tv.tv_usec + 10000) / 1000) / 20);

	if (!rtt) rtt = 1;

	if (Nr_debug)
	{
		log (-1, "l3rtt response from [%s] rtt [%ld]",
			pax25 (tmp, iaxp->remote), rtt);
	}

	/* 26Aug2011, Maiko get node for this callsign */
	if ((nr_node = find_nrroute (iaxp->remote)) == NULLNRRTAB)
	{
		if (Nr_debug)
		{
			log (-1, "l3rtt recv, remote [%s] iface [%s] - no route entry",
				pax25 (tmp, iaxp->remote), iaxp->iface->name);
		}
		return 0;
	}

	/* 22Aug2011, Maiko, Update the netrom tables, lookup neighbour first */

	if ((nr_neigh = find_nrnbr (iaxp->remote, iaxp->iface)) == NULLNTAB)
	{
		if (Nr_debug)
		{
			log (-1, "l3rtt recv, remote [%s] iface [%s] - no neighbour entry",
				pax25 (tmp, iaxp->remote), iaxp->iface->name);
		}
		return 0;
	}

    /* 26Aug2011, initialize all hops of routes via this neighbour to zero,
	 * 06Sep2011, Maiko, I think I got it right, 1 hop, leave tt blank so that
	 * that the RTT measured for the neighbour is used instead ...
	 */
	for (nrbp = nr_node->routes; nrbp != NULLNRBIND; nrbp = nrbp->next)
	{
		if (nrbp->via == nr_neigh)
		{
			nrbp->flags &= ~NRB_RECORDED;	/* no longer a recorded route */
			nrbp->obsocnt = Obso_init;	/* keep from becoming obsolete */

			nrbp->hops = 1;	/* technically it is one hop, not zero */
			nrbp->tt = 0;	/* use RTT measured earlier instead */
		}
	}

	/* is this a new interlink ? give it a higher RTT */

	if (nr_neigh->inp_state == NR_INP_STATE_0)
	{
		nr_neigh->inp_state = NR_INP_STATE_RTT;

		nr_neigh->rtt = rtt + 10;

		log (-1, "neighbour [%s] interlink (l3rtt)",
			pax25 (tmp, nr_neigh->call));

		/* inp3_rif_tx (nr_neigh, 1); */
	}

	/* Smooth RTT value */

	rtt = nr_neigh->rtt = (nr_neigh->rtt + rtt) / 2;

	if (Nr_debug)
	{
		log (-1, "neighbour [%s] smoothed rtt [%d]",
			pax25 (tmp, nr_neigh->call), rtt);
	}

	if (rtt >= TT_HORIZON)
	{
		if (Nr_debug)
			log (-1, "is now over (l3rtt) horizon");

		/* inp3_route_neg (nr_neigh);
			nr_neigh_put (nr_neigh);
			dev_put (dev);
		 */
			return 0;
	}

	/* 22Aug2011, Maiko, end (ongoing) USE l3rtt.c code from years ago */

	/* 26Aug2011, Maiko, Set all routes of this neighbour with new rtt */

	for (del = 0, cnt = 0, chain = 0; chain < NRNUMCHAINS; chain++)
	{
		nr_node = Nrroute_tab[chain];

		while (nr_node != NULL)
		{
			/* 02Nov2005, Maiko, NOS uses link lists, while Linux INP3
			 * code uses arrays of 3 - difficult to port at times
			 */
			for (nrbp = nr_node->routes; nrbp != NULLNRBIND; nrbp = nrbp->next)
			{
				if (nrbp->via == nr_neigh)
				{
					quality = rtt2qual (nr_neigh->rtt + nrbp->tt, nrbp->hops);

					/*
					 * 12Sep2011, Maiko, unbind low quality routes, I suspect
					 * we won't see many of these, but we should probably have
					 * the code in place to deal with it anyways.
					 */
					if (quality < Nr_autofloor)
					{
						if (Nr_debug)
						{
							log (-1, "unbind node [%s], too low qualiity",
								pax25 (tmp, nr_node->call));
						}
	/*
	 * 13Sep2011, Maiko, CRASH - should not use nr_routedrop_postread
	 * function inside of this loop, need a variant that considers
	 * the way this FOR loop is using pointers. Not a big deal.
	 *
						nr_routedrop_postread (nr_node->call, nr_neigh->call,
							nr_node, nr_neigh, nrbp);
	 */
						/* 13Sep2011, Maiko (VE4KLM), new function */
						nr_unbind (nr_node, nr_neigh, nrbp);

						del++;	/* track number of routes deleted */

						break;	/* make sure we break out of loop !!! */
					}
					else
					{
						nrbp->quality = quality;

						cnt++;	/* just track the number of updates */
					}
				}
			}

			nr_node = nr_node->next;
		}
	}

	/*
	 * 26Aug2011, Maiko, When I list a particular node after getting a
	 * response to an l3rtt request, I see this code is indeed updating
	 * the quality value of that node if the rtt of the neighbour has
	 * changed. Very good, I'm happy with that.
	 * 12Sep2011, Maiko, Now deleting anything under quality floor.
	 */
	if (Nr_debug)
		log (-1, "%d quality updates, %d unbound", cnt, del);

	return 1;
}

/*
 * 19/20 August 2011 - Maiko, works, BUT crashing after the response comes
 * back. After a bit of observation, I have to conclude that it's actually
 * some thing in the trace code (likely nr_dump) that is doing this. I've
 * had the trace shut off for several hours and it's perfectly fine now.
 *
 * 21Aug2001, Maiko, Think I'm done with this for now, payload size tuned
 * up nicely, and it seems to be working quite well now.
 */

int inp_l3rtt_tx (struct nrnbr_tab *nr_neigh, char *ifcname)
{
	struct timezone tz;
	struct timeval tv;
	struct mbuf *hbp;
//	struct nrroute_tab *rp;
	struct nr3hdr n3hdr;
//	struct mbuf *n3b;
	char tmp[AXBUF];
	char *rtt_data;

	if (Nr_debug)
	{
		log (-1, "l3rtt request to [%s] on port [%s]",
			pax25 (tmp, nr_neigh->call), ifcname);
	}

	/* get the netrom level 3 data formatted first */

	if ((hbp = alloc_mbuf (93)) == NULLBUF)
	{
		log (-1, "inp_l3rtt_tx - no memory");
		return 0;
	}

	hbp->cnt = 93;	/* 21Aug2011, Maiko (VE4KLM), fine tune the size
			 * 01Sep2011, Maiko, changed to 93, since the content
			 * of the L3RTT payload is wrong format, see below
			 */
	rtt_data = hbp->data;

	*rtt_data++ = 0x00;
	*rtt_data++ = 0x00;
	*rtt_data++ = 0x00;
	*rtt_data++ = 0x00;

	*rtt_data++ = NR_INFO;

	/* do_gettimeofday (&tv); */
	gettimeofday (&tv, &tz);

	/*
	 * 21Aug2011, Maiko (VE4KLM), The INP specification says the text portion
	 * of L3RTT frames are implementation specific (which makes sense now that
	 * I see the trace data), the remote system simply has to reflect it back
	 * to the sender unchanged, so lets keep bandwith usage to a minimum, and
	 * just send what we need. The JNOS version is not required, but it could
	 * come in useful from a troubleshooting point of view.
	 *
	 * 22Aug2011, Maiko (VE4KLM), Now that nrnbr is updated, we can now use
	 * the values from there and not the hardcoded values of 60 I used.
	 *
	 * 01Sep2011, Maiko (VE4KLM), Implementation specific my ass ! It seems
	 * that Xnet and Xrouter both use an exact format here, meaning the spec
	 * is absolutely not implementation specific (I interpret that as meaning
	 * specific to the platform using it, ie: Xnet, Xrouter, JNOS, BPQ, etc),
	 * this makes me cranky ! Explains why my ALIAS on the Xrouter side is
	 * actually showing '0i' and not 'MBGATE'. Xnet shows JNOS instead :(
	 *
 	 * Anyways, we will stick with the format from the PE1RXQ kernel patch !
	`*
	 */

#ifdef	DONT_COMPILE
	rtt_data += sprintf (rtt_data, "%10d %10d %10d %10d JNOS 2.0i $M%d $N",
		(int)tv.tv_sec, /* 60 */ nr_neigh->rtt, /* 60 */ nr_neigh->rtt,
			(int)tv.tv_usec, TT_HORIZON);
#endif

	rtt_data += sprintf (rtt_data, "L3RTT: %10d %10d %10d %10d ",
		(int)tv.tv_sec, nr_neigh->rtt, nr_neigh->rtt, (int)tv.tv_usec);

	rtt_data += sprintf (rtt_data, "%-6s %11s %s $M%d $N",
		Nralias, "LEVEL3_V2.1", "JNOS20i", TT_HORIZON);

    *rtt_data = 0x0d;

	/* now setup the netrom level 3 header */

	memcpy (n3hdr.dest, L3RTTcall, AXALEN);

	n3hdr.ttl = 3;	/* nr routing decrements it, it will go out as a 2 */

	/*
	 * 07Sep2011, Maiko, Oops - forgot about this, we need to send
	 * our l3rtt DIRECT to our immediate neighbours, no routing, the
	 * same as what I did in nr3.c for the l3rtt reflection.
	 *
	 * 04Sep2011, Maiko, New function to send L3 frame
	 * directly to a callsign, not via the best route,
	 * which could wind up being another neighbour.
	 */

	/* 07Sep2011, Maiko, Do NOT forget this (nr_direct does not do it) */
        memcpy (n3hdr.source, Nr_iface->hwaddr, AXALEN);

	nr_direct (hbp, nr_neigh->call, nr_neigh->iface, &n3hdr);

#ifdef	DONT_COMPILE
	if ((rp = find_nrroute (nr_neigh->call)) == NULLNRRTAB)
	{
		if (Nr_debug)
		{
			log (-1, "no route to [%s] on port [%s]",
				pax25 (tmp, nr_neigh->call), ifcname);
		}
		return 0;
	}

	/* we are originating this, so iaxp is set to NULLAX25 */
   if (!nr_finally_route (hbp, rp, NULLAX25, &n3hdr))
		free_p (n3b);
#endif

	return 1;
}

/* 20Aug2011, Maiko */

void doINPtick()
{
	struct nrnbr_tab *nr_neigh;

	int chain;

	for (chain = 0; chain < NRNUMCHAINS; chain++)
	{
		nr_neigh = Nrnbr_tab[chain];

		while (nr_neigh != NULL)
		{
		/*
		 * 20Feb2012, Maiko, Temporary kludge, my BPQ neighbour
		 * thinks I am trying to connect to him, which is BAD, I
		 * should be sending these ONLY to INP3 capable systems,
		 * this is seriously affecting my forwarding with Werner's
		 * F6FBB system, the strcmp is a kludge, working on it !
		 *
		 * 04Nov2013, Maiko, Kludge replaced with an actual list
		 * that can be configured by the SYSOP - long overdue !
		 *
			if (strcmp ("vhf", nr_neigh->iface->name))
				inp_l3rtt_tx (nr_neigh, nr_neigh->iface->name);
		 */
			if (isINPiface (nr_neigh->iface->name))
				inp_l3rtt_tx (nr_neigh, nr_neigh->iface->name);

			nr_neigh = nr_neigh->next;
		}
	}
  
    /* Restart timer */
    start_timer (&INPtimer) ;
}

/* 20Aug2011, Maiko */

/* Set l3rtt interval (same structure function as donodetimer) */
int doINPtimer (int argc, char **argv, void *p)
{
	log (-1, "INP scheduler is active");

	INP_active = 1;	/* 21Aug2011, Maiko, flag to tell nr3.c to accept L3RTT */ 

    stop_timer (&INPtimer) ;	/* in case it's already running */

    INPtimer.func = (void (*)(void*))doINPtick;	/* what to call on timeout */

    INPtimer.arg = NULLCHAR;	/* dummy value */

    set_timer (&INPtimer, L3RTT_INTERVAL);	/* atoi(argv[1])*1000); */

    start_timer (&INPtimer);

    return 0;
}

#endif

