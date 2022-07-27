/* NOS User Session control
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Mods by PA0GRI
 */
#include "global.h"
#include "config.h"
#include "mbuf.h"
#include "proc.h"
#include "ip.h"
#include "ftpcli.h"
#include "icmp.h"
#include "telnet.h"
#include "tty.h"
#include "session.h"
#include "unix.h"
#include "socket.h"
#include "cmdparse.h"
#include "rlogin.h"
#include "commands.h"
#include "main.h"
#ifdef UNIX
#include "sessmgr.h"
#endif
  
struct session *Sessions;
struct session *Command;
struct session *Current;
struct session *Lastcurr;
extern struct session *Trace;
int Row;
int Morewait;
extern int Numrows;
extern int StatusLines;
  
char Notval[] = "Not a valid control block\n";
static char Badsess[] = "Invalid session\n";
char TooManySessions[] = "Too many sessions\n";
  
char *Sestypes[] = {
    "Command",
    "Telnet",
    "FTP",
    "AX25",
    "Finger",
    "Ping",
    "NET/ROM",
    "Command",
    "More",
    "Hopcheck",
    "Tip",
    "PPP PAP",
    "Dial",
    "Query",
    "Cache",
    "Rlogin",
    "Repeat",
    "Look",
    "Trace",
    "AXUI",
    "Edit",
    "NNTP"
};

/* Convert a character string containing a decimal session index number
 * into a pointer. If the arg is NULLCHAR, use the current default session.
 * If the index is out of range or unused, return NULLSESSION.
 */
struct session *
sessptr(cp)
char *cp;
{
    struct session *sp;
    int i;
  
    if(cp == NULLCHAR){
        sp = Lastcurr;
    } else {
        i = (unsigned)atoi(cp);
        if(i >= Nsessions)
            sp = NULLSESSION;
        else
            sp = &Sessions[i];
    }
    if(sp == NULLSESSION || sp->type == FREE)
        sp = NULLSESSION;
  
    return sp;
}
  
