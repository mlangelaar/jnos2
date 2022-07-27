#ifndef _GLOBAL_H
#define _GLOBAL_H
  
#ifdef __cplusplus
#define private j_private
#define public j_public
#define protected j_protected
#define class j_class
#define virtual j_virtual
#define new j_new
#define delete j_delete
#define friend j_friend
#endif

/* Global definitions used by every source file.
 * Some may be compiler dependent.
 */
  
#include <stdio.h>
#ifndef UNIX
#include <stdlib.h>
#include <string.h>
#endif

/*
 * 07Aug2010, Maiko (VE4KLM), msclock() is used to initialize
 * values for RTT and SEQ numbers, BUT the results are usually
 * placed into 2 or 4 bytes in a packet going out somewheres,
 * so right now I modulo the result of msclock() with MAXINT,
 * or MAXSHORT, or MAXLONG - should see if there is a better
 * and less CPU intensive way of doing this ... TODO LIST !
 */
#include <values.h>
  
#if     defined(__TURBOC__) || defined(__STDC__) || defined(LATTICE)
#define ANSIPROTO       1
#endif
  
#ifdef UNIX
#define O_BINARY 0
#define cdecl
#define _Cdecl
#define far
#define _FAR
#define near
#define _NEAR
#endif
  
/* Distinguish between Turbo C 2.0, and TC++ v1.0 and Borland C++ 2.0,3.0,3.1 */
#if defined(__TURBOC__) && __TURBOC__ >= 0x0295 && __TURBOC__ < 0x0300
#define __BORLANDC__ 0x0100     /* Pretend we're Borland C */
/* Turbo C++ 1.0x is confused about some structures whose addresses are
    passed as arguments.  We kludge something here to fix it...wa7tas
    We use "-stu" to ignore the Warning: Undefined structure 'xxx' messages.
*/
#pragma warn -stu
extern struct ax25_cb;
extern struct iface;
extern struct ip;
extern struct mbx;
extern struct mbuf;
extern struct nr4cb;
extern struct session;
#endif
  
/* Distinguish between Turbo C 2.0, and Borland C++ 2.0,3.0,3.1 */
#if defined(__TURBOC__) && defined(__BORLANDC__)
#define DFAR far
#else
#define DFAR
#endif
  
#ifndef __ARGS
#ifdef  ANSIPROTO
#define __ARGS(x)       x
#ifdef __GNUC__
#define __FARGS(x) x
#else
#define __FARGS(x) ()
#endif
#else
#define __ARGS(x)       ()
#define __FARGS(x) ()
#endif
#endif
  
/* To avoid some problems with the possibility of opening devices
 * that lock up nos (ie. CON etc.)
 * redefine fopen() call.
 * WG7J, 930205
 */
/* Apparently Turbo-C cannot handle this - WG7J */
#ifdef __BORLANDC__
  
FILE _FAR *_Cdecl newfopen(const char _FAR *__path, const char _FAR *__mode);
#define fopen newfopen
  
#endif /* __BORLANDC__ */
  
#if     !defined(AMIGA) && (defined(LATTICE) || defined(MAC) || defined(__TURBOC__))
/* These compilers require special open modes when reading binary files.
 *
 * "The single most brilliant design decision in all of UNIX was the
 * choice of a SINGLE character as the end-of-line indicator" -- M. O'Dell
 *
 * "Whoever picked the end-of-line conventions for MS-DOS and the Macintosh
 * should be shot!" -- P. Karn's corollary to O'Dells' declaration
 */
#define READ_BINARY     "rb"
#define WRITE_BINARY    "wb"
#define APPEND_BINARY   "ab+"
#define READ_TEXT       "rt"
#define WRITE_TEXT      "wt"
#define APPEND_TEXT     "at+"
  
#else
  
#define READ_BINARY     "r"
#define WRITE_BINARY    "w"
#define APPEND_BINARY   "a+"
#define READ_TEXT       "r"
#define WRITE_TEXT      "w"
#define APPEND_TEXT     "a+"
  
#endif

/*
 * 29Sep2009, Maiko, Making JNOS 64 bit compatible - Oh Boy, what fun !
 *
 * An 'int' is 4 bytes on both 32 and 64 bit systems, 'long' is 4 bytes on
 * a 32 bit system, and 8 bytes on a 64 bit system. So just define 'int32'
 * as an 'int', period ! No need to check for __x86_64__ GCC macro then.
 */

