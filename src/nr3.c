/*
 * JNOS 2.0
 *
 * $Id: nr3.c,v 1.8 2012/03/20 16:30:03 ve4klm Exp ve4klm $
 *
 * net/rom level 3 low level processing
 * Copyright 1989 by Daniel M. Frank, W9NK.  Permission granted for
 * non-commercial distribution only.
 *
 * Mods by PA0GRI and WG7J and VE4KLM
 *
 * Sep2005, Maiko (VE4KLM) - INP3 support (was shelved for a long time)
 *
 * 01Sep2011, Maiko (VE4KLM) - INP3 now replaced with INP2011, revamped, as
 * of today, the new implementation is 'read only', see new newinp.c module.
 */
#include <ctype.h>
#include "global.h"

#ifdef NETROM

#include "mbuf.h"
#include "pktdrvr.h"
#include "iface.h"
#include "netuser.h"
#include "timer.h"
#include "arp.h"
#include "slip.h"
#include "ax25.h"
#include "netrom.h"
#include "nr4.h"
#include "lapb.h"
#include "socket.h"
#include "trace.h"
#include "ip.h"
#include "commands.h"

#ifdef	INP2011
#include "newinp.h"	/* 01Sep2011, Maiko, Clean up, reorganize a bit */
#endif

#include "jlocks.h"	/* 06Feb2006, Maiko, new locking code */

#ifdef NRR		/* 03Mar2006, Maiko, NRR separate from INP3 now */
#include "nrr.h"
#endif

#ifdef __GNUC__
struct targ;   /* forward definition to keep GCC happy */
#endif
  
/* IF the following is defined,
 * when we receive a nodes broadcast of a new neighbour,
 * we will immediately respond with a nodes broadcast on that interface
 * This speeds up route discovery at bootup etc..
 * 920422 - WG7J
 */
#define NR_BC_RESPOND 1
  
static int accept_bc __ARGS((char *addr,struct iface *ifp));
struct nr_bind *find_best __ARGS((struct nr_bind *list,unsigned obso));
struct nr_bind *find_bind __ARGS((struct nr_bind *list,struct nrnbr_tab *np));
static struct nr_bind *find_binding __ARGS((struct nr_bind *list,struct nrnbr_tab *neighbor));
struct nrnbr_tab *find_nrnbr __ARGS((char *, struct iface *));
void nrresetlinks __ARGS((struct nrroute_tab *rp)); /* s/b in a header file */
static struct nrnf_tab *find_nrnf __ARGS((char *, struct iface *));
static struct nr_bind *find_worst __ARGS((struct nr_bind *list));
void nr_poll_resp __ARGS((struct targ *targ));
void nr_poll_respond __ARGS((struct iface *iface));
  
struct nrnbr_tab *Nrnbr_tab[NRNUMCHAINS];
struct nrroute_tab *Nrroute_tab[NRNUMCHAINS];
struct nrnf_tab *Nrnf_tab[NRNUMCHAINS];

unsigned Nr_nfmode = NRNF_NOFILTER;
unsigned short Nr_ttl = 10;

/*
 * 05Sep2011, Maiko, No longer static, needed in newinp.c, and as of
 * 02Sep2020 it can now be user defined, using new 'netrom obsoinit'
 * command, do not change this in the source anymore, do it from a
 * command line intead. Brian (N1URO) notes NEDA default is 5
 */
unsigned Obso_init = 6;

/*
 * 02Sep2020, Maiko (VE4KLM), no longer static, can now be user defined,
 * using new 'netrom obsominbc' command, do not change this in the source
 * anymore, do it from a command line instead. NEDA default is 3
 */
unsigned Obso_minbc = 5;

unsigned Nr_maxroutes = 10;	/* 24Aug2011, Maiko, not static anymore
				 * 30Oct2020, Maiko, too many exceeded warnings,
				 * so bumped it up to 10, but still not a fix */

unsigned Nr_autofloor = 10;
int Nr_derate = 1;          /* Allow automatic derating of routes */
int Nr_promisc = 0;         /* behave promisuously with nr bcasts or not? */

struct iface *Nr_iface = NULLIF;	/* 14Sep2005, Maiko, Safe guard !!! */

int Nr_debug = 0;	/* 04Oct2005, Maiko, New NETROM and INP3 debug flag */

extern char Nralias[ALEN+1];

/*
 * Validate the alias field is good quality ascii to prevent network corruption
 * 25Aug2011, Maiko, no longer static, I need this in the INP routines.
 */
  
int nr_aliasck (char *alias)
{
    int x = ALEN;
    int c;
    while (x--) {
        c = *alias++;
        if (!isprint( (int) c) )
            return 1;
    }
    return 0;
}

/* send a NET/ROM layer 3 datagram */
void
nr3output(dest, data)
char *dest;
struct mbuf *data;
{
    struct nr3hdr n3hdr;
    struct mbuf *n3b;
  
    memcpy(n3hdr.dest,dest,AXALEN); /* copy destination field */
    n3hdr.ttl = Nr_ttl; /* time to live from initializer parm */
  
    if((n3b = htonnr3(&n3hdr)) == NULLBUF){
        free_p(data);
        return;
    }
    append(&n3b, data);
    /* The null interface indicates that the packet needs to have */
    /* an appropriate source address inserted by nr_route */
    nr_route(n3b,NULLAX25);
}
  
/* send IP datagrams across a net/rom network connection */
int
nr_send(bp,iface,gateway,prec,del,tput,rel)
struct mbuf *bp;
struct iface *iface;
int32 gateway;
int prec;
int del;
int tput;
int rel;
{
    struct arp_tab *arp;
  
    dump(iface,IF_TRACE_OUT,CL_NETROM,bp);
  
    if((arp = arp_lookup(ARP_NETROM,gateway,iface)) == NULLARP){
        free_p(bp); /* drop the packet if no route */
        return -1;
    }
  
    /*
       these are already taken care of when the packet reaches
       nr_route() to be routed - WG7J
  
    iface->rawsndcnt++;
    iface->lastsent = Nr_iface->lastsent = secclock();
    */
  
    Nr_iface->ipsndcnt++;
    nr_sendraw(arp->hw_addr, NRPROTO_IP, NRPROTO_IP, bp);
    return 0;
}
  
/* Send arbitrary protocol data on top of a NET/ROM connection */
void
nr_sendraw(dest,family,proto,data)
char *dest;
unsigned family;
unsigned proto;
struct mbuf *data;
{
    struct mbuf *pbp;
    struct nr4hdr n4hdr;
  
    /* Create a "network extension" transport header */
    n4hdr.opcode = NR4OPPID;
    n4hdr.u.pid.family = family;
    n4hdr.u.pid.proto = proto;
  