#ifdef SESSIONS
/* Select and display sessions */
int
dosession(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
    struct sockaddr fsocket;
    int i,k=0,s;
    int r,t;
    char *cp,swap;
    char *param[3];
  
    sp = (struct session *)p;
  
    if(argc > 1){
#ifdef MSDOS
        if(strncmpi(argv[1],"swap",strlen(argv[1])) == 0) {
            if(argc == 2) {
#ifdef EMS
                if(SwapMode == EMS_SWAP)
                    j2tputs("EMS\n");
                else
#endif
#ifdef XMS
                    if(SwapMode == XMS_SWAP)
                        j2tputs("XMS\n");
                    else
#endif
                        if(SwapMode == MEM_SWAP)
                            j2tputs("Memory\n");
                        else
                            j2tputs("Tmp files\n");
            } else {
#ifdef EMS
                if(*argv[2] == 'e' || *argv[2] == 'E') {
                    SwapMode = EMS_SWAP;
                } else
#endif
#ifdef XMS
                    if(*argv[2] == 'x' || *argv[2] == 'X') {
                        if(!ScreenSizeK)
                            ScreenSizeK = (ScreenSize / 1024) + 1;
                        SwapMode = XMS_SWAP;
                    } else
#endif
                        if(*argv[2] == 'f' || *argv[2] == 'F') {
                            SwapMode = FILE_SWAP;
                        } else {
                            SwapMode = MEM_SWAP;
                        }
            }
            return 0;
        }
#endif /* MSDOS */
        if((sp = sessptr(argv[1])) == NULLSESSION){
            tprintf("Session %s not active\n",argv[1]);
            return 1;
        }
        if(argc == 2){
            go(0,NULL,sp);
        } else {
            switch (*argv[2]) {
                case 'f':   /* flowmode */
                    param[0] = argv[2];
                    param[1] = argv[3];
                    param[2] = NULL;
                    setbool(&sp->flowmode,"Set flowmode on/off",argc-2,param);
                    break;
                case 's':   /* split */
                    param[0] = argv[2];
                    param[1] = argv[3];
                    param[2] = NULL;
                    setbool(&sp->split,"Set split on/off",argc-2,param);
#ifdef UNIX
#ifndef HEADLESS
                    sm_splitscreen(sp);
#endif
#endif /* UNIX */
#ifdef SPLITSCREEN
                    sp->row = Numrows - 1 - StatusLines;
                    if (sp->split)
                        sp->row -= 2;
#endif
                    break;
                default:
		    tprintf("usage:session # [flow [on/off]] | [split [on/off]]\n");
            }
        }
        return 0;
    }
    j2tputs(" #  S#  Sw Type     Rcv-Q Snd-Q State        Remote socket\n");
    for(sp=Sessions; sp < &Sessions[Nsessions];sp++){
        if(sp->type == FREE || sp->type == COMMAND || sp->type == TRACESESSION)
            continue;
  
        /* Rcv-Q includes output pending at the screen driver */
        r = socklen(sp->output,1);
        t = 0;
        cp = NULLCHAR;
        if((s = sp->s) != -1){
            i = SOCKSIZE;
/*            s = sp->s; */
            k = j2getpeername(s,(char *)&fsocket,&i);
            r += socklen(s,0);
            t = socklen(s,1);
            cp = sockstate(s);
        }
#ifdef MSDOS
#ifdef EMS
        if(sp->screen->stype == EMS_SWAP)
            swap = 'E';
        else
#endif
#ifdef XMS
            if(sp->screen->stype == XMS_SWAP)
                swap = 'X';
            else
#endif
                if(sp->screen->stype == FILE_SWAP)
                    swap = 'F';
                else
#endif
                    swap = 'M';
  
        tputc((Lastcurr == sp)?'*':' ');
        tprintf("%-3u", (unsigned)(sp - Sessions));
        tprintf("%-4d%c  %-8s%6d%6d %-13s",
        s,swap,Sestypes[sp->type],r,t,(cp != NULLCHAR) ? cp : "Limbo!");
        if(sp->name != NULLCHAR)
            tprintf("%s ",sp->name);
        if(sp->s != -1 && k == 0)
            tprintf("(%s)",psocket(&fsocket));
        tputc('\n');
  
        if(sp->type == FTP && (s = sp->cb.ftp->data) != -1){
            /* Display data channel, if any */
            i = SOCKSIZE;
            k = j2getpeername(s,(char *)&fsocket,&i);
            r = socklen(s,0);
            t = socklen(s,1);
            cp = sockstate(s);
            tprintf("    %-4d   %-8s%6d%6d %-13s%s",
            s,Sestypes[sp->type],r,t,
            (cp != NULLCHAR) ? cp : "Limbo!",
            (sp->name != NULLCHAR) ? sp->name : "");
            if(k == 0)
                tprintf(" (%s)",psocket(&fsocket));
            if(tputc('\n') == EOF)
                break;
        }
        if(sp->rfile != NULLCHAR)
            tprintf("    Record: %s\n",sp->rfile);
        if(sp->ufile != NULLCHAR)
            tprintf("    Upload: %s\n",sp->ufile);
    }
    return 0;
}
#endif

