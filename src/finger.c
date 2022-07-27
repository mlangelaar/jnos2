/* Internet finger client
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "netuser.h"
#include "commands.h"
#include "tty.h"
  
int
dofinger(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct sockaddr_in sock;
    char *cp;
    int s,i;
    struct mbuf *bp;
    struct session *sp;
  
    /*Make sure this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;
  
    /* Allocate a session descriptor */
    if((sp = newsession(argv[1],FINGER,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        keywait(NULLCHAR,1);
        return 1;
    }
    sp->ttystate.echo = sp->ttystate.edit = 0;
    sp->flowmode = 1;
    sock.sin_family = AF_INET;
    sock.sin_port = IPPORT_FINGER;
    for(i=1;i<argc;i++){
        cp = strchr(argv[i],'@');
        if(cp == NULLCHAR){
            tprintf("%s@localhost -- ",argv[i]);
            sock.sin_addr.s_addr = 0x7f000001UL;  /* 127.0.0.1 */
        } else {
            *cp++ = '\0';
            tprintf("%s@%s -- ",argv[i],cp);
            tprintf("Resolving %s... ",cp);
            if((sock.sin_addr.s_addr = resolve(cp)) == 0){
                tprintf("Host %s unknown\n",cp);
                continue;
            }
        }
        tprintf("trying %s",psocket((struct sockaddr *)&sock));
        if((sp->s = s = j2socket(AF_INET,SOCK_STREAM,0)) == -1){
            j2tputs(Nosock);
            break;
        }
        sockmode(s,SOCK_ASCII);
        if(j2connect(s,(char *)&sock,sizeof(sock)) == -1){
            cp = sockerr(s);
            tprintf(" -- Connect failed: %s\n",cp != NULLCHAR ? cp : "");
            j2shutdown(s,2);  /* K2MF: To make sure it doesn't linger around */
            close_s(s);
            sp->s = -1;
            continue;
        }
        tputc('\n');
        usputs(s,argv[i]);
        usputc(s,'\n');
        usflush(Curproc->output);
        while(recv_mbuf(s,&bp,0,NULLCHAR,(int *)0) > 0){
            send_mbuf(Curproc->output,bp,0,NULLCHAR,0);
        }
        close_s(s);
        sp->s = -1;
    }
    keywait(NULLCHAR,1);
    freesession(sp);
    return 0;
}
