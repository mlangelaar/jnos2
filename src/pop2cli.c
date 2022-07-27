/* POP2 Client routines -- Not recommended.
 *      Jan 92  William Allen Simpson
 *              complete re-write to match new mailreader commands
 *
 *      partly based on a NNTP client design by Anders Klemets, SM0RGV
 *      Originally authored by Mike Stockett (WA7DYX).
 *
 * History:
 *      Support for Mail Index Files, June/July 1993, Johan. K. Reinalda, WG7J
 *      Modified 12 May 1991 by Mark Edwards (WA6SMN) to use new timer
 *      facilities in NOS0423.  Fixed type mismatches spotted by C++.
 *      Modified 27 May 1990 by Allen Gwinn (N5CKP) for compatibility
 *        with later releases (NOS0522).
 *      Added into NOS by PA0GRI (and linted into "standard" C)
 *      Modified 14 June 1987 by P. Karn for symbolic target addresses,
 *        also rebuilt locking mechanism
 *
 *      Some code culled from previous releases of SMTP.
 *      See that code for applicable copyright notices.
 */
#include <time.h>
#include "global.h"
#ifdef POP2CLIENT
#include "timer.h"
#include "proc.h"
#include "netuser.h"
#include "socket.h"
#include "cmdparse.h"
#include "files.h"
#include "mailcli.h"
#include "mailutil.h"
#include "smtp.h"
  
extern char Badhost[];

#ifdef STATUSWIN
#if defined(POP2SERVER) || defined(POP3SERVER) || defined(POP3CLIENT)
extern
#endif
int PopUsers;
#endif // STATUSWIN
  
/* Response string keys */
static char *greeting_rsp  = "+ POP2 ";

/* 29Dec2004, Replace GOTO 'quit' and 'quitcmd' with functions */
static void do_quit (int s, FILE *wfp, struct mailservers *np)
{
    log(s,"POP2 daemon exiting");
    (void) close_s(s);
#ifdef STATUSWIN
    PopUsers--;
#endif
    if (wfp != NULLFILE)
        fclose(wfp);
    start_timer(&np->timer);
}

static void do_quitcmd (int s, FILE *wfp, struct mailservers *np)
{
    (void)usputs(s,"QUIT\n");
	return (do_quit (s, wfp, np));
}
  
void
pop2_job(unused,v1,p2)
int unused;
void *v1;
void *p2;
{
    struct mailservers *np = v1;
    struct sockaddr_in fsocket;
    char buf[TLINELEN];
    char *cp;
    FILE *wfp = NULLFILE;
    int s = -1;
    int folder_len;
    int msg_num = 0;
    time_t t;
  
    if ( mailbusy( np ) )
        return;
  
    if ( (fsocket.sin_addr.s_addr = resolve(np->hostname)) == 0L ) {
        /* No IP address found */
        if (Mailtrace >= 1)
            log(-1,"POP2 can't resolve host '%s'", np->hostname);
        start_timer(&np->timer);
        return;
    }
  
    fsocket.sin_family = AF_INET;
    fsocket.sin_port = IPPORT_POP2;
  
    s = j2socket(AF_INET,SOCK_STREAM,0);
    sockmode(s,SOCK_ASCII);
  
    if (j2connect(s,(char *)&fsocket,SOCKSIZE) == -1) {
        cp = sockerr(s);
        if (Mailtrace >= 2)
            log(s,"POP2 Connect failed: %s",
            cp != NULLCHAR ? cp : "");
#ifdef STATUSWIN
	PopUsers++;			// because it gets decremented at quit
#endif
	return (do_quit (s, wfp, np));
    }
  
    log(s,"POP2 Connected to mailhost %s", np->hostname);
#ifdef STATUSWIN
    PopUsers++;
#endif
  
    if ( mailresponse( s, buf, "banner" ) == -1)
	return (do_quit (s, wfp, np));
  
    if (strncmp(buf,greeting_rsp,strlen(greeting_rsp)) != 0)
		return (do_quitcmd (s, wfp, np));
  
    (void)usprintf(s,"HELO %s %s\n",np->username,np->password);
  
    if ( mailresponse( s, buf, "HELO" ) == -1)
	return (do_quit (s, wfp, np));
  
    if (buf[0] != '#'
    || (folder_len = atoi(&(buf[1]))) == 0) {
        /* If there is no mail (the only time we get a "+"
         * response back at this stage of the game),
         * then just close out the connection, because
         * there is nothing more to do!! */
		return (do_quitcmd (s, wfp, np));
    }
  
    if ((wfp = tmpfile()) == NULLFILE)
	{
        if ( Mailtrace >= 1 )
            log(s,"POP2 Cannot create %s", "tmp file" );
		return (do_quitcmd (s, wfp, np));
    }
  
    (void)usputs(s,"READ\n");
  
    /* now, get the text */
    while(TRUE) {
        long msg_len;
  
        if ( mailresponse( s, buf, "read loop" ) == -1)
			return (do_quit (s, wfp, np));
  
        if (buf[0] == '='
        && (msg_len = atol(&(buf[1]))) > 0) {
            (void)usputs(s,"RETR\n");
  
            /* for each message, add a 'Received by' line for our system.
             * Thus USERLOG and mail-index code works properly - WG7J
             */
            time(&t);
            fprintf( wfp, "From POP2@%s %s", np->hostname, ctime(&t));
            fprintf( wfp, Hdrs[RECEIVED]);
            fprintf( wfp, "by %s (%s) with POP2\n\tid AA%ld ; %s",
            Hostname, shortversion, get_msgid(), ptime(&t));
  
  
            while ( msg_len > 0 )
			{
                if (recvline(s,buf,TLINELEN) == -1)
					return (do_quit (s, wfp, np));
  
                rip(buf);
  
                if(!strncmp(buf,"From ",5))
                    fputc('>',wfp);
  
                fprintf(wfp,"%s\n",buf);
  
                msg_len -= (long)(strlen(buf)+2);/* Add CRLF */
            }
            (void)usputs(s,"ACKD\n");
  
            msg_num++;
        } else {
            break;
        }
    }
  
    if ( folder_len > 0 ) {
        /* testing the result is pointless,
         * since POP2 already deleted mail.
         */
        copymail( np->mailbox, buf, TLINELEN, wfp, Mailtrace );
  
        if (Mailtrace)
            tprintf("New mail arrived for %s from mailhost %s%c\n",
                np->mailbox, np->hostname,
                Mailquiet ? ' ' : '\007');
    }
  
	return (do_quitcmd (s, wfp, np));
}
  
#endif
  
