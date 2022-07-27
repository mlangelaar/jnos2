#ifndef	_J2CALLLISTS_H_
#define _J2CALLLISTS_H_

/*
 * 19Jun2020, Maiko (VE4KLM), finally decided to move my callsigns lists
 * functions into their own source file. These functions were exclusively
 * written for just the APRS code roughly 18 years ago ? but I have found
 * them to be very useful in other areas of the JNOS package.
 *
 * This is a new header file for the function prototypes.
 *
 * The new source file is j2calllists.c
 *
 */

/* the following are specifically used in the source file, aprssrv.c */

int callsign_ignored (char*);
int callsign_allowed (char*);
int callsign_banned (char*);
int callsign_can_stat (char*);
int callsign_can_pos (char*);
int callsign_can_wx (char*);
int callsign_can_obj (char*);
int callsign_can_mice (char*);

/* the following is specifically used in the source file, aprsmsg.c */

int ip_allow_45845 (char*);

/* 21Nov2020, Maiko, the following is specifically used in the source file, mailbox.c */

int ip_exclude_MFA (char *ipaddr);

#ifdef	WINLINK_SECURE_LOGIN
/* 03Dec2020, Maiko, MULTIPLE_WL2K_CALLS removed from makefile, changed to WINLINK_SECURE_LOGIN */

/* the following is specifically used in the source file, forward.c */

int wl2kmulticalls (char* (*) (int, char*, char*), int, char*);

char *wl2kmycall (void);

#endif

/* the following used in mboxcmd.c */

int docallslist (int, char**, void *);

#endif	/* end of _J2CALLLISTS_H_ */
