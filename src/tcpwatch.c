/*
 * A TCP Watch Dog of sorts, I have noticed a lot of SYN attacks towards various
 * ports on JNOS, and so this is an attempt to reset any TCB entries stuck in a
 * SYN RECEIVED or FIN WAIT state for longer then they should be in. What is an
 * acceptable amount of time to define this 'should be in' ? Don't know, but we
 * can try 30 seconds, that seems reasonable, no ? The idea to make sure JNOS
 * does not 'run out out resources ' type of thing. We'll use a timer approach,
 * so JNOS will check the TCB list every 30 seconds, and reset anything 'old'.
 *
 * 12Apr2014, Maiko (VE4KLM), first attempt.
 *
 */

#include "global.h"
#include "timer.h"
#include "usock.h"
#include "tcp.h"

static struct timer TcpWatchTimer;

/* 15Apr2016, Maiko, This should be declared as a pointer to tcb, not a long,
 * not sure why I did this originally, cause I was being lazy ? don't know.
 *
static long thewdlist[20];
 */

static struct tcb *thewdlist[20];

#ifdef	DONT_COMPILE	/* 07May14, Maiko, Not needed anymore */

/* 12Apr2014, Maiko (VE4KLM), Created new function so I can better log stuff */
extern int gettcbsocket (struct tcb *tcbp);	/* moved into socket.c */

#endif

/* 19Apr2014, Maiko (VE4KLM), Let's add TCP_FINWAIT1 and TCP_FINWAIT2 as well */

static int watched (struct tcb *tcbp)
{
	int retval = 0;

	switch (tcbp->state)
	{
		case TCP_SYN_RECEIVED:
		case TCP_FINWAIT1:
		case TCP_FINWAIT2:
			retval = 1;
			break;
	}

	return retval;
}

void rstwdlist ()
{
	register struct tcb *tcbp;

	int cnt = 0;

	while (cnt < 20)
	{
		if (thewdlist[cnt])
		{
			tcbp = thewdlist[cnt];

	    	if (tcpval (tcbp) && watched (tcbp))
			{
				log (/* gettcbsocket (tcbp) */ -1, "tcp reset %s:%d",
					inet_ntoa (tcbp->conn.remote.address), tcbp->conn.remote.port);

			    reset_tcp (tcbp);
			}

//			thewdlist[cnt] = 0L;	/* make available for someone else */
			thewdlist[cnt] = (struct tcb*)0;
		}

		cnt++;
	}
}

void addwdlist (struct tcb *tcbp)
{
	int cnt = 0;

	while (cnt < 20)
	{
//		if (thewdlist[cnt] == 0L)
		if (thewdlist[cnt] == (struct tcb*)0)
		{
			thewdlist[cnt] = tcbp;

			log (/* gettcbsocket (tcbp)*/ -1, "tcp watch %s:%d",
				inet_ntoa (tcbp->conn.remote.address), tcbp->conn.remote.port);

			return;
		}

		cnt++;
	}

	log (-1, "tcp watch - out of resources");
}

void dowatchtick (void)
{
    register struct tcb *tcbp;
 
	rstwdlist ();	/* process current list, reset anything if need be */ 

    /* now loop through all the current TCB entries */
    for (tcbp = Tcbs; tcbp != NULLTCB; tcbp = tcbp->next)
    {
		if (watched (tcbp))
			addwdlist (tcbp);
    }
  
    /* Restart timer */
    start_timer (&TcpWatchTimer) ;
}

int dotcpwatch (int argc, char** argv, void *p)
{
    if(argc < 2)
    {
        tprintf ("TCP Watch Dog timer %d/%d seconds\n",
        	read_timer (&TcpWatchTimer)/1000,
        		dur_timer(&TcpWatchTimer)/1000);

        return 0;
    }

    stop_timer (&TcpWatchTimer);	/* in case it's already running */

	/* what to call on timeout */
    TcpWatchTimer.func = (void (*)(void*))dowatchtick;

    TcpWatchTimer.arg = NULLCHAR;	/* dummy value */

	/* set timer duration */
    set_timer (&TcpWatchTimer, (uint32)atoi (argv[1])*1000);

    start_timer (&TcpWatchTimer);	/* and fire it up */

    return 0;
}

