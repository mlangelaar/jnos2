/*
 * JNOS 2.0
 *
 * $Id: mboxfile.c,v 1.3 2012/03/20 16:20:44 ve4klm Exp $
 *
 * These are the mailbox FILE commands
 * These lines MUST come before global.h.  Otherwise we get symbol clashes.
 *
 * Mods by VE4KLM
 */
#include <time.h>
#include <ctype.h>
#ifdef  UNIX
#include <sys/types.h>
#include <sys/stat.h>
#endif
#include "global.h"
#ifdef FILECMDS
#ifdef MSDOS
#include <alloc.h>
#endif
#include "timer.h"
#include "proc.h"
#include "socket.h"
#include "usock.h"
#include "session.h"
#include "smtp.h"
#include "dirutil.h"
#include "telnet.h"
#include "ftp.h"
#include "ftpserv.h"
#include "commands.h"
#include "netuser.h"
#include "files.h"
#include "bm.h"
#include "pktdrvr.h"
#include "ax25.h"
#include "mailbox.h"
#include "ax25mail.h"
#include "nr4mail.h"
#include "cmdparse.h"
#include "mailfor.h"
  
extern char Noperm[];
  
/* uuencode a file -- translated from C++; both versions copyright 1990
   by David R. Evans, G4AMJ/NQ0I
*/
  
int
uuencode(infile,s,infilename)
FILE *infile;
int s;                  /* output socket */
char *infilename;
{
    int n_read_so_far = 0, n_written_so_far = 0, in_chars, n, mode = 0755;
    unsigned long cnt = 0;
    unsigned char in[3], out[4], line[100];
#ifdef UNIX
    struct stat stb;
  
    if(stat(infilename,&stb) != -1)
        mode = stb.st_mode & 0777;       /* get real file protection mode */
#endif
    usprintf(s, "begin %03o %s\n", mode, infilename);
  
  /* do the encode */
    for(;;){
        in_chars = fread(in, 1, 3, infile);
        out[0] = in[0] >> 2;
        out[1] = in[0] << 6;
        out[1] = out[1] >> 2;
        out[1] = out[1] | (in[1] >> 4);
        out[2] = in[1] << 4;
        out[2] = out[2] >> 2;
        out[2] = out[2] | (in[2] >> 6);
        out[3] = in[2] << 2;
        out[3] = out[3] >> 2;
        for (n = 0; n < 4; n++)
            out[n] += ' ';
        n_read_so_far += in_chars;
        for (n = 0; n < 4; n++)
            line[n_written_so_far++] = out[n];
        if (((in_chars != 3) || (n_written_so_far == 60)) && n_read_so_far > 0) {
            line[(n_read_so_far + 2) / 3 * 4] = '\0';
  
            usprintf(s,"%c%s\n",n_read_so_far + ' ', line);
            cnt += n_read_so_far;
            n_read_so_far = 0;
            n_written_so_far = 0;
        }
        if (in_chars == 0)
            break;
    }
    if (usprintf(s," \nend\nsize %lu\n", cnt) == EOF)
        return 1;
    return 0;
}
  
int
dodownload(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    FILE *fp;
    char *file,*path;
  
    m = (struct mbx *)p;
  
    if (m->stype=='M') {  /* DM => download motd, no need to check access */
        file = j2strdup(Motdfile);
    }
    else {      
        /* build the full pathname for the file */
        if (argc != 2) {
#ifdef XMODEM
            j2tputs("Usage: D[U|X] <filename>\n");
#else
            j2tputs("Usage: D[U] <filename>\n");
#endif
            return 0;
        }
        else {
            path = firstpath(m->path);
            file = pathname(path,argv[1]);
            free(path);
  
            if(!permcheck(m->path,RETR_CMD,file)){
                j2tputs(Noperm);
#ifdef MAILERROR
                mail_error("%s: download denied: %s\n",m->name,cmd_line(argc,argv,m->stype));
#endif
                free(file);
                return 0;
            }
        }
    }
#if defined(TIPSERVER) && defined(XMODEM)
    if (m->stype=='X') {
        if (m->type==TIP_LINK){   /* xmodem send tip only */
            m->state = MBX_XMODEM_TX;
            /* disable the mbox inactivity timeout timer - WG7J */
            j2alarm(0);
            doxmodem('S',file,m);
        } else {
            j2tputs("Xmodem on TIP connects only\n");
        }
    }
    else
#endif
    {
        m->state = MBX_DOWNLOAD;
        if((fp = fopen(file,READ_TEXT)) == NULLFILE)
            tprintf("Can't open \"%s\": %s\n",file,strerror(errno));
        else {
            /* disable the mbox inactivity timeout timer - WG7J */
            j2alarm(0);
            if(m->stype == 'U'){            /* uuencode ? */
                fclose(fp);
                fp = fopen(file,READ_BINARY);   /* assume non-ascii */
                uuencode(fp,m->user,file);
            } else
                sendfile(fp,m->user,ASCII_TYPE,0,NULL);
            fclose(fp);
        }
    }
    free(file);
    return 0;
}
  
