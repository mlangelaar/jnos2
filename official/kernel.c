
/*
 * Non pre-empting synchronization kernel, machine-independent portion
 * Copyright 1991 Phil Karn, KA9Q
 *
 * 19Sep2006, Maiko Langelaar (VE4KLM), Time to clean up this module for
 * post JNOS 2.0e era. The setjmp/longjmp functions are becoming increasely
 * more difficult to use. The newer glibc mangles the pointers, so I think
 * it's time to switch over to using the ucontext functions instead.
 *
 * Filename : kernel.glibc2.4+.c - LINUX specific !!!
 *
 */

#include "global.h"

#include "mbuf.h"
#include "proc.h"
#include "timer.h"
#include "socket.h"
#include "unix.h"
  
struct proc *Curproc;       /* Currently running process */
struct proc *Rdytab;        /* Processes ready to run (not including curproc) */
struct proc *Waittab[PHASH];    /* Waiting process list */
struct proc *Susptab;       /* Suspended processes */
static struct mbuf *Killq;

static void addproc __ARGS((struct proc *entry));
static void delproc __ARGS((struct proc *entry));
  
/* Create a process descriptor for the main function. Must be actually
 * called from the main function!
 * Note that standard I/O is NOT set up here.
 */
struct proc *
mainproc(name)
char *name;
{
    register struct proc *pp;
  
    /* Create process descriptor */
    pp = (struct proc *)callocw(1,sizeof(struct proc));
  
    /* Create name */
    pp->name = j2strdup(name);
    pp->stksize = MAINSTKBASE;
    pp->stack = (void *) MAINSTKBASE;

    /* Make current */
    pp->state = READY;
    Curproc = pp;
  
    return pp;
}

/* Create a new, ready process and return pointer to descriptor.
 * The general registers are not initialized, but optional args are pushed
 * on the stack so they can be seen by a C function.
 */
struct proc *
newproc(name,stksize,pc,iarg,parg1,parg2,freeargs)
char *name;     /* Arbitrary user-assigned name string */
unsigned int stksize;   /* Stack size in words to allocate */
void (*pc)__ARGS((int,void*,void*));    /* Initial execution address */
int iarg;       /* Integer argument (argc) */
void *parg1;        /* Generic pointer argument #1 (argv) */
void *parg2;        /* Generic pointer argument #2 (session ptr) */
int freeargs;       /* If set, free arg list on parg1 at termination */
{
    register struct proc *pp;
    unsigned int i;
  
#ifdef CHKSTK
    chkstk();
#endif

    /* Create process descriptor */
    pp = (struct proc *)callocw(1,sizeof(struct proc));
  
    /* Create name */
    pp->name = j2strdup(name);
  
    /* Allocate stack - account for CURSES overhead */

#ifndef CURSES_OVERHEAD
#define CURSES_OVERHEAD 8192	/* This number is NOT unrealistic */
#endif

    stksize += CURSES_OVERHEAD;     /* curses (rflush()) overhead */

    pp->stksize = stksize;

	/* 22Dec2005, Maiko, Changed malloc() to mallocw() instead ! */
    if((pp->stack = (int16 *)mallocw(sizeof(int16)*stksize)) == NULL)
	{
        free(pp->name);
        free((char *)pp);
        return NULLPROC;
    }

    /* log (-1, "newproc %s %x", pp->name, (int)(pp->stack)); */
  
    /* Initialize stack for high-water check */
    for(i=0;i<stksize;i++)
        pp->stack[i] = STACKPAT;
  
    /* Do machine-dependent initialization of stack */
    psetup(pp,iarg,parg1,parg2,pc);
  
    pp->freeargs = freeargs;
    pp->iarg = iarg;
    pp->parg1 = parg1;
    pp->parg2 = parg2;
  
    /* Inherit creator's input and output sockets */
    usesock(Curproc->input);
    pp->input = Curproc->input;
    usesock(Curproc->output);
    pp->output = Curproc->output;
  
	/*
	 * The old "curses" tty driver faked this, occasionally getting it
	 * not quite right (IMHO, but the DOS version did the same implicitly).
	 * The new one uses this pointer to get it "right".
	 *
	 * The session manager uses this because multiple sessions (potentially
	 * all of them!) can be simultaneously "current" (e.g. "xterm" session
	 * manager).  This is even more important with external sessions, which
	 * are *always* "current".
         *
	 * 1.11x6: now part of all versions, since dns_query can use it.
	 */
	pp->session = Curproc->session;

    /* Add to ready process table */
    pp->state = READY;
    addproc(pp);
    return pp;
}
  
/* Free resources allocated to specified process. If a process wants to kill
 * itself, the reaper is called to do the dirty work. This avoids some
 * messy situations that would otherwise occur, like freeing your own stack.
 */
