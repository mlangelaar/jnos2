/* These routines, plus the assembler hooks in stopwatch.asm, implement a
 * general purpose "stopwatch" facility useful for timing the execution of
 * code segments. The PC's "timer 2" channel (the one ordinarily
 * used to drive the speaker) is used. It is driven with a 838 ns
 * clock. The timer is 16 bits wide, so it "wraps around" in only 55 ms.
 *
 * There is an array of "stopwatch" structures used for recording the number
 * of uses and the min/max times for each. Since only one hardware timer is
 * available, only one stopwatch can actually be running at any one time.
 *
 * This facility is useful mainly for timing routines that must execute with
 * interrupts disabled. An interrupt that occurs while the timer is running
 * will not stop it, so it would show an errneously large value.
 *
 * To start a timer, call swstart(). To stop it and record its value,
 * call swstop(n), where n is the number of the stopwatch structure.
 * The stopwatch structures can be displayed with the "watch" command.
 *
 * Copyright 1991 Phil Karn, KA9Q
 */
/* Mods by PA0GRI */
  
#include "global.h"
#ifdef SWATCH
#include "cmdparse.h"
#include "pc.h"
  
struct stopwatch Sw[NSW];
char Swflag = 1;            /* default is allow stop watch functions */
  
/* Stop a stopwatch and record its value.
 * Uses stopval() routine in stopwatch.asm
 */
void
swstop(n)
int n;
{
    register int16 cnt;
    register struct stopwatch *sw;
  
    if((int)!Swflag)
        return;         /* no stop watch functions */
  
    cnt = stopval();
    sw = &Sw[n];
  
    if(sw->calls++ == 0){
        sw->maxval = cnt;
        sw->minval = cnt;
    } else if(cnt > sw->maxval){
        sw->maxval = cnt;
    } else if(cnt < sw->minval){
        sw->minval = cnt;
    }
}
int
doswatch(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct stopwatch *sw;
    long maxval,minval;
    int i, ret, flag;
  
    if(argc > 1){
        /* Clear timers */
        for(i=0,sw=Sw;i < NSW;i++,sw++){
            sw->calls = 0;
        }
    }
    flag = (int)Swflag;
    ret = setbool(&flag,"Stop Watch flag",argc,argv);
    Swflag = (char)flag;
  
    if(ret != 0 || argc > 1)
        return ret;
  
    for(i=0,sw=Sw;sw < &Sw[NSW];i++,sw++){
        if(sw->calls != 0){
            minval = (65536L - sw->maxval) * 838 / 1000;
            maxval = (65536L - sw->minval) * 838 / 1000;
            tprintf("%d: calls %ld min %ld max %ld\n",
            i,sw->calls,minval,maxval);
        }
    }
    return 0;
}
#endif /* SWATCH */
  
