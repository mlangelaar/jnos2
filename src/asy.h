#ifndef _ASY_H
#define _ASY_H
  
#ifndef _GLOBAL_H
#include "global.h"
#endif
  
#ifndef _MBUF_H
#include "mbuf.h"
#endif
  
#ifndef _IFACE_H
#include "iface.h"
#endif
  
#ifdef UNIX
#define ASY_MAX 9               /* Nine asynch ports allowed on UNIX PCs */
#else
#define ASY_MAX 6               /* Six asynch ports allowed on the PC */
#endif
  
#define SLIP_MODE       0
#define AX25_MODE       1
#define NRS_MODE        2
#define UNKNOWN_MODE    3
#define PPP_MODE        4
  
/* In i8250.c: */
int asy_init __ARGS((int dev,struct iface *ifp,char *arg1,char *arg2,
    int16 bufsize,int trigchar,char monitor,long speed,int force,int triglevel,
    int polled));
int32 asy_ioctl __ARGS((struct iface *ifp,int cmd,int set,int32 val));
int asy_speed __ARGS((int dev,long bps));
int asy_send __ARGS((int dev,struct mbuf *bp));
  
int asy_stop __ARGS((struct iface *ifp));
int get_rlsd_asy __ARGS((int dev, int new_rlsd));
int get_asy __ARGS((int dev));
void asy_sendbreak __ARGS((int dev));
  
/* In asyvec.asm: */
INTERRUPT asy0vec __ARGS((void));
INTERRUPT asy1vec __ARGS((void));
INTERRUPT asy2vec __ARGS((void));
INTERRUPT asy3vec __ARGS((void));
INTERRUPT asy4vec __ARGS((void));
INTERRUPT asy5vec __ARGS((void));
  
#endif  /* _ASY_H */