void
killproc(pp)
register struct proc *pp;
{
    char **argv;
  
    if(pp == NULLPROC)
        return;
    /* Don't check the stack here! Will cause infinite recursion if
     * called from a stack error
     */
  
    if(pp == Curproc)
        killself(); /* Doesn't return */
  
    /* Close any open sockets */
    freesock(pp);
  
    close_s(pp->input);
    close_s(pp->output);
  
    /* Stop alarm clock in case it's running */
    stop_timer(&pp->alarm);
  
    /* Alert everyone waiting for this proc to die */
    j2psignal(pp,0);
  
    /* Remove from appropriate table */
    delproc(pp);
  
    /* Free allocated memory resources */
    if(pp->freeargs){
        argv = pp->parg1;
        while(pp->iarg-- != 0)
            free(*argv++);
        free(pp->parg1);
    }

	/* log (-1, "killproc %s %x", pp->name, (int)(pp->stack)); */

    free(pp->name);
    free(pp->stack);
    free(pp->outbuf);
    free((char *)pp);
}
/* Terminate current process by sending a request to the killer process.
 * Automatically called when a process function returns. Does not return.
 */
void
killself()
{
    register struct mbuf *bp;
  
    if(Curproc != NULLPROC){
        bp = pushdown(NULLBUF,sizeof(Curproc));
        memcpy(bp->data,(char *)&Curproc,sizeof(Curproc));
        enqueue(&Killq,bp);
    }
    /* "Wait for me; I will be merciful and quick." */
    for(;;)
        pwait(NULL);
}
/* Process used by processes that want to kill themselves */
void
killer(i,v1,v2)
int i;
void *v1;
void *v2;
{
    struct proc *pp;
    struct mbuf *bp;
  
    for(;;){
        while(Killq == NULLBUF)
            pwait(&Killq);
        bp = dequeue(&Killq);
        pullup(&bp,(char *)&pp,sizeof(pp));
        free_p(bp);
        if(pp != Curproc)   /* We're immortal */
            killproc(pp);
    }
}
  
/* Inhibit a process from running */
void
suspend(pp)
struct proc *pp;
{
    if(pp == NULLPROC)
        return;
    if(pp != Curproc)
        delproc(pp);    /* Running process isn't on any list */
    pp->state |= SUSPEND;
    if(pp != Curproc)
        addproc(pp);    /* pwait will do it for us */
    else
        pwait(NULL);
}

/* Restart suspended process */
void
resume(pp)
struct proc *pp;
{
    if(pp == NULLPROC)
        return;
    delproc(pp);    /* Can't be Curproc! */
    pp->state &= ~SUSPEND;
    addproc(pp);
}
  
/* Wakeup waiting process, regardless of event it's waiting for. The process
 * will see a return value of "val" from its pwait() call.
 */
void
alert(pp,val)
struct proc *pp;
int val;
{
    if(pp == NULLPROC)
        return;
    if(pp != Curproc)
        delproc(pp);
    pp->state &= ~WAITING;
    pp->retval = val;
    pp->event = 0;
    if(pp != Curproc)
        addproc(pp);
}
  
/* Post a wait on a specified event and give up the CPU until it happens. The
 * null event is special: it means "I don't want to block on an event, but let
 * somebody else run for a while". It can also mean that the present process
 * is terminating; in this case the wait never returns.
 *
 * Pwait() returns 0 if the event was signaled; otherwise it returns the
 * arg in an alert() call. Pwait must not be called from interrupt level.
 *
 * Note that pwait can run with interrupts enabled even though it examines
 * a few global variables that can be modified by j2psignal at interrupt time.
 * These *seem* safe.
 */
int
pwait(event)
volatile void *event;
{
    register struct proc *oldproc;
    int tmp;
  
    if(Curproc != NULLPROC)	/* If process isn't terminating */
	{
#ifdef CHKSTK
    chkstk();
#endif
        if(event == NULL){
            /* Special case; just give up the processor.
             *
             * Optimization: if nothing else is ready, just return.
             */
            if(Rdytab == NULLPROC){
                return 0;
            }
        } else {
            /* Post a wait for the specified event */
            Curproc->event = event;
            Curproc->state = WAITING;
        }
        addproc(Curproc);
    }
    /* Look for a ready process and run it. If there are none,
     * loop or halt until an interrupt makes something ready.
     */
    while(Rdytab == NULLPROC)
	{
        /* Give system back to upper-level multitasker, if any.
         * Note that this function enables interrupts internally
         * to prevent deadlock, but it restores our state
         * before returning.
         */
        giveup();
    }
    /* Remove first entry from ready list */
    oldproc = Curproc;
    Curproc = Rdytab;
    delproc(Curproc);
  
    /* Now do the context switch.
     * This technique was inspired by Rob, PE1CHL, and is a bit tricky.
     *
     * If the old process has gone away, simply load the new process's
     * environment. Otherwise, save the current process's state. Then if
     * this is still the old process, load the new environment. Since the
     * new task will "think" it's returning from the setjmp() with a return
     * value of 1, the comparison with 0 will bypass the longjmp(), which
     * would otherwise cause an infinite loop.
     */

    /* i_state isn't needed --- the signal mask is part of the context */

    if (oldproc == NULLPROC)
	setcontext(&Curproc->env);
    else
	swapcontext(&oldproc->env, &Curproc->env);

    /* At this point, we're running in the newly dispatched task */
    tmp = Curproc->retval;
    Curproc->retval = 0;
  
    /* Also restore the true interrupt state here, in case the longjmp
     * DOES restore the interrupt state saved at the time of the setjmp().
     * This is the case with Turbo-C's setjmp/longjmp.
     */
    restore(Curproc->i_state);
    return tmp;
}