/* Resume current session, and wait for it */
int
go(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
  
    sp = (struct session *)p;
    if(sp == NULLSESSION || sp->type == FREE || \
        sp->type == COMMAND || sp->type == TRACESESSION)
        return 0;
    Current = sp;
#ifndef HEADLESS
    swapscreen(Command,sp);
#ifdef STATUSWIN
    UpdateStatus();
#endif
#endif
    j2psignal(sp,0);
    return 0;
}
int
doclose(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
  
    sp = (struct session *)p;
    if(argc > 1)
        sp = sessptr(argv[1]);
  
    if(sp == NULLSESSION){
        j2tputs(Badsess);
        return -1;
    }
    j2shutdown(sp->s,1);
    if(sp->type == MORE || sp->type == LOOK)
        alert(sp->proc,EABORT);
    return 0;
}
int
doreset(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
  
    sp = (struct session *)p;
    if(argc > 1)
        sp = sessptr(argv[1]);
  
    if(sp == NULLSESSION){
        j2tputs(Badsess);
        return -1;
    }
    /* Unwedge anyone waiting for a domain resolution, etc */
    alert(sp->proc,EABORT);
    alert(sp->proc1,EABORT);
    j2shutdown(sp->s,2);
    if(sp->type == FTP)
        j2shutdown(sp->cb.ftp->data,2);
    return 0;
}
int
dokick(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
  
    sp = (struct session *)p;
    if(argc > 1)
        sp = sessptr(argv[1]);
  
    if(sp == NULLSESSION){
        j2tputs(Badsess);
        return -1;
    }
    sockkick(sp->s);
    if(sp->type == FTP)
        sockkick(sp->cb.ftp->data);
    return 0;
}

#ifdef HEADLESS 
struct session *sm_newsession(char *sc, char *name, int type, int split)
{
#define MASK_SPLIT 1
#define MASK_NOSWAP 2
    /*extern struct session *Trace;*/
    struct session *sp;
    int i;

    if (!sc || !*sc)
    {
	/*
	 * HACK - new sessions should inherit the current session if it is a
	 * COMMAND session.  Else use Command's next session, unless Command
	 * doesn't exist yet (creation of Command or Trace) in which case the
	 * default session manager is used.
	 *
	 * Maybe this isn't really a hack... but it feels like one.
	 */
	if (Current && Current->screen && Current->type == COMMAND)
	    sc = Current->screen->next_sm;
	else if (Command && Command->screen)
	    sc = Command->screen->next_sm;
	else
	    sc = "";
    }
    if (type == TRACESESSION)
	i = Nsessions - 1;
    else
    {
	for (i = 0; i < Nsessions; i++)
	{
	    if (Sessions[i].type == FREE)
		break;
	}
    }
    if (i == Nsessions)
	return NULLSESSION;
    sp = Sessions + i;

    sp->curdirs = NULL;
    sp->num = i;
    sp->type = type;
    sp->s = -1;
    if (name != NULLCHAR)
	sp->name = j2strdup(name);
    sp->proc = Curproc;
    Curproc->session = sp;  /* update Curproc's session pointer as well! */
    /* Create standard input and output sockets. Output is
     * translated to local end-of-line by default
     */

    /* close_s(Curproc->input); [not in sm_newsession!] */
    Curproc->input =  sp->input = j2socket(AF_LOCAL,SOCK_STREAM,0);
    seteol(Curproc->input,Eol);
    sockmode(Curproc->input,SOCK_BINARY);
        /* close_s(Curproc->output); [not in sm_newsession!] */
        Curproc->output = sp->output = j2socket(AF_LOCAL,SOCK_STREAM,0);
        seteol(Curproc->output,Eol);
        sockmode(Curproc->output,SOCK_ASCII);

#ifndef HEADLESS
    /* on by default */
    sp->ttystate.crnl = sp->ttystate.edit = sp->ttystate.echo = 1;
    sp->flowmode = 0;	/* Off by default */
    sp->morewait = 0;
    sp->row = Numrows - 1 - StatusLines;
#ifdef SPLITSCREEN
    if ((sp->split = split&MASK_SPLIT) != 0)
        sp->row -= 2;   /* but suppose the sm doesn't support split? */
#else
    sp->split = split&MASK_SPLIT;
#endif
    sm_newscreen(sc, sp);
    if (!(split&MASK_NOSWAP)) {   /* screen swap enabled? */
        swapscreen(Current,sp);
        Current = sp;
    }
#ifdef STATUSWIN
    UpdateStatus();
#endif
#else
        Current = sp;
#endif
    return sp;
}
#endif 

