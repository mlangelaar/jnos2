/* Higher level user subroutines built on top of the socket primitives
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by PA0GRI
 */
#include "global.h"
#ifdef  ANSIPROTO
#include <stdarg.h>
#endif
#include "mbuf.h"
#include "proc.h"
#include "socket.h"
#ifdef  LZW
#include "lzw.h"
#endif
#include "usock.h"
#include "session.h"
#include "nr4.h"

/* 29Jun2016, Maiko (VE4KLM), Allow sysop to disable EOL warnings */
extern int Meolwarnings;
  
/*
#define oldusputs
*/

#ifdef	DONT_COMPILE

/* 10Apr06, Maiko (VE4KLM), I'll be darned - not called from anywhere !!! */
  
/* Higher-level receive routine, intended for connection-oriented sockets.
 * Can be used with datagram sockets, although the sender id is lost.
 */
int
j2recv(s,buf,len,flags)
int s;      /* Socket index */
char *buf;  /* User buffer */
int len;    /* Max length to receive */
int flags;  /* Unused; will eventually select oob data, etc */
{
    struct mbuf *bp;
    int cnt;
  
    if(len == 0)
        return 0;   /* Otherwise would be interp as "all" */
  
    cnt = recv_mbuf(s,&bp,flags,NULLCHAR,(int *)NULL);
    if(cnt > 0){
        cnt = min(cnt,len);
        pullup(&bp,buf,(int16)cnt);
        free_p(bp);
    }
    return cnt;
}

#endif

#ifdef	DONT_COMPILE

/* 11Apr06, Maiko, Same thing, I'll be darned, not used by JNOS anywhere */

/* Higher level receive routine, intended for datagram sockets. Can also
 * be used for connection-oriented sockets, although from and fromlen are
 * ignored.
 */
int
j2recvfrom(s,buf,len,flags,from,fromlen)
int s;      /* Socket index */
char *buf;  /* User buffer */
int len;    /* Maximum length */
int flags;  /* Unused; will eventually select oob data, etc */
char *from; /* Source address, only for datagrams */
int *fromlen;   /* Length of source address */
{
    struct mbuf *bp;
    register int cnt;
  
    cnt = recv_mbuf(s,&bp,flags,from,fromlen);
    if(cnt > 0){
        cnt = min(cnt,len);
        pullup(&bp,buf,(int16)cnt);
        free_p(bp);
    }
    return cnt;
}

#endif	/* end of DONT_COMPILE */

/* High level send routine */
int
j2send(s,buf,len,flags)
int s;      /* Socket index */
char *buf;  /* User buffer */
int len;    /* Length of buffer */
int flags;  /* Unused; will eventually select oob data, etc */
{
    register struct mbuf *bp;
    char sock[MAXSOCKSIZE];
    int i = MAXSOCKSIZE;
  
    if(j2getpeername(s,sock,&i) == -1)
        return -1;
    bp = qdata(buf,(int16)len);
    return send_mbuf(s,bp,flags,sock,i);
}
/* High level send routine, intended for datagram sockets. Can be used on
 * connection-oriented sockets, but "to" and "tolen" are ignored.
 */
int
j2sendto(s,buf,len,flags,to,tolen)
int s;      /* Socket index */
char *buf;  /* User buffer */
int len;    /* Length of buffer */
int flags;  /* Unused; will eventually select oob data, etc */
char *to;   /* Destination, only for datagrams */
int tolen;  /* Length of destination */
{
    register struct mbuf *bp;

    bp = qdata(buf,(int16)len);
    return send_mbuf(s,bp,flags,to,tolen);
}

/*
 * 01June2016, Maiko (VE4KLM), need some type of holding area indexed
 * by socket to hold the last eol that may have been gobbled up so that
 * we can ensure the integrity of the data for subsequent recvline().
 */

typedef struct eolgobble {
    int soc;
    int eol;
} GOBBLEEOL;

static GOBBLEEOL gobbleeol[10];

/* Receive a newline-terminated line from a socket, returning # chars read.
 * The end-of-line sequence is recognized and translated into a single '\n'.
 */