    if((pbp = htonnr4(&n4hdr)) == NULLBUF){
        free_p(data);
        return;
    }
    append(&pbp,data);      /* Append the data to that */
    nr3output(dest, pbp); /* and pass off to level 3 code */
}

/*
 * 04Sep2011, Maiko, Splitting out more code, I need a way to send
 * a connected netrom frame DIRECT to a call (ie, my neighbour), not
 * via the best route like is done by nr_finally_route (), so I need
 * to split out most of that function into a new nr_direct () call.  
 */
int nr_direct (struct mbuf *bp, char *call,
	struct iface *iface, struct nr3hdr *n3hdr)
{
	struct ax25_cb *axp;
	struct mbuf *pbp;

    /* Make sure there is a connection to the neighbor */
    /* Make sure we use the netrom-interface call for this connection!
     * 11/20/91 WG7J/PA3DIS
     */
    if ((axp = find_ax25 (Nr_iface->hwaddr, call, iface)) == NULLAX25 ||
		(axp->state != LAPB_CONNECTED && axp->state != LAPB_RECOVERY))
	{
        /* Open a new connection or reinitialize old one */
        /* hwaddr has been advanced to point to neighbor + digis */
        axp = open_ax25 (iface, Nr_iface->hwaddr, call, AX_ACTIVE,
        		0, s_arcall, s_atcall, s_ascall, -1);

        if (axp == NULLAX25)
			return 0;
    }
 
	n3hdr->ttl--;
 
    if (n3hdr->ttl == 0)	/* the packet's time to live is over! */
		return 0;

    /* now format network header */
    if ((pbp = htonnr3 (n3hdr)) == NULLBUF)
		return 0;

    append (&pbp, bp);        /* append data to header */
  
    /* put AX.25 PID on front */
    bp = pushdown (pbp, 1);
    bp->data[0] = PID_NETROM;
  
    if ((pbp = segmenter(bp,axp->paclen)) == NULLBUF)
		return 0;

    send_ax25 (axp, pbp, -1);  /* pass it off to ax25 code */

	return 1;
}

/*
 * 11Oct2005, Maiko, Put in to simplify the code, since adding INP3 stuff
 * requires me to call this 'finally route the packet' from several places
 * in the original nr_route (). Also, make darn sure the program calling
 * this function does a free_p (bp) if this function returns 0 value !
 *
 * 20Aug2011, Maiko, I need this function in 'newinp.c', not static anymore
 */
int nr_finally_route (struct mbuf *bp, struct nrroute_tab *rp, struct ax25_cb *iaxp, struct nr3hdr *n3hdr)
{
    struct nr_bind *bindp;
    struct nrnbr_tab *np;
	struct iface *iface; 

    if (rp == NULLNRRTAB)
		return 0;

    if ((bindp = find_best (rp->routes, 1)) == NULLNRBIND)
		return 0;
  
    np = bindp->via;

    iface = np->iface;
  
    /* Now check to see if iaxp is null.  That is */
    /* a signal that the packet originates here, */
    /* so we need to insert the callsign of the appropriate  */
    /* interface */
    if (iaxp == NULLAX25)
        memcpy (n3hdr->source, Nr_iface->hwaddr, AXALEN);

	/* 04Sep2011, Maiko, More splitting out code to separate function */
	if (nr_direct (bp, np->call, iface, n3hdr))
	{
	    /*Update the last-used timestamp - WG7J */
    	np->lastsent = secclock();
  
    	/* Add one to the route usage count */
    	bindp->usage++;
  
    	/* Add one to the number of tries for this neighbour */
    	np->tries++;

		return 1;
	}

	return 0;
}

/* Route net/rom network layer packets.
 */
void
nr_route(bp, iaxp)
struct mbuf *bp;            /* network packet */
struct ax25_cb *iaxp;           /* incoming ax25 control block */
{
    struct nr3hdr n3hdr;
    struct nr4hdr n4hdr;
    //struct ax25_cb *axp;
    //struct mbuf *pbp;
    //struct nrnbr_tab *np;
    struct nrroute_tab *rp;
    //struct nr_bind *bindp;
    //struct iface *iface;

#ifdef SNMPD
	int32 len;
#endif

#ifdef	INP2011
	/*
	 * 21Aug2011, Maiko (VE4KLM), New code for INP RIF (RIP) processing
	 * 11Nov2013, Maiko (VE4KLM), Can't believe I forgot to check for the
	 * null ax25 callback structure, but nobody ever gave me a detailed
	 * report on this until now (N1URO for one of his neighbour nodes).
	 * NOTE : not sure this is the way to really handle this yet, but
	 * at least it will avoid the crash scenario !
	 */
    if ((iaxp != NULLAX25) && ((unsigned char)(*(bp->data)) == 0xff))
	{
		inp_rif_recv (bp, iaxp);
		return;
	}	
#endif

#ifdef SNMPD
	/* 27Sep2011, Maiko, Track netrom interface (excludes RIF frames) */
	len = len_p (bp);
#endif

    if (ntohnr3(&n3hdr,&bp) == -1)
	{
        free_p(bp);
        return;
    }

    /* If this isn't an internally generated network packet,
     * give the router a chance to record a route back to the
     * sender, in case they aren't in the local node's routing
     * table yet.
     */
    /* Add some statictics gathering - WG7J */
    if (iaxp != NULLAX25)
	{
        /* incoming packet, find the interface number */
        if (!(iaxp->iface->flags & IS_NR_IFACE))
		{
            free_p(bp);	/* Not a net/rom interface! */
            return;
        }
 
        /* Add (possibly) a zero-quality recorded route via */
        /* the neighbor from which this packet was received */
        /* Note that this doesn't work with digipeated neighbors. */

	/*
	 * 01Sep2011, Maiko, reflected L3RTT that I originated will be
	 * caught here, I do NOT want my own callsign added, that can
	 * cause possible loops, as well 'N VE4KLM-3' is local !!!
	 */
		if (addreq (Nr_iface->hwaddr, n3hdr.source))
		{
			if (Nr_debug)
				log (-1, "don't add my call to node tables, l3rtt reflected ?");
		}
      	else
			nr_routeadd ("##temp",n3hdr.source,iaxp->iface,0,iaxp->remote,0,1);

        Nr_iface->rawrecvcnt++;
        Nr_iface->lastrecv = secclock();
#ifdef SNMPD
		/* 27Sep2011, Maiko, Might as well track netrom interface */
		Nr_iface->rawrecbytes += len;
#endif
    }
	else
	{
        Nr_iface->rawsndcnt++;
        Nr_iface->lastsent = secclock();
#ifdef SNMPD
		/* 27Sep2011, Maiko, Might as well track netrom interface */
		Nr_iface->rawsndbytes += len;
#endif
    }