/* 19Sep2006, Maiko, Moved out of ksubr.c, since only used here */
unsigned phash(event)
volatile void *event;
{
    register unsigned x;

	long x_l = (long)event;	/* 01Oct2009, Maiko, Bridge for 64 bit warning */

    /* Fold the two halves of the pointer */
    x = (unsigned)x_l;
  
    /* If PHASH is a power of two, this will simply mask off the
     * higher order bits
     */
    return x % PHASH;
}

/* Make ready the first 'n' processes waiting for a given event. The ready
 * processes will see a return value of 0 from pwait().  Note that they don't
 * actually get control until we explicitly give up the CPU ourselves through
 * a pwait(). Psignal may be called from interrupt level. It returns the
 * number of processes that were woken up.
 *
 * 20May2009, Maiko (VE4KLM), renamed to j2psignal() to avoid O/S clash !
 */
int
j2psignal(event,n)
volatile void *event;    /* Event to signal */
int n;      /* Max number of processes to wake up */
{
    register struct proc *pp;
    struct proc *pnext;
    int i_state;
    unsigned int hashval;
    int cnt = 0;

#ifdef CHKSTK
    chkstk();
#endif
 
    if(event == NULL)
        return 0;       /* Null events are invalid */
  
    /* n = 0 means "signal everybody waiting for this event" */
    if(n == 0)
        n = 65535U;
  
    hashval = phash(event);
    i_state = dirps();
    for(pp = Waittab[hashval];n != 0 && pp != NULLPROC;pp = pnext)
	{
        pnext = pp->next;
        if(pp->event == event)
	{
            delproc(pp);
            pp->state &= ~WAITING;
            pp->retval = 0;
            pp->event = NULL;
            addproc(pp);
            n--;
            cnt++;
        }
    }

    for(pp = Susptab;n != 0 && pp != NULLPROC;pp = pnext)
	{
        pnext = pp->next;
        if(pp->event == event)
	{
            delproc(pp);
            pp->state &= ~WAITING;
            pp->event = 0;
            pp->retval = 0;
            addproc(pp);
            n--;
            cnt++;
        }
    }

    restore(i_state);

    return cnt;
}
  
/* Rename a process */
void
chname(pp,newname)
struct proc *pp;
char *newname;
{
    free(pp->name);
    pp->name = j2strdup(newname);
}

/* Remove a process entry from the appropriate table */
static void
delproc(entry)
register struct proc *entry;    /* Pointer to entry */
{
    int i_state;
  
    if(entry == NULLPROC)
        return;
  
    i_state = dirps();
    if(entry->next != NULLPROC)
        entry->next->prev = entry->prev;
    if(entry->prev != NULLPROC){
        entry->prev->next = entry->next;
    }
	else
	{
        switch(entry->state)
	{
            case READY:
                Rdytab = entry->next;
                break;
            case WAITING:
                Waittab[phash(entry->event)] = entry->next;
                break;
            case SUSPEND:
            case SUSPEND|WAITING:
                Susptab = entry->next;
                break;
        }
    }
    restore(i_state);
}

/* Append proc entry to end of appropriate list */
static void
addproc(entry)
register struct proc *entry;    /* Pointer to entry */
{
    register struct proc *pp;
    struct proc **head;
    int i_state;
  
    if(entry == NULLPROC)
        return;

    switch(entry->state)
	{
        case READY:
            head = &Rdytab;
            break;
        case WAITING:
            head = &Waittab[phash(entry->event)];
            break;
        case SUSPEND:
        case SUSPEND|WAITING:
            head = &Susptab;
            break;
    }
    entry->next = NULLPROC;
    i_state = dirps();
    if(*head == NULLPROC)
	{
        /* Empty list, stick at beginning */
        entry->prev = NULLPROC;
        *head = entry;
    }
	else
	{
        /* Find last entry on list */
        for(pp = *head;pp->next != NULLPROC;pp = pp->next);
        pp->next = entry;
        entry->prev = pp;
    }
    restore(i_state);
}