int
recvline(s,buf,len)
int s;      /* Socket index */
char *buf;  /* User buffer */
unsigned len;   /* Length of buffer */
{
    int gnt, c, cnt = 0;

	for (gnt = 0; gnt <  10; gnt++)
	{
		if (gobbleeol[gnt].soc == s)
		{
			gobbleeol[gnt].soc = 0;

			if (Meolwarnings)
			{
				log (s, "warning : reintegrating data [%c] from eol issue",
					gobbleeol[gnt].eol);
			}

       		if(buf != NULLCHAR)
           		*buf++ = gobbleeol[gnt].eol;

			cnt++;

			len--;

			break;	/* 28Jun2016, Maiko (VE4KLM), Probably should do this */
		}
	}
  
    while(len-- > 1){
        if((c = recvchar(s)) == EOF){
            cnt = -1;
            break;
        }
        if(buf != NULLCHAR)
            *buf++ = c;
        cnt++;
        if(uchar(c) == '\n')
            break;
    }
    if(buf != NULLCHAR)
        *buf = '\0';
    return cnt;
}
#if defined(ANSIPROTO)
/* Do printf on a user socket */
int
usprintf(int s,char *fmt,...)
{
    va_list args;
    int len;
  
    va_start(args,fmt);
    len = usvprintf(s,fmt,args);
    va_end(args);
    return len;
}
/* Printf on standard output socket */
int
tprintf(char *fmt,...)
{
    va_list args;
    int len;
  
    va_start(args,fmt);
    len = usvprintf(Curproc->output,fmt,args);
    va_end(args);
    return len;
}
  
#ifdef UNIX

#if 0
/* Print on Tracesession stdout socket (see trace.[ch]) */
int
traceusprintf(int trsock, char *fmt, ...)
{
	extern struct session *Trace;
	extern int Tracesession;
        extern int stdoutSock;
	va_list args;
	int len;

	va_start(args,fmt);
	if (trsock == stdoutSock)
	{
	    if (Tracesession)
		trsock = Trace->output;
	    else
		trsock = Command->output;
        }
        len = usvprintf(trsock, fmt, args);
	va_end(args);
	return len;
}
#endif

/* New style - use session manager switch's status entry point (see smtpserv.c) */
int
tcmdprintf(const char *fmt, ...)
{
    va_list args;
    char *buf;
    int len;
#include "sessmgr.h"

    buf = mallocw(SOBUF);
    va_start(args, fmt);
    if ((len = vsprintf(buf, fmt, args)) >= SOBUF)
    {
	log(-1, "tcmdprintf() buffer overflow");
	where_outta_here(1,252);
    }
    va_end(args);
#ifndef HEADLESS
    sm_status(0, buf);
#endif
    free(buf);
    return len;
}

/* Print on Current process' (formerly Command) stdout socket or a file (see domain.c) */
int
tfprintf(FILE *fp, char *fmt,...)
{
	va_list args;
	int len;

	va_start(args,fmt);
	if (fp == stdout)
	    len = usvprintf(Curproc->output, fmt, args);
	else
	    len = vfprintf(fp, fmt, args);
	va_end(args);
	return len;
}
#endif
  