	/*
	 * Check if this frame is for me !
	 */
    if (addreq (Nr_iface->hwaddr, n3hdr.dest))
	{
		/*
		 * Toss it out if this is from me (an internal loop) or if
		 * we are not able to read a proper Level 4 Transport Hdr.
		 */
        if ((iaxp == NULLAX25) || (ntohnr4 (&n4hdr, &bp) == -1))
		{
			log (-1, "tossed frame to me from me, or bad level 4 header");
            free_p (bp);
            return;
		}

#ifdef	NRR
		/* 11Oct2005, Maiko, Finished version of NRR code */
		/* 03Mar2006, Maiko, NRR independent of INP3 now */
		if (n4hdr.flags & NR4_NRR)
		{
			if (!nrr_proc (&n3hdr, &bp))
				return;

			jnos_lock (JLOCK_NR);
			rp = find_nrroute (n3hdr.dest);
			jnos_unlock (JLOCK_NR);

			/* Now finally go route it */
			if (!nr_finally_route (bp, rp, iaxp, &n3hdr))
				free_p (bp);

			return;
		}
#endif

#ifdef G8BPQ
        /* Add the G8BPQ stuff to our route info - WG7J */
        if (n4hdr.flags & NR4_G8BPQMASK)
		{
			jnos_lock (JLOCK_NR);
            rp = find_nrroute(n3hdr.source);
			jnos_unlock (JLOCK_NR);

            if (rp != NULLNRRTAB)
			{
                if(n4hdr.flags & NR4_G8BPQTTL)
				{
                    rp->hops = n4hdr.u.conack.ttl - n3hdr.ttl + 1;
                    rp->flags |= G8BPQ_NODETTL;
                }
				else
				{
                    rp->irtt = n4hdr.u.conreq.t4init;
                    rp->flags |= G8BPQ_NODERTT;
                }
			}
		}
#endif
        if ((n4hdr.opcode & NR4OPCODE) == NR4OPPID)
		{
            /* IP does not use a NET/ROM level 3 socket */
            if (n4hdr.u.pid.family == NRPROTO_IP &&
				n4hdr.u.pid.proto == NRPROTO_IP)
			{
                if(Nr_iface->flags & LOG_IPHEARD)
				{
                    struct mbuf *nbp;
                    struct ip ip;
                    int len;
  
                    len = len_p(bp);
                    if(dup_p(&nbp, bp, 0, len) == len)
					{
                        ntohip(&ip,&nbp);
                        if(ip.version == IPVERSION)
                            log_ipheard(ip.source,Nr_iface);
                    }
                    free_p(nbp);
                }

                Nr_iface->iprecvcnt++;
                ip_route(iaxp->iface,bp,0);
            }
			else free_p (bp);	/* we don't do this proto */
        }
		else nr4input (&n4hdr,bp);	/* must be net/rom transport: */

        return;
    }

#ifdef	INP2011

	/* 18/19/20 Aug2011, Maiko, New attempt to do INP stuff */

	if (inp_l3rtt (n3hdr.dest))
	{
		/*
		 * 20Aug2011, Maiko, If the L3 source address is ours, then process
		 * it as a reply to an earlier L3RTT frame we sent our neighbor, other
		 * wise send this frame back to where it came from (our neighbor).
		 * 22Aug2011, Maiko, l3rtt_recv 2nd parameter is now iaxp, not remote
		 */
		if (addreq (n3hdr.source, Nr_iface->hwaddr))
			inp_l3rtt_recv (n3hdr.source, iaxp, bp);

		else {

			char tmp[AXBUF];

			/* 20Aug2011, Maiko, L3RTT both ways working nicely now */

			/* 02Sep2011, Maiko, routing loops are happening because
			 * I think if a neighbour node is not in our node tables
			 * (i manuall added one for the fun of it), the respond to
			 * keeps going and going cause find_nrroute is actually
			 * routing it via some other neighbour - ie when this
			 * happens we have no DIRECT route back for L3RTT, which
			 * I believe this mechanism really really depends on.
			 *
			 * Anyways fix this later, I know why it happens now.
			 *
			 * Actually, it's because someone else may have a better
			 * route then the direct one, I can't use find_nrroute
			 * then, I need to be more direct about this then.
			 * 03Sep2011, Maiko, Since bob is not sending NODES, but
			 * rather to my Nr_call, my obscolencse drops and kills
			 * this routing method. I need to fix this badly !
			 */
			if (Nr_debug)
			{
				log (-1, "respond to l3rtt request from [%s]",
					pax25 (tmp, n3hdr.source));
			}
			/*
			 * 04Sep2011, Maiko, New function to send L3 frame
			 * directly to a callsign, not via the best route,
			 * which could wind up being another neighbour.
			 */
			nr_direct (bp, n3hdr.source, iaxp->iface, &n3hdr);
/*
 * 04Sep2011, Maiko, We MUST go direct, no best route for this
 *
    		rp = find_nrroute (n3hdr.source);

			if (Nr_debug)
				log (-1, "rp->call [%s]", pax25 (tmp, rp->call));

			if (!nr_finally_route (bp, rp, iaxp, &n3hdr))
				free_p (bp);
 *
 */
		}

		return;
	}
#endif

#ifdef G8BPQ
    {
        struct mbuf *nbp;
  
    /* If this is not a locally generated packet,
     * Add the G8BPQ stuff to our route info - WG7J
     */
        if (iaxp != NULLAX25)
		{
            dup_p(&nbp,bp,0,len_p(bp));

            if(ntohnr4(&n4hdr,&nbp) != -1)
			{
                if(n4hdr.flags & NR4_G8BPQMASK)
				{
					jnos_lock (JLOCK_NR);
                    rp = find_nrroute(n3hdr.source);
					jnos_unlock (JLOCK_NR);

                    if (rp != NULLNRRTAB)
					{
                        if(n4hdr.flags & NR4_G8BPQTTL)
						{
                            rp->hops = n4hdr.u.conack.ttl - n3hdr.ttl + 1;
                            rp->flags |= G8BPQ_NODETTL;
                        }
						else
						{
                            rp->irtt = n4hdr.u.conreq.t4init;
                            rp->flags |= G8BPQ_NODERTT;
                        }
                    }
				}
			}

            free_p(nbp);
        }
    }
#endif

	jnos_lock (JLOCK_NR);
    rp = find_nrroute (n3hdr.dest);
	jnos_unlock (JLOCK_NR);

    /* Now finally go route it */
	if (!nr_finally_route (bp, rp, iaxp, &n3hdr))
		free_p (bp);
}
  
