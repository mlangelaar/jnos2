/* Post Office Protocol (POP3) Client -- RFC1225
 * Copyright 1992 William Allen Simpson
 *      partly based on a NNTP client design by Anders Klemets, SM0RGV
 *      and POP2 Client by Mike Stockett, WA7DYX, et alia.
 *
 *      Support for Mail Index Files, June/July 1993, Johan. K. Reinalda, WG7J
 *      Add APOP cmd [RFC1725] (when MD5AUTHENTICATE is defined) - n5knx 10/96.
 */
#include <time.h>
#include "global.h"
#ifdef POP3CLIENT
#include "timer.h"
#include "proc.h"
#include "netuser.h"
#include "socket.h"
#include "cmdparse.h"
#include "files.h"
#include "mailcli.h"
#include "mailutil.h"
#include "smtp.h"
#if defined(LZW)
#include "lzw.h"
extern int poplzw;
#endif
#ifdef MD5AUTHENTICATE
#include "md5.h"
#endif

#ifdef STATUSWIN
#if defined(POP2SERVER) || defined(POP3SERVER)
extern
#endif
int PopUsers;
#endif // STATUSWIN

/* 29Dec2004, Replace GOTO 'quit' with function */
static void do_quit (int s, FILE *wfp, struct mailservers *np)
{  
    log(s,"POP3 daemon exiting" );
    close_s(s);
#ifdef STATUSWIN
    PopUsers--;
#endif
    if (wfp != NULLFILE)
        fclose(wfp);
    if (np->timer.duration == -1L)
		np->timer.duration = 0L;  /* reset flag */
    start_timer(&np->timer);
}