int
dombupload(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    FILE *fp;
    char *file,*path;
  
    m = (struct mbx *)p;
  
    /* build the full pathname for the file */
    path = firstpath(m->path);
    file = pathname(path,argv[1]);
    free(path);
  
    if(!permcheck(m->path,STOR_CMD,file)){
        j2tputs(Noperm);
#ifdef MAILERROR
        mail_error("%s: upload denied: %s\n",m->name,cmd_line(argc,argv,m->stype));
#endif
    }
#ifdef TIPSERVER
#ifdef XMODEM
    else if (m->stype=='X'){
        if (m->type==TIP_LINK){   /* xmodem receive tip only */
            m->state = MBX_XMODEM_RX;
            /* disable the mbox inactivity timeout timer - WG7J */
            j2alarm(0);
            doxmodem('R',file,m);
        } else {
            j2tputs("Xmodem on TIP connects only\n");
        }
    }
#endif
#endif
    else {  
        if((fp = fopen(file,WRITE_TEXT)) == NULLFILE)
            tprintf("Can't create \"%s\": %s\n",file,strerror(errno));
        else {
            log(m->user,"MBOX upload: %s",file);
            m->state = MBX_UPLOAD;
            tprintf("Send file,  %s",Howtoend);
            for(;;){
                if(mbxrecvline(m) == -1){
                    unlink(file);
                    break;
                }
                if(*m->line == 0x01){  /* CTRL-A */
                    unlink(file);
                    j2tputs(MsgAborted);
                    break;
                }
                if(*m->line == CTLZ || !stricmp("/ex",m->line))
                    break;
                fputs(m->line,fp);
#if !defined(UNIX) && !defined(__TURBOC__) && !defined(AMIGA)
                /* Needed only if the OS uses a CR/LF
                 * convention and putc doesn't do
                 * an automatic translation
                 */
                if(putc('\r',fp) == EOF)
                    break;
#endif
                if(putc('\n',fp) == EOF)
                    break;
            }
            fclose(fp);
        }
    }
    free(file);
    return 0;
}

#ifdef WPAGES  
extern int dombwpages (int argc, char **argv, void *p);
#endif

int dowhat (int argc, char **argv, void *p)
{
    struct mbx *m;
    FILE *fp;
    char *file,*path;
  
    m = (struct mbx *)p;

#ifdef WPAGES
    /* 02Feb2012, Maiko, In the TNOS 240 source, this is in mailbox2.c */
    if (m->stype == 'P')
        return (dombwpages (argc, argv, p));
#endif

    path = firstpath(m->path);

    if(argc < 2)
        file = j2strdup(path);
    else
        file = pathname(path,argv[1]);
    free(path);
  
    if(!permcheck(m->path,RETR_CMD,file)){
        j2tputs(Noperm);
#ifdef MAILERROR
        mail_error("%s: directory denied: %s\n",m->name,cmd_line(argc,argv,m->stype));
#endif
    }
    else {
        m->state = MBX_WHAT;
        if((fp = dir(file,1)) == NULLFILE)
            tprintf("Can't read directory: \"%s\": %s\n",file,strerror(errno));
        else {
            j2alarm(0);
            sendfile(fp,m->user,ASCII_TYPE,0,NULL);
            fclose(fp);
        }
    }
    free(file);
    return 0;
}
  
int
dozap(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    struct mbx *m;
    char *file,*path;
  
    m = (struct mbx *)p;
  
    path = firstpath(m->path);
    file = pathname(path,argv[1]);
    free(path);
  
  
    if(!permcheck(m->path,DELE_CMD,file)){
        j2tputs(Noperm);
#ifdef MAILERROR
        mail_error("%s: zap denied: %s\n",m->name,cmd_line(argc,argv,m->stype));
#endif
    }
    else {
        if(unlink(file))
            tprintf("Zap failed: %s\n",strerror(errno));
        else log(m->user,"MBOX Zap: %s",file);
    }
    free(file);
    return 0;
}
  
#endif /* FILECMDS */
