#ifndef _XMS_H
#define _XMS_H
/*
 *  XMS Driver C Interface Routine Definitions
 */
  
struct  XMS_Move {
    unsigned long Length;
    unsigned int SourceHandle;
    unsigned long SourceOffset;
    unsigned int DestHandle;
    unsigned long DestOffset;
};
  
extern int XMS_Available;

/* in xms.asm */  
unsigned Installed_XMS(void);	/* 1 if driver installed, 0 otherwise */
int Request_HMA(void);		/* result is 1 if successful */
int Release_HMA(void);		/* result is 1 if successful */
long Request_UMB(unsigned int npara); /* result is npara*2^16+UMBseg; UMBseg=0 if err */
long Release_UMB(unsigned int UMBseg); /* result is 0 if successful */
long Query_XMS(void);		/* get total free extd mem, in KB */
long Total_XMS(void);		/* get largest free extd mem block, in KB */
long Alloc_XMS(unsigned int sizeinKB);	/* handle in LSW */
long Free_XMS(unsigned int handle);	/* result is 0 if successful */
long Move_XMS(struct XMS_Move *);	/* result is 0 if successful */
long Query_XMS_handle(unsigned int handle);  /* result is blklenKB*2^16+Locks*2^8+HandlesFree */

/* in pc.c */
void Free_Screen_XMS(void);

/* in xmsutil.c */
void far *mallocUMB (unsigned);
void far *callocUMB (unsigned);
unsigned int largestUMB(void);   /* in paragraphs */
  
#define XMS_PAGE_SIZE       1024

/*  Define the XMS error codes */
#define XMSErrOK            0x00    /* No Error */
#define XMSErrUnimp         0x80    /* Unimplemented function */
#define XMSErrVDISK         0x81    /* VDISK device detected */
#define XMSErrA20           0x82    /* A20 error */
#define XMSErrNoHMA         0x90    /* HMA does not exist */
#define XMSErrHMAInUse      0x91    /* HMA already in use */
#define XMSErrHMAMin        0x92    /* HMA space req. < /HMAMIN= parameter */
#define XMSErrHMANotAll     0x93    /* HMA not allocated */
#define XMSErrA20Enab       0x94    /* A20 still enabled */
#define XMSErrNoXMLeft      0xA0   /* All XM allocated */
#define XMSErrNoHandles     0xA1   /* All handles are allocated */
#define XMSErrHandInv       0xA2   /* Invalid handle */
#define XMSErrSHandInv      0xA3   /* Invalid Source Handle */
#define XMSErrSOffInv       0xA4   /* Invalid Source Offset */
#define XMSErrDHandInv      0xA5   /* Invalid Dest Handle */
#define XMSErrDOffInv       0xA6   /* Invalid Dest Offset */
#define XMSErrLenInv        0xA7   /* Invalid Length */
#define XMSErrOverlap       0xA8   /* Invalid move overlap */
#define XMSErrParity        0xA9   /* Parity error */
#define XMSErrNoLock        0xAA   /* Handle not locked */
#define XMSErrLock          0xAB   /* Handle Locked */
#define XMSErrLockOvflo     0xAC   /* Lock count overflo */
#define XMSErrLockFail      0xAD   /* Lock fail */
#define XMSErrSmUMB         0xB0   /* Smaller UMB available */
#define XMSErrNoUMB         0xB1   /* No UMB's available */
#define XMSErrUMBInv        0xB2   /* Invalid UMB segment */
#endif