typedef int int32;              /* 32-bit signed integer */
typedef unsigned int uint32;    /* 32-bit unsigned integer */
typedef unsigned short int16;   /* 16-bit unsigned integer */
typedef unsigned char byte_t;   /*  8-bit unsigned integer */

/* 05Aug2010, Maiko (VE4KLM), I think we need to go 64 bit for JNOS clock */
typedef long long int int64;

#define uchar(x) ((unsigned char)(x))

#define MAXINT16 65535U         /* Largest 16-bit integer */
#ifdef __GNUC__
#define MAXINT32 4294967295UL    /* Largest 32-bit integer */
#else
#define MAXINT32 4294967295L    /* Largest 32-bit (unsigned) integer */
#endif
  
#define HASHMOD 7               /* Modulus used by hash_ip() function */
  
/* The "interrupt" keyword is non-standard, so make it configurable */
#if     defined(__TURBOC__) && defined(MSDOS)
#define INTERRUPT       void interrupt
#else
#define INTERRUPT       void
#endif
  
/* Note that these definitions are on by default if none of the Turbo-C style
 * memory model definitions are on; this avoids having to change them when
 * porting to 68K environments.
 */
#if     !defined(__TINY__) && !defined(__SMALL__) && !defined(__MEDIUM__)
#define LARGEDATA       1
#endif
  
#if     !defined(__TINY__) && !defined(__SMALL__) && !defined(__COMPACT__)
#define LARGECODE       1
#endif
  
/* Since not all compilers support structure assignment, the ASSIGN()
 * macro is used. This controls how it's actually implemented.
 */
#ifdef  NOSTRUCTASSIGN  /* Version for old compilers that don't support it */
#define ASSIGN(a,b)     memcpy((char *)&(a),(char *)&(b),sizeof(b));
#else                   /* Version for compilers that do */
#define ASSIGN(a,b)     ((a) = (b))
#endif
  
/* Define null object pointer in case stdio.h isn't included */
#ifndef NULL
/* General purpose NULL pointer */
#define NULL (void *)0
#endif
#define NULLCHAR (char *)0      /* Null character pointer */
#define NULLCHARP (char **)0    /* Null character pointer pointer */
#define NULLINT (int *)0        /* Null integer pointer */
#ifdef __GNUC__
#define NULLFP(x)  (int (*)x)0
#define NULLVFP(x) (void (*)x)0
#else
#define NULLFP(x)  (int (*)())0   /* Null pointer to function returning int */
#define NULLVFP(x) (void (*)())0  /* Null pointer to function returning void */
#endif
#define NULLVIFP (INTERRUPT (*)())0
#define NULLFILE (FILE *)0      /* Null file pointer */
  
/* standard boolean constants */
#define FALSE 0
#define TRUE 1
#define NO 0
#define YES 1
  
#ifdef UNIX
#ifdef BSD_RANDOM
#define SRANDOM(n) srandom(n)
#define RANDOM(n) ((int) (random() * (n)))
#else
#define SRANDOM(n) srand48(n)
#define RANDOM(n) ((int) (drand48() * (n)))
#endif
#else
#define RANDOM(n) random(n)
#endif
  
#ifdef UNIX
/* !@#$%&* DOS quote-C-unquote dialects! ++bsa */
#define strcmpi strcasecmp
#define stricmp strcasecmp
#define strncmpi strncasecmp
#define strnicmp strncasecmp

/* 11Apr06, Maiko (VE4KLM), tputs is now renamed to j2tputs, a JNOS func ! */
#ifdef	DONT_COMPILE
/* and work around a collision which is currently making me drop core... */
#define tputs j_tputs
#endif

/* some older systems lack strtoul(); we'll just have to hope this is okay */
#ifdef NO_STRTOUL
#define strtoul(s,cp,b) ((unsigned long) strtol((s),(cp),(b)))
#endif
/* minimal malloc checking is done, so intercept free() */
#define free j_free
  
#endif /* UNIX */
  
/* string equality shorthand */
#define STREQ(x,y) (strcmp(x,y) == 0)
  
