/* Miscellaneous machine independent utilities
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Adding IPV6, beginning Feb, 2023, by Maiko Langelaar, VE4KLM
 *  (wanting unsigned buffer versions of put and get functions)
 */
#include "global.h"
#include "socket.h"
#include "mbuf.h"
  
/* convert a tcp port number or name to integer - WG7J */
int
atoip(char *s) {
    int p,n;
  
    if((p=atoi(s)) == 0) {
        n = strlen(s);
        if(!strncmp(s,"convers",n))
            p = IPPORT_CONVERS;
/* 02Sep2020, Maiko (VE4KLM), Adding in mods from Brian (N1URO) */
#ifdef TNODE
        else if(!strncmp(s,"node",n))
            p = IPPORT_NODE;            /* URONode port - N1URO */
#endif
        else if(!strncmp(s,"telnet",n))
            p = IPPORT_TELNET;
        else if(!strncmp(s,"ttylink",n))
            p = IPPORT_TTYLINK;
    }
    return p;
}
  
/* Select from an array of strings, or return ascii number if out of range */
char *
smsg(msgs,nmsgs,n)
char *msgs[];
unsigned nmsgs,n;
{
    static char buf[16];
  
    if(n < nmsgs && msgs[n] != NULLCHAR)
        return msgs[n];
    sprintf(buf,"%u",n);
    return buf;
}
  
/* Convert hex-ascii to integer */
int
htoi(s)
char *s;
{
    int i = 0;
    char c;
  
    while((c = *s++) != '\0'){
        if(c == 'x')
            continue;   /* allow 0x notation */
        if('0' <= c && c <= '9')
            i = (i * 16) + (c - '0');
        else if('a' <= c && c <= 'f')
            i = (i * 16) + (c - 'a' + 10);
        else if('A' <= c && c <= 'F')
            i = (i * 16) + (c - 'A' + 10);
        else
            break;
    }
    return i;
}
  
/*
 * Copy a string to a malloc'ed buffer. Turbo C has this one in its
 * library, but it doesn't call mallocw() and can therefore return NULL.
 * NOS uses of strdup() generally don't check for NULL, so they need this one.
 *
 * 03Apr2006, Maiko (VE4KLM), renamed to j2strdup () to avoid conflicts
 * with system library function of the same name.
 */
char *
j2strdup(s)
const char *s;
{
    register char *out;
    register int len;
  
    if(s == NULLCHAR)
        return NULLCHAR;
    len = strlen(s);
    out = mallocw(len+1);
    /* This is probably a tad faster than strcpy, since we know the len */
    memcpy(out,s,len);
    out[len] = '\0';
    return out;
}
/* Routines not needed for Turbo 2.0, but available for older libraries */
#ifdef  AZTEC
  
/* Case-insensitive string comparison */
strnicmp(a,b,n)
register char *a,*b;
register int n;
{
    char a1,b1;
  
    while(n-- != 0 && (a1 = *a++) != '\0' && (b1 = *b++) != '\0'){
        if(a1 == b1)
            continue;   /* No need to convert */
        a1 = tolower(a1);
        b1 = tolower(b1);
        if(a1 == b1)
            continue;   /* NOW they match! */
        if(a1 > b1)
            return 1;
        if(a1 < b1)
            return -1;
    }
    return 0;
}
  
char *
strtok(s1,s2)
char *s1;   /* Source string (first call) or NULL */
#ifdef  __STDC__    /* Ugly kludge for aztec's declaration */
const char *s2; /* Delimiter string */
#else
char *s2;   /* Delimiter string */
#endif
{
    static int isdelim();
    static char *next;
    register char *cp;
    char *tmp;
  
    if(s2 == NULLCHAR)
        return NULLCHAR;    /* Must give delimiter string */
  
    if(s1 != NULLCHAR)
        next = s1;      /* First call */
  
    if(next == NULLCHAR)
        return NULLCHAR;    /* No more */
  
    /* Find beginning of this token */
    for(cp = next;*cp != '\0' && isdelim(*cp,s2);cp++)
        ;
  
    if(*cp == '\0')
        return NULLCHAR;    /* Trailing delimiters, no token */
  
    /* Save the beginning of this token, and find its end */
    tmp = cp;
    next = NULLCHAR;    /* In case we don't find another delim */
    for(;*cp != '\0';cp++){
        if(isdelim(*cp,s2)){
            *cp = '\0';
            next = cp + 1;  /* Next call will begin here */
            break;
        }
    }
    return tmp;
}
static int
isdelim(c,delim)
char c;
register char *delim;
{
    char d;
  
    while((d = *delim++) != '\0'){
        if(c == d)
            return 1;
    }
    return 0;
}
#endif  /* AZTEC */
  
  
  
