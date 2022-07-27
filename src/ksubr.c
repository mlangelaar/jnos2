
/*
 * Machine or compiler-dependent portions of kernel
 * Turbo-C version for PC
 * Copyright 1991 Phil Karn, KA9Q
 *
 * 19Sep2006, Maiko Langelaar (VE4KLM), Time to clean up this module for
 * post JNOS 2.0e era. The setjmp/longjmp functions are becoming increasely
 * more difficult to use. The newer glibc mangles the pointers, so I think
 * it's time to switch over to using the ucontext functions instead.
 *
 * Filename : ksubr.glibc2.4+.c - LINUX specific !!!
 *
 */

#include "global.h"

#include "proc.h"

void
kinit()
{
}

/* 30Nov2005, Maiko, New function to cut down on duplicate code */

static void pproc (struct proc *pp)
{
	ucontext_t currenv;

	getcontext (&currenv);	/* 19Sep2006, Maiko, New way to get info */

	/* 14Nov2009, Maiko, Do proper casting, Arch era gcc complains */

	currenv.uc_stack.ss_sp = (void*)pp->stack,
	currenv.uc_stack.ss_size = (size_t)pp->stksize,

	tprintf ("%8.8lx %-8lx %-8lx %-8x %8.8lx %c%c%c %3d %3d  %s\n",
		(unsigned long)(pp),
		(unsigned long)(currenv.uc_stack.ss_sp),
		(unsigned long)(currenv.uc_stack.ss_size),
		stkutil (pp), (unsigned long)(pp->event),
		pp->i_state ? 'I' : ' ',
		(pp->state & WAITING) ? 'W' : ' ',
		(pp->state & SUSPEND) ? 'S' : ' ',
		pp->input,
		pp->output,
		pp->name);
}

#ifdef	DEBUG_STACK

/*
 * 12Oct2009, Maiko, Try this new function to help me find potential
 * stack overflow situations. Call it from within any process, put it
 * in a mail handling loop, see how high the stack usage goes up while
 * a process is in the middle of handling anything. Written originally
 * to help me figure out why SMTP is crashing my JNOS lately.
 */
void pscurproc ()
{
	ucontext_t currenv;

	getcontext (&currenv);	/* 19Sep2006, Maiko, New way to get info */

	log (-1, "%s Stack %x %x", Curproc->name,
		currenv.uc_stack.ss_size = Curproc->stksize,
			stkutil (Curproc));
}

#endif

/* Print process table info
 * Since things can change while ps is running, the ready proceses are
 * displayed last. This is because an interrupt can make a process ready,
 * but a ready process won't spontaneously become unready. Therefore a
 * process that changes during ps may show up twice, but this is better
 * than not having it showing up at all.
 */
int
ps(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct proc *pp;
    int i;
  
    tprintf("Uptime %s",tformat(secclock()));

    j2tputs("\n");

    j2tputs ("PID      SP       maxstk   stksize  event    fl  in  out  name\n");
  
    for (pp = Susptab; pp != NULLPROC; pp = pp->next)
		pproc (pp);

    for (i = 0; i < PHASH; i++)
        for (pp = Waittab[i]; pp != NULLPROC; pp = pp->next)
			pproc (pp);

    for (pp = Rdytab; pp != NULLPROC; pp = pp->next)
		pproc (pp);

    if (Curproc != NULLPROC)
		pproc (Curproc);

    return 0;
}

unsigned int
stkutil(pp)
struct proc *pp;
{
    unsigned i;
    register int16 *sp;
  
    i = pp->stksize;

    if ((unsigned long)(pp->stack) == MAINSTKBASE)
        return i; /* can't check system stack, dynamic */

    for(sp = pp->stack;*sp == STACKPAT && sp < pp->stack + pp->stksize;sp++)
        i--;

    return i;
}

/*
 * Run the specified function, then autodarwinate.
 * ucontext actually does the "right" thing without this, except that it
 * won't garbage-collect JNOS's process table....
 */

static void
_kicker(func, iarg, parg1, parg2)
void (*func) __ARGS((int, void *, void *));
int iarg;
void *parg1;
void *parg2;
{
    (*func)(iarg, parg1, parg2);
    killself();
}

/* Machine-dependent initialization of a task */
void
psetup(pp,iarg,parg1,parg2,pc)
struct proc *pp;        /* Pointer to task structure */
int iarg;               /* Generic integer arg */
void *parg1;            /* Generic pointer arg #1 */
void *parg2;            /* Generic pointer arg #2 */
void (*pc) __ARGS((int,void *,void *));           /* Initial execution address */
{
	/*
	 * The ucontext stuff does just about everything we need except call
	 * killself().  Since we'd kind of like the JNOS process table to get
	 * cleaned up, we simulate this by means of a wrapper function.
	 */
	getcontext(&pp->env);

	/* 19Sep2006, Maiko, These 2 lines are very important !!! */
	pp->env.uc_stack.ss_sp = pp->stack;
	pp->env.uc_stack.ss_size = pp->stksize;

	makecontext(&pp->env, _kicker, 4, pc, iarg, parg1, parg2);

	/* Task initially runs with interrupts on */
	pp->i_state = 1;
}