void
pop3_job(unused,v1,p2)
int unused;
void *v1;
void *p2;
{
    struct mailservers *np = v1;
    struct sockaddr_in fsocket;
    char buf[TLINELEN];
    char *cp;
    FILE *wfp = NULLFILE;
    time_t t;
    int s = -1;
    int bytes;
    int messages;
    int i;
#if defined(LZW)
    int lzwmode, lzwbits;
    extern int16 Lzwbits;
    extern int Lzwmode;
#endif
#ifdef MD5AUTHENTICATE
    char *cp1;
#endif
	int authokay = 0;
  
    if ( mailbusy( np ) )
        return;
  
    if ( (fsocket.sin_addr.s_addr = resolve(np->hostname)) == 0L ) {
        /* No IP address found */
        if (Mailtrace >= 1)
            log(-1,"POP3 can't resolve host '%s'", np->hostname);
        start_timer(&np->timer);
        return;
    }
  
    fsocket.sin_family = AF_INET;
    fsocket.sin_port = IPPORT_POP3;
  
    s = j2socket(AF_INET,SOCK_STREAM,0);
    sockmode(s,SOCK_ASCII);
  
    /* n5knx: Kludge a flag indicating a manual kick is in progress */
    if (np->timer.duration == 0L) np->timer.duration = -1L;

    if (j2connect(s,(char *)&fsocket,SOCKSIZE) == -1)
	{
        cp = sockerr(s);
        if (Mailtrace >= 2)
            log(s,"POP3 Connect failed: %s",
            cp != NULLCHAR ? cp : "" );
#ifdef STATUSWIN
	PopUsers++;		// because it gets incremented at quit
#endif
		return (do_quit (s, wfp, np));
    }
  
    log(s,"POP3 Connected to mailhost %s", np->hostname);

#ifdef STATUSWIN
    PopUsers++;
#endif

    /* Eat the banner */
    if ( mailresponse( s, buf, "banner" ) == -1
    || buf[0] == '-' ) {
		return (do_quit (s, wfp, np));
    }
 
	authokay = 0;
 
#ifdef MD5AUTHENTICATE
    if ((cp = strchr(buf,'<')) != NULLCHAR &&
        (cp1 = strrchr(buf,'>')) != NULLCHAR)
	{

        MD5_CTX md;

        *++cp1 = '\0';
        MD5Init(&md);
        MD5Update(&md,(unsigned char *)cp,strlen(cp));
        MD5Update(&md,(unsigned char *)np->password,strlen(np->password));
        MD5Final(&md);

        for(i=0, cp=buf; i<16; i++, cp+=2)
            sprintf(cp,"%02x",md.digest[i]);

        usprintf(s,"APOP %s %s\n", np->username, buf);
        if ( mailresponse( s, buf, "APOP" ) != -1 && buf[0] != '-' )
		{
            authokay = 1;
        }
    }  /* else fall into standard authentication code */
#endif /* MD5AUTHENTICATE */

	if (!authokay)
	{
    	usprintf(s,"USER %s\n", np->username);
    	if ( mailresponse( s, buf, "USER" ) == -1 || buf[0] == '-' )
		{
			return (do_quit (s, wfp, np));
    	}
  
    	usprintf(s,"PASS %s\n", np->password);
    	if ( mailresponse( s, buf, "PASS" ) == -1 || buf[0] == '-' )
		{
			return (do_quit (s, wfp, np));
    	}
	}

    usputs(s,"STAT\n" );
    if ( mailresponse( s, buf, "STAT" ) == -1 || buf[0] == '-' )
	{
		return (do_quit (s, wfp, np));
    }
  
    rip(buf);
    if ( Mailtrace >= 1 )
        log(s,"POP3 status %s",buf);
    sscanf(buf,"+OK %d %d",&messages,&bytes);
  
    if ((wfp = tmpfile()) == NULLFILE) {
        if ( Mailtrace >= 1 )
            log(s,"POP3 Cannot create %s", "tmp file" );
		return (do_quit (s, wfp, np));
    }
  
#if defined(LZW)
    if (poplzw && messages ) {
        usprintf(s,"XLZW %d %d\n",Lzwbits,Lzwmode);
        if ( mailresponse( s, buf, "XLZW" ) == 0
          && buf[0] != '-' ) {
            lzwmode = lzwbits = 0;
            sscanf(buf,"+OK lzw %d %d",&lzwbits,&lzwmode);
            if(lzwmode != Lzwmode || lzwbits != Lzwbits) {
                lzwmode = LZWCOMPACT;
                lzwbits = LZWBITS;
            }
            lzwinit(s,lzwbits,lzwmode);
        }
    }
#endif

#if defined(POP_MAX_MSGS) && POP_MAX_MSGS > 0
    if (messages > POP_MAX_MSGS) messages=POP_MAX_MSGS;
#endif
    for ( i = 0; i++ < messages; ) {
        usprintf(s,"RETR %d\n", i);
        if ( mailresponse( s, buf, "RETR" ) == -1 )
			return (do_quit (s, wfp, np));
        if ( buf[0] == '-' ) {
            continue;
        }
  
        time(&t);
        fprintf( wfp, "From POP3@%s %s", np->hostname, ctime(&t));
        /* Add a 'Received by' line for our system, such that
         * USERLOG and mail-index code works properly - WG7J
         */
        fprintf(wfp, "%s", Hdrs[RECEIVED]);	/* 05Jul2016, Maiko, compiler */
        fprintf(wfp,"by %s (%s) with POP3\n\tid AA%ld ; %s",
        Hostname, shortversion, get_msgid(), ptime(&t));
  
  
        if ( recvmail(s, buf, TLINELEN, wfp, Mailtrace) == -1 ) {
			return (do_quit (s, wfp, np));
        }
  
        usprintf(s,"DELE %d\n", i);
        if ( mailresponse( s, buf, "DELE" ) == -1
        || buf[0] == '-' ) {
			return (do_quit (s, wfp, np));
        }
    }
  
    if ( messages == 0 ) {
        /* Quit for politeness sake */
        usputs(s,"QUIT\n" );
        mailresponse( s, buf, "QUIT" );
    } else if ( copymail( np->mailbox, buf, TLINELEN, wfp, Mailtrace ) != -1 ) {
        /* Quit command allows the deletions to complete */
        usputs(s,"QUIT\n" );
        mailresponse( s, buf, "QUIT" );
  
        if (Mailtrace)
            tprintf("New mail arrived for %s from mailhost %s%c\n",
                np->mailbox, np->hostname,
                Mailquiet ? ' ' : '\007');
    }
  
	return (do_quit (s, wfp, np));
}
  
#endif