/* Perform a nodes broadcast on interface # ifno in the net/rom
 * interface table.
 */
/* This uses Nralias as the alias instead of Nriface.alias as before - WG7J */
void
nr_bcnodes(ifp)
struct iface *ifp;
{
    struct mbuf *hbp, *dbp, *savehdr;
    struct nrroute_tab *rp;
    struct nrnbr_tab *np;
    struct nr_bind * bp;
    struct nr3dest nrdest;
    int i, didsend = 0, numdest = 0;
  
    /* prepare the header */
    if((hbp = alloc_mbuf(NR3NODEHL)) == NULLBUF)
        return;
  
    hbp->cnt = NR3NODEHL;
  
    *hbp->data = NR3NODESIG;
    memcpy(hbp->data+1,Nralias,ALEN);
  
    /* Some people don't want to advertise any routes; they
     * just want to be a terminal node.  In that case we just
     * want to send our call and alias and be done with it.
     */
  
    if(ifp->nr_autofloor == 0) {
        /*changed to use the Netrom interface call on all broadcasts,
         *INDEPENDENT from the actual interface call!
         *11/20/91 WG7J/PA3DIS
         */
        (*ifp->output)(ifp, Ax25multi[NODESCALL], Nr_iface->hwaddr,
        PID_NETROM, hbp);   /* send it */
        return;
    }
  
    /* make a copy of the header in case we need to send more than */
    /* one packet */
    savehdr = copy_p(hbp,NR3NODEHL);
  
    /* now scan through the routing table, finding the best routes */
    /* and their neighbors.  create destination subpackets and append */
    /* them to the header */
    for(i = 0; i < NRNUMCHAINS; i++){
        for(rp = Nrroute_tab[i]; rp != NULLNRRTAB; rp = rp->next){
            /* look for best, non-obsolescent route */
            if((bp = find_best(rp->routes,0)) == NULLNRBIND)
                continue;   /* no non-obsolescent routes found */
  
            if(bp->quality == 0)    /* this is a loopback route */
                continue;           /* we never broadcast these */
  
            if ((int)(bp->quality) < ifp->nr_autofloor) /* below threshhold route */
                continue ;      /* so don't broadcast it */
  
            if (nr_aliasck(rp->alias))  /* corrupted alias entry? */
                continue ;      /* don't rebroadcast it */
                            /* safety measure! */
#ifdef	INP2011
			/* 22Sep2011, Maiko (VE4KLM), don't broadcast tmp entries */
			if (alias2ignore (rp->alias))
				continue;

			/* 22Sep2011, Maiko (VE4KLM), don't broadcast nocall nodes */
			if (callnocall (rp->call))
				continue;
#endif 
            np = bp->via;
            /* insert best neighbor */
            memcpy(nrdest.neighbor,np->call,AXALEN);
            /* insert destination from route table */
            memcpy(nrdest.dest,rp->call,AXALEN);
            /* insert alias from route table */
            strcpy(nrdest.alias,rp->alias);
            /* insert quality from binding */
            nrdest.quality = bp->quality;
            /* create a network format destination subpacket */
            if((dbp = htonnrdest(&nrdest)) == NULLBUF){
                free_p(hbp);    /* drop the whole idea ... */
                free_p(savehdr);
                return;
            }
            /* we now have a partially filled packet */
            didsend = 0;
            append(&hbp,dbp);   /* append to header and others */
            /* see if we have appended as many destinations
             * as we can fit into a single broadcast.  If we
             * have, go ahead and send them out.
             */
            if(++numdest == NRDESTPERPACK){ /* filled it up */
                /* indicate that we did broadcast */
                didsend = 1;
                /* reset the destination counter */
                numdest = 0;
                (*ifp->output)(ifp, Ax25multi[NODESCALL], Nr_iface->hwaddr,
                PID_NETROM,hbp);   /* send it */
                /* new header */
                hbp = copy_p(savehdr,NR3NODEHL);
            }
        }
    }
  
    /* If we have a partly filled packet left over, or we never */
    /* sent one at all, we broadcast: */
  
    if(!didsend || numdest > 0) {
        (*ifp->output)(ifp, Ax25multi[NODESCALL], Nr_iface->hwaddr,PID_NETROM, hbp);
    } else {
        if(numdest == 0)    /* free the header copies */
            free_p(hbp);
    }
  
    free_p(savehdr);
}
  
/* Perform a nodes broadcast poll on interface ifp
 * in the net/rom interface table. - WG7J
 */
void
nr_bcpoll(ifp)
struct iface *ifp;
{
    struct mbuf *hbp;
  
    /* prepare the header */
    if((hbp = alloc_mbuf(NR3NODEHL)) == NULLBUF)
        return;
    hbp->cnt = NR3NODEHL;
    *hbp->data = NR3POLLSIG;
    memcpy(hbp->data+1,Nralias,ALEN);
  
    /* send it out */
    (*ifp->output)(ifp, Ax25multi[1],Nr_iface->hwaddr,PID_NETROM, hbp);
    return;
}
  
/* Drop all Net/Rom filters, routes and neighbors that point to an interface.
 * Used by if_detach() and nr_stop() - K2MF */
void
if_nrdrop(struct iface *ifp)
{
    int i;
    struct nr_bind *bind, *bindnext;
    struct nrnf_tab *filt, *filtnext;
    struct nrroute_tab *nrp, *nrpnext;

    if(ifp->flags & IS_NR_IFACE) {
        /* Drop Net/Rom filters */
        for(i = 0; i < NRNUMCHAINS; i++) {
            for(filt = Nrnf_tab[i]; filt != NULLNRNFTAB; filt = filtnext) {
                filtnext = filt->next;

                if(filt->iface == ifp)
                    nr_nfdrop(filt->neighbor,ifp);
            }
        }
        /* Drop Net/Rom routes and neighbors */
        for(i = 0; i < NRNUMCHAINS; i++) {
            for(nrp = Nrroute_tab[i]; nrp != NULLNRRTAB; nrp = nrpnext) {
                nrpnext = nrp->next;

                for(bind = nrp->routes; bind != NULLNRBIND; bind = bindnext) {
                    bindnext = bind->next;

                    if(bind->via->iface == ifp)
  		        /*nr_binddrop((int16)i,nrp,bind,bind->via);*/
                        nr_routedrop(nrp->call, bind->via->call, ifp);
                }
            }
        }
        ifp->flags &= ~IS_NR_IFACE;
    }
}

extern struct timer Nodetimer,Obsotimer;
extern void donodetick __ARGS((void)),doobsotick __ARGS((void));
int nr_stop(struct iface *);
  
