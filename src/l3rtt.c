/*
 * Support routines for L3RTT packets.
 *
 * Alot of this code is written from scratch, and alot of it comes
 * from the INP3 Linux Kernel patch from PE1RXQ, with necessary mods
 * to make that code 'fit' into the NOS code structure. Some of the
 * code I wrote from scratch is based on ideas from the PE1RXQ code,
 * and from actual observations I made using logfiles and traces.
 *
 * Thanks to PE1RXQ and other contributors for doing INP3 ...
 *
 * Mid September, 2005, by Maiko Langelaar / VE4KLM
 *
 */

#include <ctype.h>
#include "global.h"

#if defined (NETROM) && defined (INP3)

#include "inp3.h"

#include <sys/time.h>

/* 04Nov2005, Maiko, should really prototype this */
extern struct nr_bind *find_best (struct nr_bind*, unsigned);

/* 16Sep2005, Maiko (VE4KLM), Let's start playing with INP3 support */

static char L3RTTcall[AXALEN] = {
    'L' << 1, '3' << 1, 'R' << 1, 'T' << 1, 'T' << 1, ' ' << 1, '0' << 1
};

extern int Nr_debug;	/* 05Oct2005, Maiko, New debugging flag */

/* Check the NETROM destination call to see if this is a L3RTT frame */

int inp3_is_this_l3rtt (char *dest)
{
	return (addreq (L3RTTcall, dest));
}

/*
 * 19Sep2005, Maiko. We sent L3RTT to someone, now deal with the reply.
 * This particular function is more or less taken from the INP3 Linux
 * Kernel patch from PE1RXQ, but modified by myself to 'fit' the NOS
 * code structure. The process was actually not very complicated !
 * 22Sep2005, Maiko, inp3_nodes_neg () and inp3_route_neg () funcs
 * more or less converted to NOS code structure.
 */

static int l3rtt_proc_reply (struct mbuf *bp, struct ax25_cb *ax25)
{
	int chain, qual, i, len, rtt, neg_nodes = 0;

	struct nrroute_tab *nr_node = NULL;
	struct nrnbr_tab *nr_neigh = NULL;
	struct hlist_node *node, *node2;
	struct nrroute_tab **neg_node;
	struct timeval tv, tvret;
	struct nr3hdr n3hdr;
	struct nr_bind *nrbp;
	struct timezone tz;
	struct net_device *dev;
	struct sk_buff *skbret;
	char tmp[AXBUF];

	unsigned char *dptr;

	char nr_data[200];

	len = (int)len_p (bp);

	if (pullup (&bp, nr_data, len) < len)
	{
		free_p (bp);
		return 0;
	}
		
	nr_data[len] = 0;	/* add terminating null */

	dptr = nr_data + 12;

	while (*dptr == 0x20) dptr++;
	tvret.tv_sec = strtoul (dptr, (char**)(&dptr), 0);
	while (*dptr == 0x20) dptr++;
	strtoul (dptr, (char**)(&dptr), 0);
	while (*dptr == 0x20) dptr++;
	strtoul (dptr, (char**)(&dptr), 0);
	while (*dptr == 0x20) dptr++;
	tvret.tv_usec = strtoul (dptr, (char**)(&dptr), 0);

	gettimeofday (&tv, &tz);

	rtt = ((tv.tv_sec - tvret.tv_sec) * 1000 +
			(tv.tv_usec + 10000) / 1000) / 20;

	if (!rtt)
		rtt = 1;

	if (Nr_debug)
		log (-1, "l3rtt len [%d] rtt [%d]", len, rtt);

	/* get node for this callsign */
	if ((nr_node = find_nrroute (ax25->remote)) == NULLNRRTAB)
	{
		log (-1, "l3rtt_proc_reply - no route");
		return 0;
	}

	/* get neighbor for the node we just found */
	if ((nr_neigh = find_nrnbr (ax25->remote, ax25->iface)) == NULLNTAB)
	{
		log (-1, "l3rtt_proc_reply - no neighbour");
		return 0;
	}

	/* 02Nov2005, Maiko, NOS uses link lists, Linux INP3 uses Array of 3 */
	for (nrbp = nr_node->routes; nrbp != NULLNRBIND; nrbp = nrbp->next)
	{
		if (nrbp->via == nr_neigh)
			nrbp->hops = 0;
	}

	/* new link ? give it a higher rtt */
	if (nr_neigh->inp_state == NR_INP_STATE_0)
	{
		nr_neigh->rtt = rtt + 10;
		nr_neigh->inp_state = NR_INP_STATE_RTT;

		log (-1, "new neighbour - transmit RIFs for all our routes");

		inp3_rif_tx (nr_neigh, 1);
	}

	/* smooth rtt */
	rtt = nr_neigh->rtt = (nr_neigh->rtt + rtt) / 2;

	if (rtt >= TT_HORIZON)
	{
		log (-1, "rtt higher than horizon");

		inp3_route_neg (nr_neigh);

		// nr_neigh_put (nr_neigh);
		// dev_put (dev);
		return 0;
	}
		
	/* set all routes of this neighbour with new rtt */
	neg_node = (struct nrroute_tab**)callocw (1,
		sizeof(struct nrroute_tab) * MAX_RIPNEG);

	if (!neg_node)
		return 0;

	lock_nrroute ();

	for (chain = 0; chain < NRNUMCHAINS; chain++)
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
					qual = rtt2qual (nr_neigh->rtt + nrbp->tt, nrbp->hops);

					nrbp->quality = qual;

					/* nr_sort_node(nr_node); */
				}
			}

			nrbp = find_best (nr_node->routes, 0);

			if (nrbp != NULLNRBIND &&
				infotype (nr_node->ltt, nrbp->tt + (nrbp->via)->rtt) == -1)
			{
				if (neg_nodes >= MAX_RIPNEG)
				{
					inp3_nodes_neg (neg_node, neg_nodes, nr_neigh);
					neg_nodes = 0;
				}

				neg_node[neg_nodes] = nr_node;

				neg_nodes++;
			}

			nr_node = nr_node->next;
		}
	}

	unlock_nrroute ();

	if (neg_nodes)
		inp3_nodes_neg(neg_node, neg_nodes, nr_neigh);

	free (neg_node);

	return 0;
}

/* Process an L3RTT frame (called from within nr_route function) */

int inp3_proc_l3rtt (char *src, struct mbuf *bp, struct ax25_cb *iaxp)
{
   	char tmp[AXBUF];

	/* 19Sep2005, Maiko, Is this a reply to an L3RTT we sent earlier */

	if (addreq (src, Nr_iface->hwaddr))
	{
		if (Nr_debug)
			log (-1, "[%s] L3RTT response", pax25 (tmp, src));

		l3rtt_proc_reply (bp, iaxp);	/* nothing to free !!! */

        return 1;
	}
	else
	{
		if (Nr_debug)
			log (-1, "[%s] L3RTT request", pax25 (tmp, src));
	}

	return 0;
}

#endif