/* The guts of printf, uses variable arg version of sprintf */
int
usvprintf(int s,char *fmt, va_list args)
{
    int len,withargs;
    char *buf;
  
    if(strchr(fmt,'%') == NULLCHAR){
        /* Common case optimization: no args, so we don't
         * need vsprintf()
         */
        withargs = 0;
        buf = fmt;
        len = strlen(fmt);
    } else {
        /* Use a default value that is hopefully longer than the
         * biggest output string we'll ever print (!)
         */
        withargs = 1;
        buf = mallocw(SOBUF);
/* Start of mod by N4YYH */
        if((len=vsprintf(buf,fmt,args)) >= SOBUF) {
            /* It's too late to be sorry. He's dead, Jim. */
#ifdef STKTRACE
            stktrace();
#endif
            log(s,"usvprintf() has exceeded the size of it's buffer. Restarting NOS.");
            where_outta_here(1,252);
        }
/* End of mod by N4YYH */
/*
        len = strlen(buf);
*/
    }
    if(usputs(s,buf) == EOF)
        len = -1;
    if(withargs)
        free(buf);
    return len;
}
#else
/*VARARGS*/
/* Printf to standard output socket */
int
tprintf(fmt,arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10,arg11,arg12)
char *fmt;      /* Message format */
int arg1,arg2,arg3; /* Arguments */
int arg4,arg5,arg6;
int arg7,arg8,arg9;
int arg10,arg11,arg12;
{
    return usprintf(Curproc->output,fmt,arg1,arg2,arg3,arg4,arg5,arg6,
    arg7,arg8,arg9,arg10,arg11,arg12);
}
/* Printf to socket. Doesn't use ANSI vsprintf */
int
usprintf(s,fmt,arg1,arg2,arg3,arg4,arg5,arg6,arg7,arg8,arg9,arg10,arg11,arg12)
int s;          /* Socket index */
char *fmt;      /* Message format */
int arg1,arg2,arg3; /* Arguments */
int arg4,arg5,arg6;
int arg7,arg8,arg9;
int arg10,arg11,arg12;
{
    int len,withargs;
    char *buf;
  
    if(strchr(fmt,'%') == NULLCHAR){
        /* No args, so we don't need vsprintf() */
        withargs = 0;
        buf = fmt;
        len = strlen(fmt);
    } else {
        /* Use a default value that is hopefully longer than the
         * biggest output string we'll ever print (!)
         */
        withargs = 1;
        buf = mallocw(SOBUF);
/* Start of mod by N4YYH */
        if((len=sprintf(buf,fmt,arg1,arg2,arg3,arg4,arg5,arg6,arg7,
        arg8,arg9,arg10,arg11,arg12)) >= SOBUF) {
            /* It's too late to be sorry. He's dead, Jim. */
            log(s,"usvprintf() has exceeded the size of it's buffer. Restarting NOS.");
            where_outta_here(1,252);
        }
/* End of mod by N4YYH */
/*        len = strlen(buf);    */
    }
    if(usputs(s,buf) == EOF)
        len = -1;
  
    if(withargs)
        free(buf);
    return len;
}
#endif
  
/* Buffered putchar to a socket */
int
usputc(s,c)
int s;
char c;
{
    struct usock *up;
    register struct mbuf *bp;
    char *cp;
    int newline;
#ifndef LZW
	int len;
#endif
  
    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return -1;
    }
	/*
	 * 14Apr2016, Maiko (VE4KLM), One will have to mask the 'flag' now
	 * before looking at the mode values, since I've had to add bits to
	 * the flag for the new IAC processing used in telnet forwarding.
	 */
    if(c == '\n' && ((up->flag & SOCK_FLAG_MASK) & SOCK_ASCII)){
        newline = 1;
#ifndef LZW
        len = strlen(up->eol);
#endif
    } else {
        newline = 0;
#ifndef LZW
        len = 1;
#endif
    }
#ifndef LZW
    /* Make sure there's room in the current buffer, if any */
    if((bp = up->obuf) != NULLBUF){
        if((bp->cnt >= bp->size - len) && usflush(s) == -1)
            return EOF;
    }
#endif
    if(up->obuf == NULLBUF){
        /* Allocate a buffer of appropriate size */
        up->obuf = ambufw(BUFSIZ);
    }
    /* Note: the buffer must be larger than the end-of-line sequence! */
    bp = up->obuf;
    cp = &bp->data[bp->cnt];
    if(newline){
        /* Translate into appropriate end-of-line sequence */
#ifdef  LZW
        for(cp = up->eol;*cp != '\0';cp++)
            if(up->zout == NULLLZW)
                bp->data[bp->cnt++] = *cp;
            else
                lzwencode(s,*cp);
#else
        strncpy(cp,up->eol,(unsigned)len);
#endif
    } else {
#ifdef  LZW
        if(up->zout == NULLLZW)
            bp->data[bp->cnt++] = c;
        else
            lzwencode(s,c);
    }
#else
    *cp = c;
}
bp->cnt += len;
#endif
    /* Flush if necessary and leave enough room for a comming eol */
if((c == up->flush && up->flush != -1) || bp->cnt >= bp->size-9)
    if(usflush(s) == -1)
        return -1;
  
#ifdef LOOKSESSION
if(up->look)    /* Some one is looking at us ! */
    usputc(up->look->output,c);
#endif
  
return (int)uchar(c);
}
  