int nr_stop(struct iface *iftmp) {
  
    /*make sure that the netrom interface is properly detached
     *fixed 11/15/91, Johan. K. Reinalda, WG7J/PA3DIS
     * Improved 11/97 by K2MF and N5KNX.
     */
    for(iftmp=Ifaces;iftmp;iftmp=iftmp->next)
        if_nrdrop(iftmp);  /* if IS_NR_IFACE, remove it from nr filters, routes and neighbors */

    stop_timer(&Nodetimer);
    stop_timer(&Obsotimer);
    Nr_iface = NULLIF;
    return 0;
}
  
/* attach the net/rom interface.  no parms for now. */
int
nr_attach(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(Nr_iface != (struct iface *)0){
        j2tputs("netrom interface already attached\n");
        return -1;
    }
  
    Nr_iface = (struct iface *)callocw(1,sizeof(struct iface));
    Nr_iface->addr = Ip_addr;
  
    /* The j2strdup is needed to keep the detach routine happy (it'll
     * free the allocated memory)
     */
    Nr_iface->name = j2strdup("netrom");
    if(Nr_iface->hwaddr == NULLCHAR){
        Nr_iface->hwaddr = mallocw(AXALEN);
        memcpy(Nr_iface->hwaddr,Mycall,AXALEN);
    }
    Nr_iface->stop = nr_stop;
    Nr_iface->mtu = NR4MAXINFO;
    setencap(Nr_iface,"NETROM");
    Nr_iface->next = Ifaces;
    Ifaces = Nr_iface;
    memcpy(Nr4user,Mycall,AXALEN);
  
    /*Added some default settings for node-broadcast interval and
     *obsolescence timers.
     *11/21/91 WG7J/PA3DIS
     */
    stop_timer(&Nodetimer);
    Nodetimer.func = (void (*)__ARGS((void*)))donodetick;/* what to call on timeout */
    Nodetimer.arg = NULLCHAR;       /* dummy value */
    set_timer(&Nodetimer,1800000);  /* 'standard netrom' 30 minutes*/
    start_timer(&Nodetimer);        /* and fire it up */
  
    stop_timer(&Obsotimer);
    Obsotimer.func = (void (*)__ARGS((void*)))doobsotick;/* what to call on timeout */
    Obsotimer.arg = NULLCHAR;       /* dummy value */
    set_timer(&Obsotimer,2100000);  /* 35 minutes */
    start_timer(&Obsotimer);        /* and fire it up */
  
    return 0;
}
  
/* This function checks an ax.25 address and interface number against
 * the filter table and mode, and returns -1 if the address is to be accepted
 * verbatim, the quality if filtered in or 0 if it is to be filtered out.
 */
static int
accept_bc(addr,ifp)
char *addr;
struct iface *ifp;
{
    struct nrnf_tab *fp;
  
    if(Nr_nfmode == NRNF_NOFILTER)      /* no filtering in effect */
        return -1;
  
    fp = find_nrnf(addr,ifp);     /* look it up */
  
    if (fp != NULLNRNFTAB && Nr_nfmode == NRNF_ACCEPT)
        return fp->quality ;
  
    if (fp == NULLNRNFTAB && Nr_nfmode == NRNF_REJECT)
        return -1;
  
    if (Nr_promisc)
        return -1;      /* Come up and see me sometime..... */
    else
        return 0 ;      /* My mummy said not to listen to strangers! */
}
  
struct targ {
    struct iface *iface;
    struct timer *t;
};
  
void nr_poll_resp(struct targ *targ) {
  
    nr_bcnodes(targ->iface);
    free(targ->t);
    free(targ);
}
  
void nr_poll_respond(struct iface *iface) {
  
    struct timer *t;
    struct targ *targ;
  
    /* Set the random delay timer */
    t = mallocw(sizeof(struct timer));
    targ = mallocw(sizeof(struct targ));
  
    /* Set the arguments */
    targ->iface = iface;
    targ->t = t;
    t->func = (void (*)__ARGS((void *)))nr_poll_resp;
    t->arg = targ;

#ifdef UNIX
#define random(x) ((rand() * (x))/(RAND_MAX))
#endif

    set_timer(t,random(30000) + 1);
    start_timer(t);
  
}
  
/* receive and process node broadcasts. */
void
nr_nodercv(iface,source,bp)
struct iface *iface;
char *source;
struct mbuf *bp;
{
    char bcalias[AXALEN];
    struct nr3dest ds;
    int qual,poll;
    unsigned char c;
  
    /* First, see if this is even a net/rom interface: */
    if(!(iface->flags & IS_NR_IFACE)){
        free_p(bp);
        return;
    }
  
    if ((qual = accept_bc(source,iface)) == 0) {    /* check against filter */
        free_p(bp) ;                /* and get quality */
        return ;
    }
  
    c = PULLCHAR(&bp);
    /* is this a route update poll from a neigbour ? - WG7J */
    poll = 0;
    if(c == NR3POLLSIG) {
        poll = 1;
        nr_poll_respond(iface);
    } else if(c != NR3NODESIG) {
        free_p(bp);
        return;
    }
  
    /* now try to get the alias */
    if(pullup(&bp,bcalias,ALEN) < ALEN){
        free_p(bp);
        return;
    }
  
    /* now check that the alias field is not corrupted - saftey measure! */
    if (nr_aliasck(bcalias)) {
        free_p(bp);
        return;
    }
  
    bcalias[ALEN] = '\0';       /* null terminate */
  
    /* Make sure that we are not hearing our own broadcasts through
     * a diode matrix - 10/95 K2MF */
    if(!addreq(Nr_iface->hwaddr,source)) {
#ifdef NR_BC_RESPOND
        /* If we were polled, we've already sent the routes list - WG7J */
        if(!poll && (find_nrnbr(source,iface) == NULLNTAB)) /* a new node ! */
            nr_poll_respond(iface);
#endif
  
        /* enter the neighbor into our routing table */
        if(qual == -1)
            qual = iface->quality; /* use default quality */
  
        nr_routeadd(bcalias, source, iface, qual, source, 0, 0);
  
        if(c == NR3NODESIG) {
            /* we've digested the header; now digest the actual */
            /* routing information */
            while(ntohnrdest(&ds,&bp) != -1){
 
#ifdef	INP2011
				/* 22Sep2011, Maiko (VE4KLM), Ignore tmp entries */
				if (alias2ignore (ds.alias))
					continue;

				/* 22Sep2011, Maiko (VE4KLM), New function */
				if (callnocall (ds.dest))
					continue;
#endif 
                if(nr_aliasck(ds.alias)
                || addreq(Nr_iface->hwaddr,ds.dest)
                || addreq(Nr_iface->hwaddr,ds.neighbor))
                    /* We ignore routes with corrupted aliases
                     * and loopback paths to ourselves */
                    continue;

                ds.quality = ((ds.quality * qual + 128) / 256) & 0xff;
  
                /* ignore routes below the minimum quality threshhold */
                if(ds.quality < Nr_autofloor)
                    continue;
  
                nr_routeadd(ds.alias,ds.dest,iface,ds.quality,source,0,0);
            }
        }
    }
    free_p(bp); /* This will free the mbuf if anything fails above */
}
  
  
/* The following are utilities for manipulating the routing table */
  
