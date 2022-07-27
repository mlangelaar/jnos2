/* routines to override the standard Borland library routines */
/* PA0GRI , N1BEE , aparent origin PE1CHL */
/* Compile into _TEXT segment with -zC_TEXT */
  
/* 920703, 920808: more patching to work with BC++ 3.1  -- N1BEE
        (becoming sorry he ever wrote this module...)
  
   PLEASE DO NOT CHANGE THE ANSI STYLE FUNCTION DEFINITIONS TO K&R STYLE.
   THE COMPILER CANNOT DETECT TYPE MISMATCH ERRORS IF THAT IS DONE.  THIS
   ENTIRE MODULE IS COMPILER-SPECIFIC ANYWAY, SO PORTABILITY IS NO ISSUE.
  
   Letters in comments in brackets indicate nesting level of conditionals.
  
   This file is also extensively cleaned up.
*/
  
#include <dos.h>
#include <io.h>
#include "global.h"
  
#ifdef __TURBOC__                       /* [A] */
  
/*-------------------------------------------------------------------
        ANSI function prototypes
-------------------------------------------------------------------*/
  
char * getnenv __ARGS((char *name));    /* In pc.c */
  
#ifdef __BORLANDC__                     /* [AB] */
#if __BORLANDC__ >= 0x0410              /* [ABC] Borland C++ 3.1 and later */
#define mkname(a,b,c) __MKNAME(a,b,c)
char * near pascal __MKNAME __ARGS((char *,char *,unsigned));
char * near pascal __TMPNAM __ARGS((char *,unsigned *));
  
#elif __BORLANDC__ == 0x0400            /* [ABC] BC++ 3.0 only */
#define mkname(a,b,c) __MKNAME(a,b,c)
char * near pascal __MKNAME __ARGS((char *,char *,unsigned));
  
#else                                   /* [ABC] BC++ 2.0 only */
#define mkname(a,b,c) __MKNAME(a,c)
char * near pascal __MKNAME __ARGS((char *,unsigned));
  
#endif                                  /* [ABC] */
#else                                   /* [AB] TC or TC++ */
#define mkname(a,b,c) __MKNAME(a,c)
char * pascal __MKNAME __ARGS((char *,unsigned));
  
#endif /* __BORLANDC__ */               /* [AB] */
  
  
/*-------------------------------------------------------------------
        __MKNAME()
-------------------------------------------------------------------*/
  
#ifdef __BORLANDC__                     /* [AB] BC++ */
char * near pascal mkname(char *tmpname,char *prefix,unsigned tmpnum)
                                        /* 'prefix' will never be used */
  
#else                                   /* [AB] TC or TC++ */
char *pascal mkname(tmpname,notused,tmpnum)
char *tmpname;
unsigned int tmpnum;
  
#endif /* __BORLANDC__ */               /* [AB] */
  
{
    char *tmpdir,*p;
    static char staticname[80];
  
    if(tmpname == NULL)
        tmpname = staticname;
  
    p = tmpdir = getnenv("TMP");    /* get tempdir name */
    if(p[0] != '\0')
        p += strlen(p) - 1;     /* point to last character */
  
    sprintf(tmpname,"%s%sTMP%u.$$$",tmpdir,
    ((*p != '/' && *p != '\\') ? "/" : ""),tmpnum);
  
    return tmpname;
}
  
/*-------------------------------------------------------------------
        tmpnam()/__TMPNAM()
-------------------------------------------------------------------*/
  
/* For the earlier compilers, __MKNAME() and tmpnam() were bound into
   the same source module, and therefore into the same OBJ module,
   which meant that replacing __MKNAME() forced us to replace tmpnam()
   and its global data.  With the newer compiler, tmpnam() is in a
   separate module, but all tmpnam() does is call __TMPNAM(), which
   is in the same module as __MKNAME().  As a result, the newer
   compiler no longer requires us to replace tmpnam(), but rather
   __TMPNAM().
*/
  
#if !defined(__BORLANDC__) || (__BORLANDC__ <= 0x0400)
                                        /* [AB] BC++ 3.0 and earlier */
  
unsigned int _tmpnum = 0;
  
char *tmpnam(name)
char *name;
{
    do {
  
        if(_tmpnum == 0xffff)
            _tmpnum = 2;
        else
            ++_tmpnum;
  
        name = mkname(name,"TMP",_tmpnum);
  
    } while(access(name,0) != -1);
  
    return name;
}
  
  
#else                                   /* [AB] BC++ 3.1 and later */
  
/* The following few lines of code which make up __TMPNAM()
   are lifted almost directly from the Borland library source,
   which is "Copyright (c) 1987, 1992 by Borland International".
   This routine is called only from tmpnam() and tmpfile() in
   the Borland library.  ("-1U"?)  -- N1BEE
*/
  
char * near pascal __TMPNAM(char *s,unsigned *numP)
{
    unsigned attr;
  
    do
        s = mkname(s,(char *)NULL,*numP += ((*numP == -1U) ? 2 : 1));
  
        while(_dos_getfileattr(s,&attr) == 0);
  
    return(s);
}
  
#endif                                  /* [AB] */
  
#endif /* __TURBOC__ */                 /* [A] */
  