/* Put a character to standard output socket */
int
tputc(c)
char c;
{
    return usputc(Curproc->output,c);
}
  
#ifndef oldusputs
/* Buffered puts to a socket */
int
usputs(s,buf)
int s;
char *buf;
{
    register struct usock *up;
    register struct mbuf *bp;
    char *cp,*wp;
    int16 len,clen;
    int doflush;
    int eol_len=0;
    int16 flushpt;
    int ascii;
#ifdef LOOKSESSION
    char *oldbuf;
#endif
  
#if defined(UNIX) && defined(TRACE)
    /* n5knx: tracing output goes to stdoutSock, which we can now redirect
       to the Trace session if desired.  Note we no longer need #defines to
       force the use of specialized output routines, eg, traceusprintf().
       */

    extern struct session *Trace;
    extern int Tracesession;
    extern int stdoutSock;

    if (s == stdoutSock) {
        if (Tracesession)
            s = Trace->output;
        else
            s = Command->output;
    }
#endif

    if((up = itop(s)) == NULLUSOCK){
        errno = EBADF;
        return EOF;
    }
#ifdef LOOKSESSION
    oldbuf = buf;
#endif
#ifdef  LZW
    if(up->zout != NULLLZW){
        while(*buf != '\0')
            if(usputc(s,*buf++) == EOF)
                return EOF;
/* K5JB: usputc() has already copied to the look socket...
 *   #ifdef LOOKSESSION
 *       if(up->look)
 *           usputs(up->look->output,oldbuf);
 *   #endif
 */
        return 0;
    }
#endif
	/*
	 * 14Apr2016, Maiko (VE4KLM), One will have to mask the 'flag' now
	 * before looking at the mode values, since I've had to add bits to
	 * the flag for the new IAC processing used in telnet forwarding.
	 */
    ascii = (up->flag & SOCK_FLAG_MASK) & SOCK_ASCII;
    if(ascii)
        eol_len = strlen(up->eol);
    doflush = (up->flush != -1) && (strchr(buf,up->flush) != NULLCHAR);
    len = strlen(buf);
  
    while(len != 0){
        if(up->obuf == NULLBUF){
            /* Allocate a buffer of appropriate size */
            clen = BUFSIZ;
            up->obuf = ambufw(clen);
        }
        /* Note: the buffer must be larger than the end-of-line sequence! */
        bp = up->obuf;
        wp = &bp->data[bp->cnt];
        if(ascii && (cp = strchr(buf,'\n')) != NULLCHAR){
            /* Copy up to, but not including, newline */
            clen = (int16)(cp - buf);
        } else {
            /* Copy whole thing */
            clen = len;
        }
        /* ...but no more than the room available */
        clen = min(clen,bp->size - bp->cnt);
        if(clen != 0){
            memcpy(wp,buf,(size_t)clen);
            wp += clen;
            bp->cnt += clen;
            buf += clen;
            len -= clen;
        }
        /* Set flush threshold to allow for eol, if enabled */
        if(ascii)
            flushpt = bp->size - eol_len;
        else
            flushpt = bp->size;
  
        if(ascii && *buf == '\n' && bp->cnt < flushpt){
            /* Add appropriate end-of-line sequence */
            strncpy(wp,up->eol,(size_t)eol_len);
            wp += eol_len;
            bp->cnt += eol_len;
            buf++;  /* Skip newline in buffer */
            len--;
        }
        if(bp->cnt >= flushpt){
            /* Buffer full, flush and get another */
            if(usflush(s) == -1)
                return EOF;
        }
    }
    if(doflush && usflush(s) == -1)
        return EOF;
  
#ifdef LOOKSESSION
    if(up->look)        /* someone is looking at us ! */
        usputs(up->look->output,oldbuf);
#endif
  
    return 0;
}
  
#else
  
int
usputs(s,x)
int s;
register char *x;
{
    while(*x != '\0') {
        if(usputc(s,*x++) == EOF)
            return EOF;
    }
    return 0;
}
#endif
  
/* Put a string to standard output socket */
/* 11Apr06, Maiko, Changed to j2tputs to avoid conflicts with curses, etc */
int
j2tputs(s)
char *s;
{
    return usputs(Curproc->output,s);
}
  