/* hash function for callsigns.  Look familiar? */
int16
nrhash(s)
char *s;
{
    register char x;
    register int i;
  
    x = 0;
    for(i = ALEN; i !=0; i--)
        x ^= *s++ & 0xfe;
    x ^= *s & SSID;
    return (int16)(uchar(x) % NRNUMCHAINS);
}
  
/* Find a neighbor table entry.  Neighbors are determined by
 * their callsign and the interface number.  This takes care
 * of the case where the same switch or hosts uses the same
 * callsign on two different channels.  This isn't done by
 * net/rom, but it might be done by stations running *our*
 * software.
 */
struct nrnbr_tab *
find_nrnbr(addr,ifp)
register char *addr;
struct iface *ifp;
{
    int16 hashval;
    register struct nrnbr_tab *np;
  
    /* Find appropriate hash chain */
    hashval = nrhash(addr);
  
    /* search hash chain */
    for(np = Nrnbr_tab[hashval]; np != NULLNTAB; np = np->next){
        /* convert first in  list to ax25 address format */
        if(addreq(np->call,addr) && np->iface == ifp){
            return np;
        }
    }
    return NULLNTAB;
}
  
/* Try to find the AX.25 address of a node with the given call or alias.
 * Return a pointer to the route if found, otherwize NULLNRRTAB.
 * alias should be a six character, blank-padded, upper-case string.
 * call should be a upper-case string.
 * 12-21-91, WG7J
 */
  
struct nrroute_tab *
find_nrboth(alias,call)
char *alias;
char *call;
{
    int i;
    register struct nrroute_tab *rp;
    char tmp[AXBUF];
  
    /* Since the route entries are hashed by ax.25 address, we'll */
    /* have to search all the chains */
  
    for(i = 0; i < NRNUMCHAINS; i++)
        for(rp = Nrroute_tab[i]; rp != NULLNRRTAB; rp = rp->next)
            if( (strncmp(alias, rp->alias, 6) == 0) ||
                (strcmp(call,pax25(tmp,rp->call)) == 0) )
                return rp;
  
    /* If we get to here, we're out of luck */
    return NULLNRRTAB;
}
  
/* Find a route table entry */
struct nrroute_tab *
find_nrroute(addr)
register char *addr;
{
    int16 hashval;
    register struct nrroute_tab *rp;
  
    /* Find appropriate hash chain */
    hashval = nrhash(addr);
  
    /* search hash chain */
    for(rp = Nrroute_tab[hashval]; rp != NULLNRRTAB; rp = rp->next){
        if(addreq(rp->call,addr)){
            return rp;
        }
    }
    return NULLNRRTAB;
}
  
/* Try to find the AX.25 address of a node with the given alias.  Return */
/* a pointer to the AX.25 address if found, otherwise NULLCHAR.  The alias */
/* should be a six character, blank-padded, upper-case string. */
  
char *
find_nralias(alias)
char *alias;
{
    int i;
    register struct nrroute_tab *rp;
  
    /* Since the route entries are hashed by ax.25 address, we'll */
    /* have to search all the chains */
  
    for(i = 0; i < NRNUMCHAINS; i++)
        for(rp = Nrroute_tab[i]; rp != NULLNRRTAB; rp = rp->next)
            if(strncmp(alias, rp->alias, 6) == 0)
                return rp->call;
  
    /* If we get to here, we're out of luck */
  
    return NULLCHAR;
}
  
  
/* Find a binding in a list by its neighbor structure's address */
static struct nr_bind *find_binding(list,neighbor)
struct nr_bind *list;
register struct nrnbr_tab *neighbor;
{
    register struct nr_bind *bp;
  
    for(bp = list; bp != NULLNRBIND; bp = bp->next)
        if(bp->via == neighbor)
            return bp;
  
    return NULLNRBIND;
}
  
/* Find the worst quality non-permanent binding in a list */
static
struct nr_bind *
find_worst(list)
struct nr_bind *list;
{
    register struct nr_bind *bp;
    struct nr_bind *worst = NULLNRBIND;
    unsigned minqual = 1000;    /* infinity */
  
    for(bp = list; bp != NULLNRBIND; bp = bp->next)
        if(!(bp->flags & NRB_PERMANENT) && bp->quality < minqual){
            worst = bp;
            minqual = bp->quality;
        }
  
    return worst;
}
  
/* Find the binding for a given neighbour in a list of routes */
struct nr_bind *
find_bind(list,np)
struct nr_bind *list;
struct nrnbr_tab *np;
{
    struct nr_bind *bp;
  
    for(bp = list; bp != NULLNRBIND; bp = bp->next)
        if(bp->via == np)
            break;
    return bp;
}
  
/* Find the best binding of any sort in a list.  If obso is 1,
 * include entries below the obsolescence threshhold in the
 * search (used when this is called for routing broadcasts).
 * If it is 0, routes below the threshhold are treated as
 * though they don't exist.
 *
 * 07Sep2011, Maiko, Let's try to use transport time if available,
 * and use it instead of quality to determine where we route. Very
 * experimental right now, but I'm curious how this works. I need
 * to come up with an ingenious way to choose between TT and just
 * plain quality, without favoring one over the other ...
 *
 * Define USE_TT_ROUTING to enable this ...
 */
struct nr_bind *
find_best(list,obso)
struct nr_bind *list;
unsigned obso;
{
    register struct nr_bind *bp;
    struct nr_bind *best = NULLNRBIND;
    int maxqual = -1;   /* negative infinity */
#ifdef	USE_TT_ROUTING
	struct nr_bind *besttt = NULLNRBIND;
	struct nrnbr_tab *np;
    int best_tt = 99999;	/* positive infinity */
#endif
    for(bp = list; bp != NULLNRBIND; bp = bp->next)
	{
        if((int)bp->quality > maxqual)
		{
            if(obso || bp->obsocnt >= Obso_minbc)
			{
                best = bp;
                maxqual = bp->quality;
            }
		}
#ifdef	USE_TT_ROUTING
		np = bp->via;
        if (np->inp_state && np->rtt && ((np->rtt + bp->tt) < best_tt))
		{
			best_tt = np->rtt + bp->tt;
			besttt = bp;
		}
#endif
	}

#ifdef	USE_TT_ROUTING
	if (besttt != NULLNRBIND)
	{
		if (Nr_debug)
			log (-1, "use tt [%d] over quality [%d]", best_tt, maxqual);

		return besttt;
	}
	else
#endif
 
    return best;
}