/* Host-network conversion routines, replaced on the x86 with
 * assembler code in pcgen.asm
 */
#ifndef MSDOS

#ifdef IPV6

/*
 * 12Feb2023, Maiko (VE4KLM), Playing with unsigned char buffer
 * versions of put32() and put16() function below, IPV6 does not
 * play well with signed character variables and buffers ...
 */

unsigned char *put32ub (register unsigned char *cp, int32 x)
{
    *cp++ = x >> 24;
    *cp++ = x >> 16;
    *cp++ = x >> 8;
    *cp++ = x;
    return cp;
}

unsigned char *put16ub (register unsigned char *cp, int16 x)
{
    *cp++ = x >> 8;
    *cp++ = x;
  
    return cp;
}

#endif

/* Put a long in host order into a char array in network order */
char *
put32(cp,x)
register char *cp;
int32 x;
{
    *cp++ = x >> 24;
    *cp++ = x >> 16;
    *cp++ = x >> 8;
    *cp++ = x;
    return cp;
}

/* Put a short in host order into a char array in network order */
char *
put16(cp,x)
register char *cp;
int16 x;
{
    *cp++ = x >> 8;
    *cp++ = x;
  
    return cp;
}

#ifdef	IPV6

/*
 * 12Feb2023, Maiko (VE4KLM), Playing with unsigned char buffer
 * versions of get16() and get32() function below, IPV6 does not
 * play well with signed character variables and buffers ...
 */

int16 get16ub (register unsigned char *cp)
{
    register int16 x;
  
    x = uchar(*cp++);
    x <<= 8;
    x |= uchar(*cp);
    return x;
}

int32 get32ub (register unsigned char *cp)
{
    int32 rval;
  
    rval = uchar(*cp++);
    rval <<= 8;
    rval |= uchar(*cp++);
    rval <<= 8;
    rval |= uchar(*cp++);
    rval <<= 8;
    rval |= uchar(*cp);
  
    return rval;
}

#endif

int16
get16(cp)
register char *cp;
{
    register int16 x;
  
    x = uchar(*cp++);
    x <<= 8;
    x |= uchar(*cp);
    return x;
}
/* Machine-independent, alignment insensitive network-to-host long conversion */
int32
get32(cp)
register char *cp;
{
    int32 rval;
  
    rval = uchar(*cp++);
    rval <<= 8;
    rval |= uchar(*cp++);
    rval <<= 8;
    rval |= uchar(*cp++);
    rval <<= 8;
    rval |= uchar(*cp);
  
    return rval;
}

#ifdef	MSDOS

/*
 * 30Sep2005, Maiko, This function is only called by
 * the alloc.c module (which I've placed in the legacy
 * subdirectory of my JNOS development source). It is
 * also prototyped in global.h (which I will remove).
 *
 * Compilers are (rightfully) starting to say :
 *
 *  warning: conflicting types for built-in function 'log2'
 *
 * 12Oct2005, Maiko, Actually this is needed in MSDOS compiles,
 * so remove the #ifdef LEGACY and replace with #ifdef MSDOS.
 */

/* Compute int(log2(x)) */
int
log2(x)
register int16 x;
{
    register int n = 16;
    for(;n != 0;n--){
        if(x & 0x8000)
            break;
        x <<= 1;
    }
    n--;
    return n;
}

#endif	/* end of MSDOS */
  
#endif
  