struct session *newsession(name,type,split)
char *name;
int type;
int split;
#define MASK_SPLIT 1
#define MASK_NOSWAP 2
{
    struct session *sp;
    int i;
  
#ifndef UNIX
    /* Make sure we have enough memory to swap the old session out - WG7J */
    if(availmem() < Memthresh/2 + ScreenSize)
        return NULLSESSION;
#endif
  
    /* Reserve the highest session for Trace, so that
     * the F-key session switching works correctly - WG7J
     */
    if (type == TRACESESSION)
        i = Nsessions - 1;
    else {
        for (i = 0; i < Nsessions; i++) {
            if (Sessions[i].type == FREE)
                break;
        }
    }
    if (i == Nsessions)
        return NULLSESSION;
    sp = Sessions + i;
  
    sp->curdirs = NULL;
    sp->num = i;
    sp->type = type;
    sp->s = -1;
    sp->name = (name == NULLCHAR) ? NULLCHAR : j2strdup(name);
        
    sp->proc = Curproc;

    /* update Curproc's session pointer as well! */
    Curproc->session = sp;

    /* Create standard input and output sockets. Output is
     * translated to local end-of-line by default
     */
    if(type != TRACESESSION) {
        close_s(Curproc->input);
        Curproc->input =  sp->input = j2socket(AF_LOCAL,SOCK_STREAM,0);
        seteol(Curproc->input,Eol);
        sockmode(Curproc->input,SOCK_BINARY);
        close_s(Curproc->output);
        Curproc->output = sp->output = j2socket(AF_LOCAL,SOCK_STREAM,0);
        seteol(Curproc->output,Eol);
        sockmode(Curproc->output,SOCK_ASCII);
    } else
        sp->input = sp->output = -1;
#ifdef TRACE
    if(type == COMMAND && Trace) {
#ifdef UNIX
        log(sp->input,"session: Program error: replacing trace i/o with %d/%d",
	    sp->input, sp->output);   /* UNIX code should never visit here */
#endif
        Trace->input = sp->input;
        Trace->output = sp->output;
    }
#endif
 
#ifndef HEADLESS
    /* on by default */
    sp->ttystate.crnl = sp->ttystate.edit = sp->ttystate.echo = 1;
    sp->flowmode = 0;   /* Off by default */
    sp->morewait = 0;
    sp->row = Numrows - 1 - StatusLines;
#ifdef SPLITSCREEN
    if ((sp->split = split&MASK_SPLIT) != 0)
        sp->row -= 2;
#else
    sp->split = split&MASK_SPLIT;
#endif
    newscreen(sp);
    if (!(split&MASK_NOSWAP)) {   /* screen swap enabled? */
        swapscreen(Current,sp);
        Current = sp;
    }
#ifdef STATUSWIN
    UpdateStatus();
#endif
#else	/* else HEADLESS */
    Current = sp;
#endif	/* endif HEADLESS */
    return sp;
}
void
freesession(sp)
struct session *sp;
{
    if(sp == NULLSESSION)
        return;
    pwait(NULL);    /* Wait for any pending output to go */
#ifndef HEADLESS
    rflush();
#endif
    killproc(sp->proc1);  /* OK if NULLPROC */
    sp->proc1 = NULLPROC;
    killproc(sp->proc2);  /* OK if NULLPROC */
    sp->proc2 = NULLPROC;
  
    free_p(sp->ttystate.line);
    sp->ttystate.line = NULLBUF;
    if(sp->s != -1)
        close_s(sp->s);
    if(sp->record != NULLFILE){
        fclose(sp->record);
        sp->record = NULLFILE;
    }
    free(sp->rfile);
    sp->rfile = NULLCHAR;
    if(sp->upload != NULLFILE){
        fclose(sp->upload);
        sp->upload = NULLFILE;
    }
    free(sp->ufile);
    sp->ufile = NULLCHAR;
    free(sp->name);
    sp->name = NULLCHAR;
    sp->type = FREE;
  