/*
 * 24Aug2011, Maiko (VE4KLM), I find nr_routeadd() to be a bloated function
 * and would like the ability to call individual sections for the new INP
 * functionality and possible other things down the road. So let's break
 * up nr_routeadd() code into smaller functions like those below :
 */

struct nrroute_tab *add_nrroute (char *alias, char *dest)
{
	struct nrroute_tab *rp;
	int16 rhash;	/* me is bad, i was declaring this as just int */

	rp = (struct nrroute_tab *)callocw(1,sizeof(struct nrroute_tab));

	/* create a new route table entry */
	strncpy(rp->alias,alias,6);
	memcpy(rp->call,dest,AXALEN);
	rhash = nrhash(dest);
	rp->next = Nrroute_tab[rhash];

	if(rp->next != NULLNRRTAB)
		rp->next->prev = rp;

	Nrroute_tab[rhash] = rp;    /* link at head of hash chain */

	return rp;
}

struct nr_bind *add_binding (struct nrnbr_tab *np, struct nrroute_tab *rp)
{
	struct nr_bind *bp;

	bp = (struct nr_bind *)callocw(1,sizeof(struct nr_bind));

	/* create a new binding and link it in */
	bp->via = np;   /* goes via this neighbor */
	bp->next = rp->routes;  /* link into binding chain */

	if(bp->next != NULLNRBIND)
    	bp->next->prev = bp;

	rp->routes = bp;
	rp->num_routes++;   /* bump route count */
	np->refcnt++;       /* bump neighbor ref count */
	bp->obsocnt = Obso_init;    /* use initial value */

	return bp;
}

struct nrnbr_tab *add_nrnbr (char *neighbor, struct iface *ifp)
{
	struct nrnbr_tab *np;

	int16 nhash;	/* me is bad, i was declaring this as just int */

	np = (struct nrnbr_tab *)callocw(1,sizeof(struct nrnbr_tab));

	/* create a new neighbor entry */
	memcpy(np->call,neighbor,AXALEN);
	np->iface = ifp;
	nhash = nrhash(neighbor);
	np->next = Nrnbr_tab[nhash];

	if(np->next != NULLNTAB)
		np->next->prev = np;

	Nrnbr_tab[nhash] = np;

	return np;
}

/* Add a route to the net/rom routing table */

/*
 * 24Aug2011, Maiko, I don't want to modify the number of arguments, I would
 * rather have this function return the *current* binding, which I can then
 * update the record outside of this call (ie, with INP tt and hops, etc),
 * so this function will now return PTR to the *current* active nr_bind !
 */
struct nr_bind *nr_routeadd (alias,dest,ifp,quality,neighbor,permanent,record)
char *alias;	/* net/rom node alias, blank-padded and */
          		/* null-terminated */
char *dest;		/* destination node callsign */
struct iface *ifp;
unsigned quality;   /* route quality */
char *neighbor; /* neighbor node + 2 digis (max) in arp format */
unsigned permanent; /* 1 if route is permanent (hand-entered) */
unsigned record;    /* 1 if route is a "record route" */
{
    struct nrroute_tab *rp;
    struct nr_bind *bp, *rbp;	/* 24Aug, Maiko, add rbp for return value */
    struct nrnbr_tab *np;

	jnos_lock (JLOCK_NR);

    /* See if a routing table entry exists for this destination */
    if ((rp = find_nrroute(dest)) == NULLNRRTAB)
		rp = add_nrroute (alias, dest);

	else if(permanent || !strncmp(rp->alias,"##temp",6))
        strncpy(rp->alias,alias,6); /* update the alias */
  
    /* See if an entry exists for this neighbor */
    if ((np = find_nrnbr(neighbor,ifp)) == NULLNTAB)
		np = add_nrnbr (neighbor, ifp);

	else if(permanent) /* force this path to the neighbor */
        memcpy(np->call,neighbor,AXALEN);

    /* See if there is a binding between the dest and neighbor */
    if((bp = find_binding(rp->routes,np)) == NULLNRBIND)
	{
		bp = add_binding (np, rp);

		bp->quality = quality;

        if(permanent)
            bp->flags |= NRB_PERMANENT;
        else if(record) /* notice permanent overrides record! */
            bp->flags |= NRB_RECORDED;
    }
	else
	{
        if(permanent) /* permanent request trumps all */
		{
            bp->quality = quality;
            bp->obsocnt = Obso_init;
            bp->flags |= NRB_PERMANENT;
            bp->flags &= ~NRB_RECORDED; /* perm is not recorded */
        }
		else if(!(bp->flags & NRB_PERMANENT)) /* not permanent */
		{
            if(record) /* came from nr_route */
			{
                if(bp->flags & NRB_RECORDED) /* no mod non-rec bindings */
				{
                    bp->quality = quality;
                    bp->obsocnt = Obso_init; /* freshen recorded routes */
                }
            }
			else /* came from a routing broadcast */
			{
                bp->quality = quality;
                bp->obsocnt = Obso_init;
                bp->flags &= ~NRB_RECORDED; /* no longer a recorded route */
            }
        }
    }
 
	rbp = bp;	/* 24Aug2011, Maiko, Save the *active* bind */
 
    /* Now, check to see if we have too many bindings, and drop */
    /* the worst if we do */
    if (rp->num_routes > Nr_maxroutes)
	{
        /* since find_worst never returns permanent entries, the */
        /* limitation on number of routes is circumvented for    */
        /* permanent routes */

        if((bp = find_worst(rp->routes)) != NULLNRBIND)
		{
            nr_routedrop(dest,bp->via->call,bp->via->iface);

			if (bp == rbp)	/* Should check for this, you never know */
			{
				jnos_unlock (JLOCK_NR);

				return (struct nr_bind*)0;
			}
        }
    }

	jnos_unlock (JLOCK_NR);

	/* 24Aug2011, Maiko, This function now returns PTR to binding */
	return rbp;

    // return 0;
}

/* Reset the netrom links to this neighbour, since the
 * route to it just expired - WG7J
 */
void
nrresetlinks(struct nrroute_tab *rp) {
    int i ;
    struct nr4cb *cb ;
  
    for(i = 0 ; i < NR4MAXCIRC ; i++)
        if((cb = Nr4circuits[i].ccb) != NULLNR4CB)
            if(!memcmp(cb->remote.node,rp->call,AXALEN))
                reset_nr4(cb);
}

