
#include "global.h"

#ifdef CHKSTK

#include "proc.h"

/* Verify that stack pointer for current process is within legal limits;
 * also check that no one has dereferenced a null pointer
 */
#ifdef UNIX
static void __chkstk_internal (int16 spp)
{
    int16 *sp, *sbase, *stop;
  
    sp = &spp;      /* close enough for government work */
    sbase = Curproc->stack;
    if(sbase == NULL || (unsigned long)sbase == MAINSTKBASE)
        return; /* Main task -- too hard to check */
    stop = sbase + Curproc->stksize;
    if(sp < sbase || sp >= stop){
        printf("Stack violation, process %s\n",Curproc->name);
        printf("SP = %lx, legal stack range [%lx,%lx)\n",
        ptol(sp),ptol(sbase),ptol(stop));
        fflush(stdout);
        rflush();
        killself();
    }
}
#endif
  
void chkstk()
{
#ifdef UNIX
    __chkstk_internal(0);   /* must have an argument to take address of */
#else
    int16 *sbase;
    int16 *stop;
    int16 *sp;
#ifdef MULTITASK
    extern int Nokeys;              /* indicates we are shelled out [pc.c]*/
#endif
  
    sp = MK_FP(_SS,_SP);
    if(_SS == _DS){
        /* Probably in interrupt context */
        return;
    }
    sbase = Curproc->stack;
    if(sbase == NULL)
        return; /* Main task -- too hard to check */
  
    stop = sbase + Curproc->stksize;
    if(sp < sbase || sp >= stop){
        printf("Stack violation, process %s\n",Curproc->name);
        printf("SP = %lx, legal stack range [%lx,%lx)\n",
        ptol(sp),ptol(sbase),ptol(stop));
        fflush(stdout);
        killself();
    }
    if(*(unsigned short *)NULL != oldNull){
#ifdef MULTITASK
        if(!Nokeys)     /* don't complain if we are shelled out */
#endif
            printf("WARNING: Location 0 smashed, process %s\n",Curproc->name);
        *(unsigned short *)NULL = oldNull;
        fflush(stdout);
    }
#endif /* UNIX */
}
#endif /* CHKSTK */
