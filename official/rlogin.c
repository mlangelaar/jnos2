#include "global.h"
#ifdef RLOGINSESSION
#include "mbuf.h"
#include "socket.h"
#include "session.h"
#include "proc.h"
#include "tty.h"
#include "commands.h"
#include "netuser.h"
  
static char *username = "guest";
static char terminal[] = "ansi";
static char *termspeed = "/38400";
  
extern FILE *Rawterm;
int rlo_connect __ARGS((struct session *sp,char *fsocket,int len));
void rlo_output __ARGS((int unused,void *p1,void *p2));
void rlrecv __ARGS((struct session *sp));
  
/* Execute user rlogin command */
int
dorlogin(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
    struct sockaddr_in fsocket;
    struct sockaddr_in lsocket;
  
    /*Make sure this comes from console - WG7J*/
    if(Curproc->input != Command->input)
        return 0;
  
    /* Allocate a session descriptor */
    if((sp = newsession(argv[1],RLOGIN,0)) == NULLSESSION){
        j2tputs(TooManySessions);
        return 1;
    }
    fsocket.sin_family = AF_INET;
    if(argc < 3)
        fsocket.sin_port = IPPORT_RLOGIN;
    else
        fsocket.sin_port = atoi(argv[2]);
  
    tprintf("Resolving %s... ",sp->name);
    if((fsocket.sin_addr.s_addr = resolve(sp->name)) == 0L){
        tprintf(Badhost,sp->name);
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
    if((sp->s = j2socket(AF_INET,SOCK_STREAM,0)) == -1){
        j2tputs(Nosock);
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
    lsocket.sin_family = AF_INET;
    lsocket.sin_addr.s_addr = INADDR_ANY;
    lsocket.sin_port = IPPORT_RLOGIN;
    j2bind(sp->s,(char *)&lsocket,sizeof(lsocket));
    return rlo_connect(sp,(char *)&fsocket,SOCKSIZE);
}
/* Generic interactive connect routine */
int
rlo_connect(sp,fsocket,len)
struct session *sp;
char *fsocket;
int len;
{
    unsigned int index;
  
    index = (unsigned int)(sp - Sessions);
  
    sockmode(sp->s,SOCK_ASCII);
    tprintf("Trying %s...\n",psocket((struct sockaddr *)fsocket));
    if(j2connect(sp->s,fsocket,len) == -1){
        tprintf("%s session %u failed: %s errno %d\n",
        Sestypes[sp->type], index, sockerr(sp->s),errno);
  
        keywait(NULLCHAR,1);
        freesession(sp);
        return 1;
    }
    tprintf("%s session ",Sestypes[sp->type]);
    tprintf("%u connected to %s\n",index,sp->name);
    rlrecv(sp);
    return 0;
}
  
/* Rlogin input routine, common to both rlogin and ttylink */
void
rlrecv(sp)
struct session *sp;
{
    int c,s,index;
    char *cp;
  
    s = sp->s;
  
    /* We run both the network and local sockets in transparent mode
     * because we have to do our own eol mapping
     */
    seteol(s,"");
    seteol(Curproc->input,"");
    seteol(Curproc->output,"");
  
    /* Read real keystrokes from the keyboard */
    sp->ttystate.crnl = 0;
    /* Put tty into raw mode */
    sp->ttystate.echo = 0;
    sp->ttystate.edit = 0;
  
    j2setflush(s,'\n');
  
    index = (unsigned int)(sp - Sessions);
  
    /* Fork off the transmit process */
    sp->proc1 = newproc("rlo_out",1024,rlo_output,0,sp,NULL,0);
  
    /* Process input on the connection */
    while((c = recvchar(s)) != -1){
#ifdef UNIX
        putch((char) c);
#else
        putc((char)c,Rawterm);
#endif
    }
    /* A close was received from the remote host.
     * Notify the user, kill the output task and wait for a response
     * from the user before freeing the session.
     */
    cp = sockerr(s);
    seteol(s,"\r\n");
    seteol(Curproc->input,"\r\n");
    seteol(Curproc->output,"\r\n");
  
    tprintf("%s session %u", Sestypes[sp->type],index);
    tprintf(" closed: %s\n", cp != NULLCHAR ? cp : "EOF");
    killproc(sp->proc1);
    sp->proc1 = NULLPROC;
    close_s(sp->s);
    sp->s = -1;
    keywait(NULLCHAR,1);
    freesession(sp);
}
  
/* User rlogin output task, started by user rlogin command */
void
rlo_output(unused,sp1,p)
int unused;
void *sp1;
void *p;
{
    struct session *sp;
    struct mbuf *bp;
    char *cp;
    char *logname, *termname;
    sp = (struct session *)sp1;
  
    logname = getenv("USER");
    if(logname == NULLCHAR)
        logname = username;
    termname = getenv("TERM");
    if(termname == NULLCHAR)
        termname = terminal;
    bp = ambufw(1 + strlen(logname)+1 + strlen(logname)+1 +
    strlen(termname) + strlen(termspeed)+1);
  
    cp = bp->data;
    *cp++ = '\0';
    strcpy(cp, logname);
    cp += strlen(logname) + 1;
    strcpy(cp, logname);
    cp += strlen(logname) + 1;
    strcpy(cp, termname);
    cp += strlen(termname);
    strcpy(cp, termspeed);
    cp += strlen(termspeed) + 1;
    bp->cnt = (int16)(cp - bp->data);
    if(send_mbuf(sp->s,bp,0,NULLCHAR,0) != -1){
        /* Send whatever's typed on the terminal */
        while(recv_mbuf(sp->input,&bp,0,NULLCHAR,0) > 0){
            if(send_mbuf(sp->s,bp,0,NULLCHAR,0) == -1)
                break;
        }
    }
    /* Make sure our parent doesn't try to kill us after we exit */
    sp->proc1 = NULLPROC;
}
#endif /* RLOGINSESSION */
  
