/*
   Extensions to the basic XMS library routines.
   Written by James Dugal, N5KNX, 5/95.
 */

#include "global.h"

#ifdef XMS
#include "xms.h"
#include <dos.h>

void far *mallocUMB __ARGS((unsigned int));
void far *callocUMB __ARGS((unsigned int));
#define freeUMB(p)	free(p)

/* Allocate UMB memory, returning pointer, or NULL if not possible */
void far *mallocUMB (nbytes)
unsigned int nbytes;
{
    int npara;
    long UMBres;
    void far *p;

    if (!XMS_Available)
        return NULL;
    npara = (nbytes+15)/16;   /* in paragraphs */
    UMBres = Request_UMB(npara);
    p = MK_FP((unsigned int)UMBres&0xFFFF, 0);
    if ((int)(UMBres>>16) != npara)  /* err, or not enough paras avail */
        p = NULL;
    return p;
}

/* Allocate zeroed UMB memory, returning pointer, or NULL if not possible */
void far *callocUMB (nbytes)
unsigned int nbytes;
{
    void far *p = mallocUMB(nbytes);

    if (p) memset(p,0,nbytes);
    return p;
}

/* Determine largest UMB memory block available, returning size in paragraphs
   (or 0 if not possible) */
unsigned int
largestUMB(void)
{
    long UMBres;
    int npara = 32767;   /* large value, should fail */
    int ngot;
    unsigned char c;

    if (XMS_Available) {
        UMBres = Request_UMB(npara);
        ngot = (int)(UMBres>>16);
        if (ngot == npara)  /* oops, it succeeded !!?? */
            Release_UMB((int)UMBres&0xFFFF);
        else {
            c = (unsigned char)(UMBres>>24);
            if (XMSErrSmUMB == c)
                ngot = (int)(UMBres&0xFFFF);   /* # paras available */
            else
                ngot = 0;
       }
   }
   else
       ngot=0;
   return ngot;   /* paragraphs */
}
#endif XMS
