#ifndef	_APRS_H_
#define	_APRS_H_

/*
 * If you are compiling this source for JNOS (111f), then make
 * sure you define '-DJNOSAPRS' somewhere in your makefile, before
 * you actually compile the code. 07Sep2001, Maiko (VE4KLM).
 */

#ifdef	JNOSAPRS
#ifndef OPTIONAL
#define OPTIONAL
#endif
#endif

#define	DEVELOPMENT	/* 20May2009, Maiko, Removed from makefile */

/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-January,2006 - Release (C-)2.0a
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 */
 
#include "udp.h"

#include "ax25.h"	/* 29Sep2019, Maiko (VE4KLM), struct breakouts */

#ifdef	APRSD

/*
 * APRS_DIGI - defining this enables the WIDE and WIDEn-N digipeating
 *             feature of NOSaprs. When compiled in, this feature can
 *             still be disabled using the 'aprs flags -digi' command
 *             from the NOS command prompt, or autoexec.nos file.
 */
#define	APRS_DIGI	/* experimental !!! */

/*
 * APRS_14501 - if you want TNOSaprs to service HTML 14501 pages, then
 *              make sure this one is defined. If you do not want this,
 *              or more importantly, if your system configuration is such
 *              that port 14501 is not accessible from the internet side,
 *              there is no point compiling this stuff in.
 */
#ifndef	APRSC
#define	APRS_14501
#endif

/*
 * APRS_44825 - if you want TNOSaprs to service Client connections on
 *              port 14825, then make sure this one is defined. If you
 *              do not want this, or more importantly, if your system
 *              configuration is such that port 14825 is not accessible
 *              from the internet side, there is no point compiling
 *              this stuff in.
 */
#ifndef	APRSC
#define APRS_44825
#endif

/*
 * APRS_MIC_E - if you want TNOSaprs to recognize MIC-E packets, then
 *              make sure this one is defined. If you have no MIC-E
 *              systems in your area, then leave it undefined.
 *
 * 17Sep2005, Maiko, gate MIC-E frames from APRS IS to RF (for Janusz)
 */
#ifndef	APRSC
#define	APRS_MIC_E
#define	GATE_MICE_RF
#endif

/*
 * APRS_MSG_C - if you want to make the APRS Message Center
 *              available to your users then make sure this
 *              one is defined.
 */
#define	APRS_MSG_C

/* Software Version (Banner) Strings */
/* 07Jun2003 - no more distinguishing between TNOS and JNOS,
 * this is now just plain old NOSaprs, since it has the same
 * functionality regardless of which NOS is in use.
 *
 * 24Feb2005 - Maiko, Make in line with JNOS 2.0 !!! 
 *
 */
#ifdef	APRSC
#define	TASVER "NOSaprs C-2.0h"
#else
#define	TASVER "NOSaprs 2.0h"
#endif

#ifdef	JNOSAPRS
/* JNOS uses pwait for process control */
#define	KLMWAIT pwait 
#else
/* TNOS uses kwait for process control */
#define	KLMWAIT kwait
#endif

/*
 * This is the APRS destination call we use for NOSaprs
 *
 * 14Jun2021, Maiko (VE4KLM), Bob Bruninga (WB4APR) was kind enough to
 * add NOSaprs in his 'tocalls' listing, assigning us the call APN2xx,
 * and also noting the call APZ000 is used by older JNOS systems.
 *
 * Thank you Janusz HF1L (ex.SP1LOP) for suggesting this
 *
#define	APRSDC "APZ200"
 *
 */
#define	APRSDC "APN20H"	/* to match the current version of NOSaprs */

/* 02Apr2002, New option to exclude DTI from aprs heard table (now default) */
extern int dtiheard;

/* 28Nov2001, common externals */
extern int aprs_debug;

/* 03Apr2002, Now pointers to alloc'ed strings */
extern char *aprs_port, *logon_callsign;
/* 10Apr2002, New locator (URL) for heard callsigns */
extern char *locator;

