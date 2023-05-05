/* Peek into a user's actions on the bbs.
 * This allows you to follow all stuff, and also
 * send messages and initiate chats...
 * (C) 1994, Johan. K. Reinalda, WG7J
 */
#include <ctype.h>
#include "global.h"
#ifdef LOOKSESSION
#include "session.h"
#include "smtp.h"
#include "usock.h"
#include "socket.h"
#include "mailbox.h"
  
static void look_input __ARGS((int unused,void *p1,void *p2));
  
struct look {
    struct session *sp;     /* our look session */
    int user;               /* socket we look at */
};
  
int dolook(int argc, char *argv[],void *p) {
    struct look lk;
    struct usock *up;
    int chat;
    char *cp;
#ifdef MAILBOX
    struct mbx *m;
#endif
    char name[MBXNAME+1];
    char buf[MBXLINE];
	int werdone = 0;
  
    /* Check if this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;
  
#ifdef MAILBOX
    /* Find the user ! */
    lk.user = atoi(argv[1]);  /* store socket #, or illegal # if callsign */
    for(m=Mbox;m;m=m->next) {
        if(!stricmp(m->name,argv[1]))
            break;
        if(m->user == lk.user) break;
    }
    if(m) {
        lk.user = m->user;
        strcpy(name,m->name);
    } else {
#endif
        lk.user = atoi(argv[1]);
        sprintf(name,"socket %d",lk.user);
#ifdef MAILBOX
    }
#endif
  
    if((up = itop(lk.user)) == NULLUSOCK){
        j2tputs("User socket error!\n");
        return 0;
    }
    if(up->look) {
        tprintf("Already looking at %s\n",argv[1]);
        return 0;
    }
    if(lk.user == Curproc->input || lk.user == Curproc->output) {
        j2tputs("Can not look at myself!\n");
        return 0;
    }
    /* Now everything seems okay ! Get a session */
    if((lk.sp = newsession(name,LOOK,1)) == NULLSESSION) {
        j2tputs(TooManySessions);
        return 0;
    }
  
    up->look = Curproc;     /* Tell the socket to echo data to this process ! */
    chat = 0;
  
    tprintf("%s session %d looking at %s\n", Sestypes[lk.sp->type],lk.sp->num,argv[1]);
  
    /* Process whatever's typed on the terminal */
	memset(buf,0,MBXLINE);   /* Clear the input buffer */
	while(recvline(Curproc->input,buf,sizeof(buf)-1) >= 0)
	{
        if(buf[0] == '/')
		{
			werdone = 0;

            /* process commands */
            switch(tolower(buf[1]))
			{
                case 'h':
                case '?':
#ifdef MAILBOX
                    tprintf("<Cmds>: /c-chat /m-msg /q-quit\n");
#else
                    tprintf("<Cmds>: /q-quit\n");
#endif
                    break;
#ifdef MAILBOX
                case 'm':   /* Send a message to the user */
                    if(!m)
                    /* Not a mailbox user */
                        break;
                    cp = &buf[2];
                    if(buf[2] == ' ')
                        cp = &buf[3];
                    usprintf(lk.user,"<sysop>: %s",cp);
                    break;
                case 'c':   /* Initiate chat mode */
                    if(chat || !m)
                    /* Already in 'chat' mode or not a mailbox user */
                        break;
                    usputs(lk.user,"*** SYSOP Initiated CHAT.\n");
                    up->look = NULL;    /* Disable echoing in socket layer */
                /* Now we need to redirect the network input
                 * from the user's bbs process to the chat process
                 */
                    lk.sp->proc1 = newproc("CHAT Server",1024,look_input,0,
                    (void *)&lk,NULL,0);
                    chat = 1;
                    stop_timer(&(up->owner->alarm));    /* disallow mbox timeout */
                    break;
#endif
                case 'b':
                case 'q':   /* quit chat mode, or look mode */
#ifdef MAILBOX
                    if(chat)
					{
                        killproc(lk.sp->proc1);
                        lk.sp->proc1 = NULLPROC;
                        up->look = Curproc; /* Enable echoing in socket layer */
                        usputs(lk.user,"*** BACK in mailbox\n");
                        resume(up->owner);
                        start_timer(&(up->owner->alarm));    /* reallow mbox timeout */
                        chat = 0;
                    }
					else
#endif
                   		werdone = 1;
                    break;
            }
			if (werdone) /* replaces GOTO 'done', break out while loop */
				break;
        }
#ifdef MAILBOX
        else if(chat)
            usprintf(lk.user,"<sysop>: %s",buf);
#endif
  
        usflush(lk.user);
        usflush(Curproc->output);
        memset(buf,0,MBXLINE);   /* Clear the input buffer */
    }

    /* A 'close' command was given, or user disconnected.
     * Notify the user, kill the receiver input task and wait for a response
     * from the user before freeing the session.
     */
    cp = sockerr(lk.sp->input);
    tprintf("%s session %u closed: %s\n",
    Sestypes[lk.sp->type],lk.sp->num,
    cp != NULLCHAR ? cp : "EOF");
  
    if((up = itop(lk.user)) != NULLUSOCK)   /* Make sure socket is still there */
        up->look = NULL;
#ifdef MAILBOX
    if(lk.sp->proc1 != NULLPROC) {
        /* kill the receive process */
        killproc(lk.sp->proc1);
        lk.sp->proc1 = NULLPROC;
        if(up) {
            usputs(lk.user,"*** BACK in mailbox\n");
            usflush(lk.user);
            /* restart the mailbox process */
            resume(up->owner);
            start_timer(&(up->owner->alarm));    /* reallow mbox timeout */
        }
    }
#endif
    keywait(NULLCHAR,1);
    freesession(lk.sp);
    return 0;
}
  
#ifdef MAILBOX
/* Task that read the user's input socket (that formerly went to the socket
 * process), and sends it to the look session !
 */
void
look_input(unused,p1,p2)
int unused;
void *p1;
void *p2;
{
    struct session *sp;
    struct usock *up;
    int user,c;
  
    sp = ((struct look *)p1)->sp;
    user = ((struct look *)p1)->user;
  
    if((up=itop(user)) == NULLUSOCK) {
        /* Make sure our parent doesn't try to kill us after we exit */
        sp->proc1 = NULLPROC;
        return;
    }
  
    /* Suspend the process that owns the socket */
    suspend(up->owner);
  
    /* Process input on the users socket connection */
    while((c = recvchar(user)) != -1)
        tputc((char)c);
  
    /* Make sure our parent doesn't try to kill us after we exit */
    sp->proc1 = NULLPROC;
  
    /* Alert the parent, in case the chat was terminated by losing the
     * user connection. This in effect will close the look session
     */
    alert(sp->proc,ENOTCONN);
  
    /* Resume the process that owns the socket */
    resume(up->owner);
  
}
#endif /* MAILBOX */
  
#endif /* LOOKSESSION */
  
