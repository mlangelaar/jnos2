/* Include needed for extended bbs support - WG7J */
#ifndef _MAILFOR_H
#define _MAILFOR_H
  
#ifndef _AX25_H
#include "ax25.h"
#endif
  
/*List of bbses we forward to, to check the R: lines for */
#define FWDBBSLEN 9 /* call + optional ssid, eg. "KB7BHF-15" */
#define NUMFWDBBS 8 /* Check for 8 bbs's max (arbitrary !) */
  
/* In mailfor.c */
extern char MyFwds[][FWDBBSLEN+1];
extern int Numfwds;
extern int Checklock;
extern int Rreturn;
extern int Rfwdcheck;
extern int Rdate;
extern int Rholdold;

int dombmailfor __ARGS((int argc,char *argv[],void *p));
  
#endif /*_MAILFOR_H*/
