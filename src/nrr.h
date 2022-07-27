#ifndef _NRR_H_
#define _NRR_H_

#if defined (NETROM) && defined (NRR)

#include "mbuf.h"
#include "netrom.h"

/* File : NRR.C */

extern int nrr_frame (unsigned char *);
extern int nrr_proc (struct nr3hdr*, struct mbuf**);

#endif

#endif
