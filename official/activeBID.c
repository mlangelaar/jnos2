
/*
 * 21Oct2020, Maiko (VE4KLM), We need to track ALL messages concurrently being
 * forwarded by all forwarding sessions (threads) and make sure multiple remote
 * systems are NOT forwarding the same message (with same BID) to us at the same
 * time or even minutes apart. As demonstrated, this can lead to JNOS accepting
 * all of them, in turn creating duplicate bulletins that can get forwarded to
 * others in turn, which is just not a good thing to be happening.
 *
 * 23Oct2020, Moved into it's own source file, so other xNOS variants can take
 * this code and easily integrated it into their own. If you have reservations
 * about upgrading to 2.0m.5B (at press time), I still strongly urge people to
 * at least integrate this code into their xNOS system, to cut down on dupes.
 *
 * So 'fbbfwd.c' calls chk4activebid(), 'mboxmail.c' calls delactivebid()
 *
 * Up til now, staggering forwards with partners has really been the only way
 * to avoid the issue of concurrent messages with identical BID coming from a
 * whole wack of remote systems and generating duplicate bulletins. This code
 * should stop or significantly reduce the number of duplication bulletins.
 *
 *  "you can control what is sent out, but not necessarily what comes in"
 *
 */

#include "global.h"

#include "activeBID.h"	/* just to make sure I'm paying attention */

struct actbidlist {

	char *system;
	char *msgbid;

	struct actbidlist *next;
};

static struct actbidlist *actbidlist = (struct actbidlist*)0;

int delactivebid (int s, char *msgbid)
{
	struct actbidlist *prev, *ptr;

	for (prev = ptr = actbidlist; ptr; prev = ptr, ptr = ptr->next)
	{
		if (!strcmp (ptr->msgbid, msgbid))
			break;
	}

	if (ptr)
	{
		/*
		 * Maiko (VE4KLM), For any of you xNOS developers out there, even with
 		 * a non-preemptive kernel like ours, it is dangerous to put any kind
		 * of function that can possibly 'wait for I/O' inside of this block.
		 *
		 * If you are in the middle of rearranging pointers in a link list as
		 * this block of code does, then do it in one shot. Do NOT put log ()
		 * calls smack in the middle of it, which introduces the risk of the
		 * CPU being handed off to another user space thread. I guarantee at
		 * some point JNOS will crash, depends how busy the system is.
		 *
		log (s, "chk4activebid - done with [%s] from [%s]",
			ptr->msgbid, ptr->system);
		 *
		 * If you still don't understand, then ask yourself what happens when
		 * this section resumes execution after the log () call is passed back
		 * to us and the pointers have changed in the meantime ? corruption.
		 */

		if (ptr == actbidlist)
			actbidlist = (struct actbidlist*)0;
		else
 			prev->next = ptr->next;

		/* free up allocation inside the structure first */

		free (ptr->system);
		free (ptr->msgbid);

		/* now release the structure itself */

		free (ptr);
	}
#ifdef	BID_DEBUGGING
	else log (s, "delactivebid [%s] - strange, contact Maiko", msgbid);

	log (s, "chk4activebid - done with [%s]", msgbid);
#endif
	return 1;
}

int chk4activebid (int s, char *system, char *msgbid)
{
	struct actbidlist *ptr = actbidlist;

	while (ptr)
	{
		/* found a message, so tell system NO WAY right now */

		if (!strcmp (ptr->msgbid, msgbid))
		{
			/*
			 * 24Oct2020, Maiko (VE4KLM), same rationale as above, probably
			 * I should not be putting this log call here, rather it should
			 * be moved outside this function, and put in fbbfwd.c instead.
		 	 *
			log (s, "chk4activebid - already processing [%s] from [%s]",
				ptr->msgbid, ptr->system);
			 *
			 */

			return 1;
		}

		ptr = ptr->next;
	}

	/* if nothing found, then add the current message */

	ptr = mallocw (sizeof (struct actbidlist));
	ptr->system = j2strdup (system);
	ptr->msgbid = j2strdup (msgbid);
	ptr->next = actbidlist;
	actbidlist = ptr;

#ifdef	BID_DEBUGGING
	log (s, "chk4activebid - processing [%s] from [%s]",
		ptr->msgbid, ptr->system);
#endif
	return 0;
}

