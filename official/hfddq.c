
/* 28Aug2008, Maiko, NEW routines for proper sync'd hostmode traffic */

#include "global.h"
#include "mbuf.h"
#include "proc.h"

struct hfddq {

	char *data;

	int len;	/* length of data */

	int type;	/* 0 = nothing, 1 = data, 2 = coordinated changeover */

	struct hfddq *next;

	int dev;
};

static struct hfddq	*rootq = (struct hfddq*)0;

/*
 * 29Aug2008, Maiko, New priority HFDD queue (one entry) so that I can CUT
 * into line, if I need to send something immediately, instead of waiting for
 * the contents of the regular HFDD queue to be dequeued and sent.
 */
static struct hfddq NOW_q = { (char*)0, 0, 0, (struct hfddq*)0, 0 };

static int lock_rootq = 0;

void delete_priority ()
{
	if (NOW_q.len)
	{
//		log (-1, "delete priority message");

		free (NOW_q.data);
		NOW_q.len = 0;
	}
}

void priority_send (int dev, char *data, int len)
{
	if (NOW_q.len)
		return;

//	log (-1, "queue priority message");

	NOW_q.data = mallocw (len);
	memcpy (NOW_q.data, data, len);
	NOW_q.len = len;
	NOW_q.dev = dev;
}

/*
 * Early September, 2008, Maiko, New QUEUE based hfdd send function. The
 * old way I was (still am for the kam, scs, and dxp) just sending stuff
 * to the modems without a proper queue order of commands. This new func
 * which I created for the new PK232 driver, ensures that commands are
 * sent in an orderly (queued) fashion, to minimize conflicts between
 * commands sent on the fly, etc. Eventually, I'll do away with the
 * original 'hfdd_send' when I update the other modem drivers.
 */
void NEW_hfdd_send (int dev, char *data, int len)
{
	struct hfddq *ptr;

	while (lock_rootq)
		pwait (NULL);

	lock_rootq = 1;

	/* SIMPLE - put it at the head of the HFDD queue */

	ptr = (struct hfddq*)callocw(1, sizeof(struct hfddq));
	ptr->next = rootq;
	rootq = ptr;

	ptr->data = mallocw (len);
	memcpy (ptr->data, data, len);
	ptr->len = len;
	ptr->dev = dev;

	/*
	 * 29Aug2008, Maiko, Kludge specific to pk232, identify data
	 * for which we have to wait for ack from the tnc.
	 */
	if (data[1] == 0x20)
		ptr->type = 1;
	else if (data[2] == 'O')
		ptr->type = 2;
	else
		ptr->type = 0;

	// log (-1, "queued (%d) [%.*s]", ptr->len, ptr->len, ptr->data);

	lock_rootq = 0;
}

extern void asy_dump (int, unsigned char*);
extern void asy_send (int, struct mbuf*);

extern int wait_4_ack;	/* 29Aug2008 */

extern int echo_bytes;	/* 21Sep2008 - only cont when ALL bytes echoed back */

int send_tnc_block ()
{
	struct hfddq *qptr, *prevqptr = (struct hfddq*)0;

	struct mbuf *bp;

	int sent = 0;

	/*
	 * 29Aug2008, Maiko, What if I need to send something now, essentially
	 * ignoring the regular HFDD queue - cutting in line so to speak !!!
	 */
	if (NOW_q.len)
	{
		bp = pushdown (NULLBUF, NOW_q.len);
		memcpy (bp->data, NOW_q.data, NOW_q.len);
		asy_dump (NOW_q.len, (unsigned char*)NOW_q.data);
		asy_send (NOW_q.dev, bp);

		return 1;
	}

	while (lock_rootq)
		pwait (NULL);

	lock_rootq = 1;

	qptr = rootq;

	/* NOT SO SIMPLE - read it from the end of the HFDD queue */

	while (qptr != (struct hfddq*)0)
	{
		if (qptr->next != (struct hfddq*)0)
		{
			prevqptr = qptr;
			qptr = qptr->next;
		}
		else
			break;
	}

	if (qptr != (struct hfddq*)0)
	{	
		// log (-1, "dequeue (%d) [%.*s]", qptr->len, qptr->len, qptr->data);

		/* 29Aug2008, Maiko, Kludge, if data, wait for _XX ack */
		if (qptr->type == 1)
		{
			echo_bytes = qptr->len - 3; /* don't want the delimiters and cmd */
			log (-1, "expecting %d echo bytes in total", echo_bytes);
			wait_4_ack = 1;
		}

		bp = pushdown (NULLBUF, qptr->len);
		memcpy (bp->data, qptr->data, qptr->len);
		asy_dump (qptr->len, (unsigned char*)qptr->data);
		asy_send (qptr->dev, bp);

		if (prevqptr != (struct hfddq*)0)
			prevqptr->next = (struct hfddq*)0;
		else
			rootq = (struct hfddq*)0;

		free (qptr->data);
		free (qptr);

		sent = 1;	/* indicate that we actually sent something to tnc */
	}

	lock_rootq = 0;

	return sent;
}