/*
 * 13Sep2011, Maiko, Yet another function split, I just want something
 * to unbind routing entries from a node, nothing more, don't delete the
 * node or neighbour, just leave the node *routeless* if it makes sense.
 */
void nr_unbind (struct nrroute_tab *rp,
	struct nrnbr_tab *np, struct nr_bind *bp)
{
    /* drop the binding first */
    if(bp->next != NULLNRBIND)
        bp->next->prev = bp->prev;
    if(bp->prev != NULLNRBIND)
        bp->prev->next = bp->next;
    else
        rp->routes = bp->next;
  
    free((char *)bp);
    rp->num_routes--;       /* decrement the number of bindings */
    np->refcnt--;           /* and the number of neighbor references */
}

/*
 * 28Aug2011, Maiko, Split the reading of tables and the actual
 * dropping part into two functions. Trying to make this more CPU
 * friendly for the new INP functionality that I am working on.
 *
 * No point reading all 3 structures when you have them already !
 */

void nr_routedrop_postread (char *dest, char *neighbor, struct nrroute_tab *rp, struct nrnbr_tab *np, struct nr_bind *bp)
{
    /* drop the binding first (13Sep2011, new function split out) */
    nr_unbind (rp, np, bp);

    /* now see if we should drop the route table entry */
    if(rp->num_routes == 0){
        if(rp->next != NULLNRRTAB)
            rp->next->prev = rp->prev;
        if(rp->prev != NULLNRRTAB)
            rp->prev->next = rp->next;
        else
            Nrroute_tab[nrhash(dest)] = rp->next;
        /* No more routes left !
         * We should close/reset any netrom connections
         * still idling for this route ! - WG7J
         */
        nrresetlinks(rp);
  
        free((char *)rp);
    }
  
    /* and check to see if this neighbor can be dropped */
    if(np->refcnt == 0){
        if(np->next != NULLNTAB)
            np->next->prev = np->prev;
        if(np->prev != NULLNTAB)
            np->prev->next = np->next;
        else
            Nrnbr_tab[nrhash(neighbor)] = np->next;
  
        free((char *)np);
    }
}

/* Drop a route to dest via neighbor */
int nr_routedrop (char *dest, char *neighbor, struct iface *ifp)
{
    register struct nrroute_tab *rp;
    register struct nrnbr_tab *np;
    register struct nr_bind *bp;
  
    if((rp = find_nrroute(dest)) == NULLNRRTAB)
        return -1;
  
    if((np = find_nrnbr(neighbor,ifp)) == NULLNTAB)
        return -1;
  
    if((bp = find_binding(rp->routes,np)) == NULLNRBIND)
        return -1;

	/* 28Aug2011, Maiko, The code that was here, split into a new function */
    nr_routedrop_postread (dest, neighbor, rp, np, bp);
  
    return 0;
}
  
/* Find an entry in the filter table */
static struct nrnf_tab *
find_nrnf(addr,ifp)
register char *addr;
struct iface *ifp;
{
    int16 hashval;
    register struct nrnf_tab *fp;
  
    /* Find appropriate hash chain */
    hashval = nrhash(addr);
  
    /* search hash chain */
    for(fp = Nrnf_tab[hashval]; fp != NULLNRNFTAB; fp = fp->next){
        if(addreq(fp->neighbor,addr) && (fp->iface == ifp)){
            return fp;
        }
    }
  
    return NULLNRNFTAB;
}
  
/* Add an entry to the filter table.  Return 0 on success,
 * -1 on failure
 */
int
nr_nfadd(addr,ifp,qual)
char *addr;
struct iface *ifp;
unsigned qual;
{
    struct nrnf_tab *fp;
    int16 hashval;
  
    if(find_nrnf(addr,ifp) != NULLNRNFTAB)
        return 0;   /* already there; it's a no-op */
  
    fp = (struct nrnf_tab *)callocw(1,sizeof(struct nrnf_tab));
  
    hashval = nrhash(addr);
    memcpy(fp->neighbor,addr,AXALEN);
    fp->iface = ifp;
    fp->next = Nrnf_tab[hashval];
    fp->quality = qual;
    if(fp->next != NULLNRNFTAB)
        fp->next->prev = fp;
    Nrnf_tab[hashval] = fp;
  
    return 0;
}
  
/* Drop a neighbor from the filter table.  Returns 0 on success, -1
 * on failure.
 */
int
nr_nfdrop(addr,ifp)
char *addr;
struct iface *ifp;
{
    struct nrnf_tab *fp;
  
    if((fp = find_nrnf(addr,ifp)) == NULLNRNFTAB)
        return -1;  /* not in the table */
  
    if(fp->next != NULLNRNFTAB)
        fp->next->prev = fp->prev;
    if(fp->prev != NULLNRNFTAB)
        fp->prev->next = fp->next;
    else
        Nrnf_tab[nrhash(addr)] = fp->next;
  
    free((char *)fp);
  
    return 0;
}
  
/* called from lapb whenever a link failure implies that a particular ax25
 * path may not be able to carry netrom traffic too well. Experimental!!!!
 */
void nr_derate(axp)
struct ax25_cb *axp;
{
    register struct nrnbr_tab *np ;
    register struct nrroute_tab *rp;
    register struct nr_bind *bp;
    struct mbuf *buf;
    int i;
    int nr_traffic = 0; /* assume no netrom traffic on connection */
  
    if(!Nr_derate)
        return;     /* derating function is disabled */
  
    /* is this an active netrom interface ? */
    if (!(axp->iface->flags & IS_NR_IFACE))
        return ;
  
    if (axp == NULLAX25)
        return;         /* abandon ship! */
  
    /* If it is valid for netrom traffic, lets see if there is */
    /* really netrom traffic on the connection to be derated.  */
    for (buf = axp->txq; buf != NULLBUF; buf = buf->anext)
        if ((buf->data[0] & 0xff) == PID_NETROM)
            nr_traffic = 1;     /* aha - netrom traffic! */
  
    if (!nr_traffic)
        return;     /* no sign of being used by netrom just now */
  
    /* we now have the appropriate interface entry */
    for (i = 0 ; i < NRNUMCHAINS ; i++) {
        for (rp = Nrroute_tab[i] ; rp != NULLNRRTAB ; rp = rp->next) {
            for (bp = rp->routes ; bp != NULLNRBIND ; bp = bp->next) {
                np = bp->via;
                if(bp->quality >= 1 && np->iface == axp->iface &&
                    !(bp->flags & NRB_PERMANENT) &&
                    !memcmp(np->call,axp->remote,ALEN) &&
                (np->call[6] & SSID) == (axp->remote[6] & SSID)) {
                    bp->quality = ((bp->quality * 2) / 3);
                }
            }
        }
    }
}
  
#endif /* NETROM */
  