/* Read a raw character from a socket with stream buffering. */
int
rrecvchar(s)
int s;          /* Socket index */
{
    register struct usock *up;
#ifdef  LZW
    register int c;
#endif
  
    if((up = itop(s)) == NULLUSOCK)
        return EOF;
#ifdef  LZW
    if(up->zin != NULLLZW)
        if((c = lzwdecode(up)) != -1)
            return c;
#endif
    /* Replenish if necessary */
    if(up->ibuf == NULLBUF && recv_mbuf(s,&up->ibuf,0,NULLCHAR,0) <= 0)
        return EOF;
  
#ifdef  LZW
    if(up->zin != NULLLZW)
	{
        if((c = lzwdecode(up)) != -1)
            return c;
        else
            return rrecvchar(s);    /* needs replenish */
	}
#endif
    return PULLCHAR(&up->ibuf); /* Returns -1 if eof */
}

/*
 * This function recognizes the end-of-line sequence for the stream
 * and translates it into a single '\n'.
 *
 * 01Jun2016, Maiko (VE4KLM), Renamed and added argument to return
 * what is 'eaten' up if EOL Sequence is more then 1 character. This
 * in turn is now called by a new recvchar function (further below)
 * which now acts as a wrapper to this slighty modified original.
 */
static int recvchar_witheol (int s, int *eol_gobble_up)
{
    int c;
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK)
        return EOF;
  
    c = rrecvchar(s);
  
	/*
	 * 14Apr2016, Maiko (VE4KLM), One will have to mask the 'flag' now
	 * before looking at the mode values, since I've had to add bits to
	 * the flag for the new IAC processing used in telnet forwarding.
	 */
    if(c != up->eol[0] || !((up->flag & SOCK_FLAG_MASK) & SOCK_ASCII)) {
#ifdef LOOKSESSION
        if(c!=EOF && up->look)
            usputc(up->look->output,c);
#endif
        return c;
    }
  
    /* This is the first char of a eol sequence. If the eol sequence is
     * more than one char long, eat the next character in the input stream.
     */
    if(up->eol[1] != '\0')
	{
        *eol_gobble_up = rrecvchar(s);
	/*
	 * 01Jun2016, Maiko (VE4KLM), It would appear from my traces that linfbb
	 * is not giving consistent EOL sequences for incoming proposals, first
	 * batch comes in fine, then I send out proposal, but then the second set
	 * of incoming no longer comes in as 0x0d 0x0a, it's just 0x0d, which is
	 * not good, since JNOS will gobble up the byte after, which just happens
	 * to be the starting byte (F) of another incoming proposal or whatever.
	 *
	 * So let's try and reintegrate this gobbled up data inside the recvline()
	 * function, so that the broken eol sequence remains transparent to the
	 * overall forwarding process (might not be that easy to do, we'll see).
	 */
		if (*eol_gobble_up > 0x10)
		{
			int cnt;

			if (Meolwarnings)
			{
				log (s, "warning : expected eol seq, got data [%c] instead",
					*eol_gobble_up);
			}

			for (cnt = 0; cnt <  10; cnt++)
			{
				if (gobbleeol[cnt].soc == 0)
				{
					gobbleeol[cnt].soc = s;
					gobbleeol[cnt].eol = *eol_gobble_up;
					break;
				}
			}
		}
		/*	
		 * 09Jun2016, Maiko (VE4KLM), If we timeout (-1 returned) it means we
		 * never got the full EOL sequence, so put a warning in the log !!!
 		 */
		else if (*eol_gobble_up == -1)
		{
			if (Meolwarnings)
				log (s, "warning : expected eol seq, got nothing");
		}
    }
  
#ifdef LOOKSESSION
    if(up->look) {
        usputc(up->look->output,'\n');
        /* In the case 'looking' is done from bbs connection,
         * flush it here...
         */
        usflush(up->look->output);
    }
#endif
  
    return '\n';
}

/* 01Jun2016, Maiko (VE4KLM), now just a wrapper to it's original self below */
int recvchar (int s)
{
	int eol_gobble_up;

	return recvchar_witheol (s, &eol_gobble_up);
}