    close_s(sp->input);
    sp->input = -1;
    sp->proc->input = -1;
    close_s(sp->output);
    sp->output = -1;
    sp->proc->output = -1;
#ifndef HEADLESS 
    freescreen(sp);
    if(Current == sp){
        Current = Command;
        swapscreen(NULLSESSION,Command);
#ifdef STATUSWIN
        UpdateStatus();
#endif
#ifndef UNIX
        alert(Display,1);
#endif
    }
#else	/* else HEADLESS */
	if (Current == sp)
		Current = Command;
#endif	/* endif HEADLESS */

    /* reparent process to command session */
    sp->proc->session = Command;

    if(Lastcurr == sp)
        Lastcurr = NULLSESSION;
}
#ifdef ALLCMD
/* Control session recording */
int
dorecord(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct session *sp;
    char *mode;
  
    sp = (struct session *)p;
    if(sp == NULLSESSION){
        j2tputs("No current session\n");
        return 1;
    }
    if(argc > 1){
        if(sp->rfile != NULLCHAR){
            fclose(sp->record);
            free(sp->rfile);
            sp->record = NULLFILE;
            sp->rfile = NULLCHAR;
        }
        /* Open new record file, unless file name is "off", which means
         * disable recording
         */
        if(strcmp(argv[1],"off") != 0){
            if(sockmode(sp->output,-1) == SOCK_ASCII)
                mode = APPEND_TEXT;
            else
                mode = APPEND_BINARY;
  
            if((sp->record = fopen(argv[1],mode)) == NULLFILE)
                tprintf("Can't open %s: %s\n",argv[1],strerror(errno));
            else
                sp->rfile = j2strdup(argv[1]);
        }
    }
    if(sp->rfile != NULLCHAR)
        tprintf("Recording into %s\n",sp->rfile);
    else
        j2tputs("Recording off\n");
    return 0;
}
/* Control file transmission */
int
doupload(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    register struct session *sp;
  
    sp = (struct session *)p;
    if(sp == NULLSESSION){
        j2tputs("No current session\n");
        return 1;
    }
    if(argc < 2){
        if(sp->ufile != NULLCHAR)
            tprintf("Uploading %s\n",sp->ufile);
        else
            j2tputs("Uploading off\n");
        return 0;
    }
    if(strcmp(argv[1],"stop") == 0 && sp->upload != NULLFILE){
        /* Abort upload */
        fclose(sp->upload);
        sp->upload = NULLFILE;
        free(sp->ufile);
        sp->ufile = NULLCHAR;
        killproc(sp->proc2);
        sp->proc2 = NULLPROC;
        return 0;
    }
    /* Open upload file */
    if((sp->upload = fopen(argv[1],READ_TEXT)) == NULLFILE){
        tprintf("Can't read %s: %s\n",argv[1],strerror(errno));
        return 1;
    }
    sp->ufile = j2strdup(argv[1]);
    /* All set, invoke the upload process */
    sp->proc2 = newproc("upload",1024,upload,0,sp,NULL,0);
    return 0;
}
/* File uploading task */
void
upload(unused,sp1,p)
int unused;
void *sp1;
void *p;
{
    struct session *sp;
    int oldf;
    char *buf;
  
    sp = (struct session *)sp1;
  
    /* Disable newline buffering for the duration */
    oldf = j2setflush(sp->s,-1);
  
    buf = mallocw(BUFSIZ);
    while(fgets(buf,BUFSIZ,sp->upload) != NULLCHAR)
        if(usputs(sp->s,buf) == EOF)
            break;
  
    free(buf);
    usflush(sp->s);
    j2setflush(sp->s,oldf);
    fclose(sp->upload);
    sp->upload = NULLFILE;
    free(sp->ufile);
    sp->ufile = NULLCHAR;
    sp->proc2 = NULLPROC;
}
#endif /*ALLCMD*/
