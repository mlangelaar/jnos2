#include "global.h"
#ifdef CALLCLI
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "netuser.h"
#include "commands.h"
#include "tty.h"
  
extern char *Callserver;
  
int
docallbook(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct sockaddr_in sock;
    char *cp;
    int s,i;
    struct mbuf *bp;
    struct session *sp;
  
    /* Allocate a session descriptor */
    if((sp = newsession(argv[1],FINGER,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        keywait(NULLCHAR,1);
        return 1;
    }
    if(Callserver == NULLCHAR)  {
        j2tputs("Callbook Services Not Configured.\n");
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
  
    sp->ttystate.echo = sp->ttystate.edit = 0;
    sp->flowmode = 1;
    sock.sin_family = AF_INET;
    sock.sin_port = IPPORT_CALLDB;
    for(i=1;i<argc;i++){
        cp = Callserver;
        strupr(argv[i]);
        tprintf("Looking up \"%s\" in the callbook at %s\n",argv[i],cp);
        if((sock.sin_addr.s_addr = resolve(cp)) == 0){
            tprintf("Callserver %s unknown\n",cp);
            continue;
        }
        tprintf("trying %s",psocket((struct sockaddr *)&sock));
        if((sp->s = s = j2socket(AF_INET,SOCK_STREAM,0)) == -1){
            j2tputs("Can't create socket\n");
            break;
        }
        sockmode(s,SOCK_ASCII);
        if(j2connect(s,(char *)&sock,sizeof(sock)) == -1){
            cp = sockerr(s);
            tprintf(" -- Connect failed: %s\n",cp != NULLCHAR ? cp : "");
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
#endif /* CALLCLI */