/* Flush output on a socket stream */
int
usflush(s)
int s;
{
    register struct usock *up;
    struct mbuf *bp;
  
    if((up = itop(s)) == NULLUSOCK)
        return -1;
  
    if(up->obuf != NULLBUF){
#ifdef  LZW
        if(up->zout != NULLLZW)
            lzwflush(up);
#endif
        bp = up->obuf;
        up->obuf = NULLBUF;
        return send_mbuf(s,bp,0,NULLCHAR,0);
    }
  
#ifdef LOOKSESSION
    if(up->look)    /* Some one is looking at us, flush them as well */
        usflush(up->look->output);
#endif
  
    return 0;
}
/* Flush output socket */
void
tflush()
{
    usflush(Curproc->output);  /* was Current->output .. wrong except for timer (c.v.) */
}
  
/* Print prompt and read one character */
int
keywait(prompt,flush)
char *prompt;   /* Optional prompt */
int flush;  /* Flush queued input? */
{
    int c;
    int i,len;
  
    if(flush && socklen(Curproc->input,0) != 0)
        recv_mbuf(Curproc->input,NULLBUFP,0,NULLCHAR,0); /* flush */
    if(prompt == NULLCHAR)
        prompt = "Hit enter to continue";
    j2tputs(prompt);
    tflush();
    c = recvchar(Curproc->input);
    /* Get rid of the prompt */
    for(i=len=strlen(prompt);i != 0;i--)
        tputc('\b');
    for(i=len;i != 0;i--)
        tputc(' ');
    for(i=len;i != 0;i--)
        tputc('\b');
    tflush();
    return (int)c;
}
  
/* Set the end-of-line sequence on a socket */
int
seteol(s,seq)
int s;
char *seq;
{
    register struct usock *up;
  
    if((up = itop(s)) == NULLUSOCK)
        return -1;
  
    if(seq != NULLCHAR)
        strncpy(up->eol,seq,sizeof(up->eol));
    else
        *up->eol = '\0';
    return 0;
}
/* Enable/disable eol translation, return previous state */
int
sockmode(s,mode)
int s,mode;
{
    struct usock *up;
    int prev;

    int no_aic_processing;	/* 14Apr2016, Maiko (VE4KLM) see below */
  
    if((up = itop(s)) == NULLUSOCK)
        return -1;

    usflush(s);
/*
 * 14Apr2016, Maiko (VE4KLM), I want to use the flag field for some new
 * bits (not values), so we'll have to mask them off BEFORE we set the
 * BINARY or ASCII mode, then add them back AFTER it's been done, this
 * keeps the bits 'hidden' and I don't affect all the existing sockmode
 * calls, that's a bit heavy to have to rewrite right now ...
 *
 * Why in the usock structure ? Cause it's easy to get to given a socket.
 *
 * All of this is because I finally got the IAC processing 'working' in the
 * telnet forwarding code, but it would 'seem' Winlink is not sticking to the
 * RFC set out for telnet (ie, not using the IAC manipulation rules at all).
 *
 */
    no_aic_processing = up->flag & SOCK_NO_IAC;
    up->flag &= ~SOCK_NO_IAC;

    prev = up->flag;	/* 12/13Jun2016, Maiko (VE4KLM), I screwed up, this
			 * MUST go after applying IAC mask, not before !!!
			 */
    switch(mode){
        case SOCK_BINARY:
        case SOCK_ASCII:
            up->flag = mode;
            break;
        default:
            break;
    }

    if (no_aic_processing)
    	up->flag |= SOCK_NO_IAC;

    return prev;
}
/* Specify the character to trigger automatic output buffer
 * flushing, or -1 to disable it. Return the previous setting.
 */
int
j2setflush(s,c)
int s;
int c;
{
    register struct usock *up;
    int old;
  
    if((up = itop(s)) == NULLUSOCK)
        return -1;
  
    old = up->flush;
    up->flush = c;
    return old;
}
  
/* Set the block mode on a socket - WG7J.
 * Primarily used on convers.c to prevent backlog of data and
 * usprintf() calls blocking because of it...
 * returns previous mode
 */
int sockblock(int s,int value) {
  
    register struct usock *up;
    int oldval;
  
    if((up = itop(s)) == NULLUSOCK)
        return -1;
  
    oldval = up->noblock;
    up->noblock = value;
    return oldval;
}
  