#ifdef	DONT_COMPILE
/* 19Jun2020, Maiko (VE4KLM), moved to the new j2KLMlists.h header file */
/* 14Jun2004, Maiko, New ip access restriction for 45845 service */
extern int ip_allow_45845 (char *ipaddr);
#endif

extern int aprsd_frame (struct udp *udp);
extern void aprsd_torf (struct iface *iface, struct mbuf *bp, struct ip *ip);

/* 16Aug2001, Maiko, Added 'iface' parameter */
extern int aprs_processing (struct ax25 *hdr,
	struct iface *iface, struct mbuf *bp);

extern int doaprsstat (int argc, char *argv[], void *p OPTIONAL);
extern void init_aprshrd (int);
extern void display_aprshrd (void);
extern int build_14501_aprshrd (char *dptr);

/* 17Oct2001, Maiko, New function to determine which interface APRS packet
 * was heard on
 */
extern const char *iface_hrd_on (char *callsign);

/* 16Aug2001, Maiko, Changed parameters so we have more info for stats */
extern void update_aprshrd (struct ax25 *hdr, struct iface *iface, char dti);

extern int mice_dti (unsigned char); /* 17Sep2005, Maiko, New function */
extern int aprs_mice (unsigned char*, unsigned char*, unsigned char*);
extern int get_topcnt (void);	/* 03Aug2001, for malloc of 14501 page */

extern void aprslog (int s, const char *fmt,...);

/* 20Nov2001, VE4KLM, New dup function for duplicate APRS frames */
/* 14Nov2002, Maiko, Added 'age_seconds' argument to function */
extern int aprs_dup (char *callsign, char *payload, int age_seconds);

#ifdef	DEVELOPMENT
/* 27Nov2001, Maiko, Duplicate management commands */
extern int doaprsdup (int argc, char *argv[], void *p OPTIONAL);
#endif

/* 27Nov2001, Maiko, Moved broadcast functions into their own module */
extern int doaprsbc (int argc, char *argv[], void *p OPTIONAL);

/*
 * 28Nov2001, Maiko, This function now in aprsbc.o module.
 * 23Jan2002, Maiko, Split the old 'startigateident' function
 * into 2 new functions, one for Inet side, one for RF side,
 * also put in functions to allow us to stop the timers.
 *
 * extern void startigateident (void);
 */
extern void startinetbc (void);
extern void stopinetbc (void);
extern void startrfbc (void);
extern void stoprfbc (void);

/* 01Jul2004, Maiko, New Wx functions */

extern void startinetwx (void);
extern void stopinetwx (void);

extern int doaprswx (int argc, char *argv[], void *p OPTIONAL);

/* 18May2005, New APRS cross port digipeating configuration */
extern int doxdigi (int argc, char *argv[], void *p OPTIONAL);

/* 28Nov2001, Maiko, Some new functions to access
 * static variables in the aprssrv module. Needed
 * for new aprsbc module. Need better way to do it,
 * but it will work for now.
 */
extern int getAsocket (void);
extern int getHsocket (void);
extern const char *getaprsdestcall (void);
extern char *getlogoncall (void);
extern void update_outstats (int length);

/* 28Nov2001, Maiko, An external, now needed in aprsbc module */
extern int sendrf (char *data, int len);

/* 09Jan2002, Maiko, DTI validation function */
extern int valid_dti (char dti);

/* 11Jan2002, Maiko, More DTI data validation */
/* 27May2005, Maiko, No more LEN parameter, 'data' is a string */
extern int valid_dti_data (char dti, char *data);

extern int aprs_send (char *data, int len);

/* 18May2004, Maiko, no longer static to aprs.c module */
extern char *format_POE_call (char *ptr);

/* 14Nov2002, Maiko, New aprsdigi module */
extern int aprs_digi (struct ax25 *hdr, char *idest, int *nomark);

/*
 * 27May2005, Maiko, Catch up time with my prototyping !
 */

/* File : aprsmsgdb.c */
extern void msgdb_save (char *from, char *to, char *data);
extern int msgdb_get_last_idx (void);

/* File: aprssrv.c */
extern void send45845msg (int urfmsg, char *from, char *rcpt, char *data);

#endif

#endif