/* Extract a short from a long */
#define hiword(x)       ((int16)((x) >> 16))
#define loword(x)       ((int16)(x))
  
/* Extract a byte from a short */
#define hibyte(x)       ((unsigned char)((x) >> 8))
#define lobyte(x)       ((unsigned char)(x))
  
/* Extract nibbles from a byte */
#define hinibble(x)     (((x) >> 4) & 0xf)
#define lonibble(x)     ((x) & 0xf)
  
#ifdef UNIX
/* Unix doesn't need x86 cruft */
#define MK_FP(seg,off) ((void *) (seg))
#define FP_SEG(p) ((unsigned long) (p))
#endif

#ifndef UNIX
unsigned long availmem __ARGS((void));
#endif

void *callocw __ARGS((unsigned nelem,unsigned size));

/* int32 clock __ARGS((void)); 29Sep2009, Maiko, 64 bit compatibility */

int dirps __ARGS((void));

/* File : getopt.c */
int j2getopt __ARGS((int argc,char *argv[],char *opts));

/* File : qsort.c */
void j2qsort(void *base, size_t nmemb, size_t size,
        int (*compar)(const void *, const void *));

int atoip __ARGS((char *));
int htoi __ARGS((char *));
long htol __ARGS((char *));
char *inbuf __ARGS((int16 port,char *buf,int16 cnt));
int16 hash_ip __ARGS((int32 addr));
int istate __ARGS((void));

/*
 * 02Oct2005, Maiko (VE4KLM) - The newer gcc 3.4 and 4.0 compilers are
 * complaining that *our* 'log' function conflicts with the built-in one,
 * which is correct. The built-in function is a math library function, and
 * has been around for eons. We need to rename *our* 'log' function to fix
 * this conflict. Problem is there are tons of calls to rename - quickests
 * way to do this is simply #undef log, then #define log as nos_log. That
 * way we do not have to go through all of the source and rename. We will
 * still rename the log function to nos_log in main.c, and that's all for
 * now. As source gets changed over time, we can rename the log calls at
 * our own convenience. JNOS does not use the math lib log (x) anyways.
 */

#ifdef log
#undef log
#endif
#define log nos_log
extern void nos_log __ARGS((int s,char *fmt, ...));

void mail_error __ARGS((char *fmt, ...));
#ifdef	MSDOS
/* 30Sep2005, Maiko, Not used anymore, alloc.c in legacy directory */
/* 12Oct2005, Maiko, Actually this is needed in MSDOS environment,
 * so replace the #ifdef LEGACY with #ifdef MSDOS instead.
 */
int log2 __ARGS((int16 x));
#endif
#ifdef UNIX
#define ltop(l) ((void *) (l))
#else
void *ltop __ARGS((long));
#endif
void *mallocw __ARGS((unsigned nb));
char *outbuf __ARGS((int16 port,char *buf,int16 cnt));
#ifdef UNIX
#define ptol(p) ((long)(p))
#else
long ptol __ARGS((void *));
#endif
void restore __ARGS((int));
void rflush __ARGS((void));
void rip __ARGS((char *));
char *skipwhite __ARGS((char *));
char *skipnonwhite __ARGS((char *));
char *smsg __ARGS((char *msgs[],unsigned nmsgs,unsigned n));
int tprintf __ARGS((char *fmt,...))
#ifdef __GNUC__
    __attribute__ ((format (printf, 1, 2)))
#endif
    ;

extern char *j2tmpnam (char*);	/* 18May2009, no more using GLIBC tmpnam ! */

/* 11Apr06, Maiko, tputs() renamed to j2tputs(), a JNOS function now ! */
extern int j2tputs (char*);

int tputc __ARGS((char c));

/* File : misc.c - renamed strdup() to j2strdup() cause mallocw dependency */
char *j2strdup __ARGS((const char *));

int wildmat __ARGS((char *s,char *p,char **argv));

#ifdef UNIX
#include <stdlib.h>
#include <string.h>
#include <errno.h>   /* not always included via stdlib.h */
/* can't do this above, GNU libc defines malloc to gnu_malloc in stdlib.h */
#ifdef malloc
#undef malloc
#endif
#define malloc mallocw
#endif

#ifdef  AZTEC
#define rewind(fp)      fseek(fp,0L,0);
#endif
  
