#ifndef	_PROC_H
#define	_PROC_H

/*
 * 19Sep2006, Maiko Langelaar (VE4KLM), Time to clean up this module for
 * post JNOS 2.0e era. The setjmp/longjmp functions are becoming increasely
 * more difficult to use. The newer glibc mangles the pointers, so I think
 * it's time to switch over to using the ucontext functions instead.
 *
 * Filename : proc.glibc2.4+.h - LINUX specific !!!
 *
 */

#include <ucontext.h>

#ifndef _MBUF_H
#include "mbuf.h"
#endif

#ifndef	_TIMER_H
#include "timer.h"
#endif

#ifdef __GNUC__
struct session;    /* forward declaration for GCC */
#endif
#ifdef __TURBOC__
#ifndef _SESSION_H
#include "session.h"
#endif
#endif

#define	OUTBUFSIZE	512	/* Size to be malloc'ed for outbuf */

/* Kernel process control block */
#define	PHASH	16		/* Number of wait table hash chains */
struct proc {
	struct proc *prev;	/* Process table pointers */
	struct proc *next;	

	ucontext_t env;

	char i_state;		/* Process interrupt state */

	unsigned short state;

#define	READY	0
#define	WAITING	1
#define	SUSPEND	2
#ifdef __STDC__
	volatile
#endif
	void *event;		/* Wait event */
	int16 *stack;		/* Process stack */
	unsigned stksize;	/* Size of same */
	char *name;		/* Arbitrary user-assigned name */
	int retval;		/* Return value from next pwait() */
	struct timer alarm;	/* Alarm clock timer */
	struct mbuf *outbuf;	/* Terminal output buffer */
	int input;		/* standard input socket */
	int output;		/* standard output socket */
	int iarg;		/* Copy of iarg */
	void *parg1;		/* Copy of parg1 */
	void *parg2;		/* Copy of parg2 */
	int freeargs;		/* Free args on termination if set */
	struct session *session;/* for dns_query & UNIX session manager */
};
#define NULLPROC (struct proc *)0
extern struct proc *Waittab[];	/* Head of wait list */
extern struct proc *Rdytab;	/* Head of ready list */
extern struct proc *Curproc;	/* Currently running process */
extern struct proc *Susptab;	/* Suspended processes */
extern int Stkchk;		/* Stack checking flag */

/* In  kernel.c: */
void alert __ARGS((struct proc *pp,int val));
void chname __ARGS((struct proc *pp,char *newname));
void killproc __ARGS((struct proc *pp));
void killself __ARGS((void));
struct proc *mainproc __ARGS((char *name));
struct proc *newproc __ARGS((char *name,unsigned int stksize,
	void (*pc) __ARGS((int,void *,void *)),
	int iarg,void *parg1,void *parg2,int freeargs));

int j2psignal (volatile void*, int);	/* 20May2009, Maiko, add j2 suffix */

int pwait __ARGS((volatile void *event));
void resume __ARGS((struct proc *pp));
void suspend __ARGS((struct proc *pp));

/* In ksubr.c: */
void chkstk __ARGS((void));
void kinit __ARGS((void));
unsigned int stkutil __ARGS((struct proc *pp));
/* 19Sep2006, Maiko, only used in kernel.c, so moved out
Static unsigned phash __ARGS((volatile void *event));
*/
void psetup __ARGS((struct proc *pp,int iarg,void *parg1,void *parg2,
	void  __ARGS(((*pc) __ARGS((int,void *,void *)) )) ));
#ifdef	AMIGA
void init_psetup __ARGS((struct proc *pp));
#endif

/* Stack background fill value for high water mark checking */
#define	STACKPAT	0x55aa

/* Value stashed in location 0 to detect null pointer dereferences */
#define	NULLPAT		0xdead

#ifdef UNIX
#ifdef linux
#define MAINSTKBASE 0xC0000000UL
#else
#define MAINSTKBASE 0x80000000UL
#endif
#endif /* UNIX */

#endif	/* _PROC_H */