#if     defined(__TURBOC__) && defined(MSDOS)
#define movblock(so,ss,do,ds,c) movedata(ss,so,ds,do,c)
#ifndef outportw
#define outportw outport
#endif
#ifndef inportw
#define inportw inport
#endif
  
#else
  
/* General purpose function macros already defined in turbo C */
#ifndef min
#define min(x,y)        ((x)<(y)?(x):(y))       /* Lesser of two args */
#endif
#ifndef max
#define max(x,y)        ((x)>(y)?(x):(y))       /* Greater of two args */
#endif
#ifdef  MSDOS
#define MK_FP(seg,ofs)  ((void far *) \
(((unsigned long)(seg) << 16) | (unsigned)(ofs)))
#endif
#endif  /* __TURBOC __ */
  
/* Try to determine byte ordering, using various system includes: endian.h, byteorder.h,... */
#ifdef linux
# include <endian.h>
#endif
#ifndef __BYTE_ORDER
# define __LITTLE_ENDIAN 1234
# ifdef UNIX
#  ifdef sun
#   include <sys/byteorder.h>
#   ifdef _LITTLE_ENDIAN
#    define __BYTE_ORDER 1234
#   endif
#   ifdef _BIG_ENDIAN
#    define __BYTE_ORDER 4321
#   endif
#  else /* UNIX but not sun ... need more info */
#   error __BYTE_ORDER undefined ... help!
#  endif
# elif defined(MSDOS)
/* ASSUME little-endian */
#  define __BYTE_ORDER __LITTLE_ENDIAN
# else
#  error I cannot determine byte ordering.
# endif
#endif /* __BYTE_ORDER */

/* Externals used by j2getopt (used to be getopt) */
extern int j2optind;
extern char *j2optarg;
  
#ifndef UNIX
/* Threshold setting on available memory */
extern int32 Memthresh;
#endif

/*
 *
 * System clock - count of ticks since startup
 *
 * 05Aug2010, Maiko (VE4KLM), System clock has been renamed to 'JnosClock' to
 * make it a bit more specific to JNOS and not the O/S - The system clock will
 * now be an unsigned 64 bit integer. This fixes the issue of the clock going
 * negative after approximately 25 days of uptime. Other ideas were definitely
 * considered, including a reset of the 'Starttime' once a week, and keeping a
 * 'weeks' counter, but that solution may be more trouble then cure. Any timers
 * still running when you did the reset, would have to be adjusted. Anyways !
 */
extern int64 JnosClock;
  
/* Various useful standard error messages */
extern char Badhost[];
extern char Badinterface[];
extern char Existingiface[];
extern char Nospace[];
extern char Notval[];
extern char Version[];
extern char Nosversion[];
extern char TelnetMorePrompt[];
extern char BbsMorePrompt[];
extern char *Hostname;
  
/* Your system's end-of-line convention */
extern char Eol[];
  
#ifndef UNIX
extern void (*Gcollect[]) __ARGS((int));
#endif
  
#ifdef UNIX
/* PCs have a few pseudo-"standard" library functions used by NOS */
extern char *stpcpy __ARGS((char *, const char *));
extern char *strlwr __ARGS((char *));
extern char *strupr __ARGS((char *));
extern char *itoa __ARGS((int, char *, int));
#endif
  
/* I know this is cheating, but it definitely makes sure that
 * each module has config.h included ! - WG7J
 */
#ifndef _CONFIG_H
#include "config.h"
#endif

#ifdef LINUX
#error "Use UNIX instead of LINUX"
#endif

#ifdef AMIGA
#error "AMIGA code doesn't work and is not supported."
#endif

#ifdef LATTICE
#error the LATTICE compiler is not supported...use Borland C instead.
#endif

#ifdef MAC
#error "MAC code doesn't work and is not supported. Try net-mac instead."
#endif

#ifdef UNIX
#ifndef _UNIXTM_H
/* this is separate so unix.c can load it without global.h */
#include "unixtm.h"
#endif
#endif
  
/* in main.c  */
void where_outta_here __ARGS((int resetme, int retcode));
  
/* Stktrace.c */
void stktrace __ARGS((void));
  
/* In alloc.c */
unsigned long Localheap(void);
  
#endif  /* _GLOBAL_H */
