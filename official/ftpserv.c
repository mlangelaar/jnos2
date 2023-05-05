/* Internet FTP Server server machine - see RFC 959
 * Copyright 1991 Phil Karn, KA9Q
 */
#include <ctype.h>
#include <time.h>
#ifdef  __TURBOC__
#include <io.h>
#include <dir.h>
#endif
#ifdef UNIX
#include <sys/types.h>
#include <sys/stat.h>
#endif
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "ftp.h"
#include "ftpserv.h"
#include "proc.h"
#include "dirutil.h"
#include "files.h"
#include "commands.h"
#include "config.h"
#include "cmdparse.h"
#include "mailutil.h"
#ifdef  LZW
#include "lzw.h"
#endif
  
static void ftpserv __ARGS((int s,void *unused,void *p));
static int pport __ARGS((struct sockaddr_in *sock,char *arg));
static int ftplogin __ARGS((struct ftpserv *ftp,char *pass));
static int sendit __ARGS((struct ftpserv *ftp,char *command,char *file));
static int recvit __ARGS((struct ftpserv *ftp,char *command,char *file));
static void sendmsgfile __ARGS((int s,int num,char *buf,int size,FILE *fp));
int doftptdisc __ARGS((int argc, char *argv[], void *p));
int doftpmaxsrv __ARGS((int argc, char *argv[], void *p));
static void SendPasv(int s, struct sockaddr_in *sock);
#if defined(MSDOS) || defined(FTPSERV_XPERM)
static int xpermcheck(struct ftpserv *ftp,int op,char **filep,char *patharg);
#endif
  
#ifdef __cplusplus
extern "C" {
#endif
extern int access __ARGS((const char *,int));
extern int unlink __ARGS((const char *));
extern int rmdir __ARGS((const char *));
#ifdef __cplusplus
}
#endif

#ifdef UNIX
extern unsigned long filelength __ARGS((int));
#endif

#ifdef FTPSERVER
  
extern char System[];
int FtpUsers=0;
static int Ftpmax = 0;   /*selcuk*/
  
/* Command table: ordering MUST match xxxx_CMD defines in ftpserv.h */
static char *commands[] = {
    "USER",
    "ACCT",
    "PASS",
    "TYPE",
    "LIST",
    "CWD",
    "DELE",
    "HELP",
    "QUIT",
    "RETR",
    "STOR",
    "PORT",
    "NLST",
    "PWD",
    "XPWD",                 /* For compatibility with 4.2BSD */
    "MKD ",
    "XMKD",                 /* For compatibility with 4.2BSD */
    "XRMD",                 /* For compatibility with 4.2BSD */
    "RMD ",
    "STRU",
    "MODE",
    "RSME",                 /* Added by IW0CNB, for resuming interrupted trasnfers */
    "RPUT",
    "NOOP",                 /* For OS/2 compatibility   kf5mg*/
    "SYST",
    "PASV",                 /* PASV by g4ide, for proxy-ftp, Mosaic... */
    "SIZE",                 /* file size  n3yco */
    "APPE",                 /* append     n3yco */
    "XLZW",                 /* LZW FTP Compression      kf5mg*/
    "CDUP",                 /* cwd .. */
    "REST",                 /* restart <starting_offset> */
#ifdef FTPSERV_MORE
    "RNFR",                 /* rename <fromname> ... */
    "RNTO",                 /* rename ... <toname> */
    "MDTM",                 /* modified date/time gmt */
#endif

    NULLCHAR
};
  
/* Response messages */
#if defined(UNIX_DIR_LIST) && defined(WS_FTP_KLUDGE)
/* SEE COMMENTS IN SYST COMMAND HANDLER, BELOW. */
static char banner[] = "220- %s, J*N*O*S FTP version %s\n";
#else
static char banner[] = "220- %s, JNOS FTP version %s\n";
#endif
static char banner1[] = "220  Ready on %s";
static char badcmd[] = "500 Unknown command\n";
static char binwarn[] = "150-Warning: type is ASCII and %s appears to be binary\n";
static char unsupp[] = "500 Unsupported command or option\n";
static char AnonymousLogin[] = "331- Anonymous login OK. Please give e-mail address as password!\n";
#ifdef MD5AUTHENTICATE
static char givepass[] = "331 Enter PASS command [%lx]\n";
#else
static char givepass[] = "331 Enter PASS command\n";
#endif
#ifdef DES_AUTH
static char challenge[] = "399 PASS challenge : %016lx\r\n";
#endif
static char restartmsg[] = "350 Restart at %s; send STOR or RETR or APPE next\n";
static char logged[] = "230 %s logged in\n";
static char loggeda[] = "230 Logged in as anonymous, restrictions apply\n";
static char typeok[] = "200 Type %s OK\n";
static char only8[] = "501 Only logical bytesize 8 supported\n";
/* static char deleok[] = "250 File deleted\n";*/
static char successful[] = "250 %s command successful\n";
static char mkdok[] = "200 MKD ok\n";
static char delefail[] = "550 Delete failed: %s\n";
static char pwdmsg[] = "257 \"%s\" is current directory\n";
static char badtype[] = "501 Unknown type \"%s\"\n";
static char badport[] = "501 Bad port syntax\n";
static char unimp[] = "502 Command not yet implemented\n";
static char bye[] = "221 Goodbye!\n";
static char nodir[] = "553 Can't read directory \"%s\": %s\n";
static char notadir[] = "550 Not a directory: %s\n";
static char cantopen[] = "550 Can't read file \"%s\": %s\n";
static char sending[] = "150 Opening data connection for %s %s %s\n"; /*N1BEE*/
static char cantmake[] = "553 Can't create \"%s\": %s\n";
static char writerr[] = "552 Write error: %s\n";
static char portok[] = "200 Port command okay\n";
static char rxok[] = "226 File received OK\n";
static char txok[] = "226 File sent OK\n";
static char noperm[] = "550 Permission denied\n";
static char noconn[] = "425 Data connection reset\n";
static char badcheck[] = "425 Bad checksum\n";
/*static char lowmem[] = "421 System overloaded, try again later\n";*/
static char tryagain[] = "421 Too many users (%d), try again later\n";  /*selcuk*/
static char notlog[] = "530 Please log in with USER and PASS\n";
static char userfirst[] = "503 Login with USER first.\n";
static char okay[] = "200 Ok\n";
static char help[] = "214-The following commands are recognized.\n";
static char syst[] = "215 %s Type: L8 Version: %s\n";
static char filesize[] = "213 %lu\n";
static char notaplain[] = "550 %s: not a plain file\n";
static char nosuchfile[] = "550 %s: No such file\n";
#ifdef FTPSERV_MORE
static char rntopending[] = "350 Rename awaiting new name\n";
static char badseq[] = "503 No prior RNFR; RNTO ignored\n";
static char norename[] = "550 Can't rename %s\n";
static char modtime[] = "213 %04d%02d%02d%02d%02d%02d\n";
#endif
  
#ifdef  LZW
static char LZWOk[] = "299 %d %d LZW OK\n";
#ifdef FTPSESSION
extern int Ftpslzw;
#else
int Ftpslzw = TRUE;   /* no way to turn this default off, unless FTPSESSION #define'd */
#endif
#endif /* LZW */
  
int32 Ftptdiscinit;
  
/* Set ftp redundancy timer */
int doftptdisc (int argc, char **argv, void *p)
{
    return setint32 (&Ftptdiscinit, "Ftp redundancy timer (sec)", argc, argv);
}
  
/* Set maximum number of concurrent ftp servers we'll spawn */
int
doftpmaxsrv(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    return setint(&Ftpmax,"Max. concurrent FTP servers",argc,argv);
}
  
/* Start up FTP service */
int
ftpstart(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_FTP;
    else
        port = atoi(argv[1]);
/*#ifdef LZW*/
    /* Both the Server and Client will default to trying to  */
    /* use lzw compressiom.                                  */
/*    Ftpslzw = 1;*/
/*    Ftpclzw = 1;*/
/*#endif*/
  
    return start_tcp(port,"FTP Server",ftpserv,2048);
}
  
static void sendmsgfile(int s,int num,char *buf,int size,FILE *fp) {
  
    while(fgets(buf,size,fp)) {
        rip(buf);
        usprintf(s,"%d- %s\n",num,buf);
    }
}

#ifdef FTPSERV_MORE
static void do_finish2 (struct ftpserv *ftp, char *rnfrom)
#else
static void do_finish2 (struct ftpserv *ftp)
#endif
{
    log(ftp->control,"close FTP");
    /* Clean up */
    close_s(ftp->control);
    if(ftp->data != -1)
        close_s(ftp->data);
    if(ftp->fp != NULLFILE)
        fclose(ftp->fp);
    free(ftp->username);
    free(ftp->path);
    free(ftp->cd);
#ifdef FTPSERV_MORE
    free(rnfrom);  /* in case rnto is pending */
#endif
}
  
static void
ftpserv(s,unused,p)
int s;  /* Socket with user connection */
void *unused;
void *p;
{
    struct ftpserv ftp;
    char **cmdp,buf[512],*arg,*cp,*file,*mode = (char*)0;
#ifdef FTPSERV_MORE
    char *rnfrom=NULLCHAR;
#endif
    long t;
    int cnt,i,r=0;
    struct sockaddr_in sock, lsocket, lcsocket;
    FILE *fp,*fpm;
  
    sockmode(s,SOCK_ASCII);
    memset((char *)&ftp,0,sizeof(ftp));     /* Start with clear slate */
    ftp.data = -1;
    
  
    sockowner(s,Curproc);           /* We own it now */
    ftp.control = s;
    /* Set default data port */
    i = SOCKSIZE;
    j2getpeername(s,(char *)&sock,&i);
    sock.sin_port = IPPORT_FTPD;
    ASSIGN(ftp.port,sock);
#ifdef LZW
    /* Socket assumes that lzw is off for socket. */
    ftp.uselzw = 0;
#endif
    ftp.startpoint = 0;
  
    /* Check concurrent server session limit -- Selcuk */
    if (Ftpmax && FtpUsers >= Ftpmax)
	{
      log(s, "denied FTP access (ftpmax)");
      usprintf(s,tryagain,FtpUsers);
#ifdef FTPSERV_MORE
		return (do_finish2 (&ftp, rnfrom));
#else
		return (do_finish2 (&ftp));
#endif
    }
  
    log(s,"open FTP");
    FtpUsers++;
  
    /* now go say 'hello' */
    usprintf(s,banner,Hostname,Version);
    if((fp = fopen(Ftpmotd,"r")) != NULL)
	{
        sendmsgfile(s, 220, buf, sizeof(buf), fp);
        fclose(fp);
    }
    time(&t);
    cp = ctime(&t);
    usprintf(s,banner1,cp);
 
	while (1)	/* replaces GOTO 'loop:' */
	{ 
    /* Time-out after some inactivity time - WG7J */
    j2alarm(Ftptdiscinit*1000);
    if((cnt = recvline(s,buf,sizeof(buf))) == -1)
	{
        /* He closed on us */
        break;
    }
    j2alarm(0);
  
    if(cnt == 0)
	{
        /* Can't be a legal FTP command */
        usputs(ftp.control,badcmd);
        continue;
    }
    rip(buf);
#ifdef notdef
    /* Translate first word to upper case */
    for(cp = buf;*cp != ' ' && *cp != '\0';cp++)
        *cp = toupper(*cp);
#endif
    /* Find command in table; if not present, return syntax error */
    for(cmdp = commands;*cmdp != NULLCHAR;cmdp++)
        if(strnicmp(*cmdp,buf,strlen(*cmdp)) == 0)
            break;
    if(*cmdp == NULLCHAR)
	{
        usputs(ftp.control,badcmd);
        continue;
    }
    /* Allow only USER, PASS, HELP and QUIT before logging in */
    if(ftp.cd == NULLCHAR || ftp.path == NULLCHAR)
	{
        switch(cmdp-commands)
		{
            case USER_CMD:
            case PASS_CMD:
            case HELP_CMD:
            case QUIT_CMD:
                break;

            default:
                usputs(ftp.control,notlog);
                continue;
        }
    }
    arg = &buf[strlen(*cmdp)];
    arg = skipwhite(arg);
  
    /* Execute specific command */
    switch(cmdp-commands)
	{
#ifdef LZW
        case LZW_CMD:
            if(!Ftpslzw) {
            /* if the server doens't want to use LZW compression, issue
             * the bad command message.
             */
                usputs(ftp.control,badcmd);
                ftp.uselzw  = 0;
            } else {
            /* Get bits and mode from clients XLZW request. */
                ftp.lzwmode = ftp.lzwbits = 0;
                sscanf(arg,"%d %d",&ftp.lzwbits,&ftp.lzwmode);
                if(!((ftp.lzwmode == 0 || ftp.lzwmode == 1)
                && (ftp.lzwbits > 8 && ftp.lzwbits < 17))) {
                /* bad args... DON'T use LZW and tell the client. */
                    ftp.lzwmode = LZWCOMPACT;
                    ftp.lzwbits = LZWBITS;
                    usputs(ftp.control,badcmd);
                    ftp.uselzw  = 0;
                } else {
                /* Tell the client that the command was accepted.    */
                    usprintf(ftp.control,LZWOk,ftp.lzwbits,ftp.lzwmode);
                /* And set the uselzw flag on the FTP socket struct. */
                    ftp.uselzw  = 1;
                }
            }
            break;
#endif /* LZW */
        case USER_CMD:
            free(ftp.username);
            ftp.username = j2strdup(arg);
            free(ftp.cd);   /* In case another 'user' command is issued - WG7J */
            free(ftp.path);
            ftp.cd = NULL;
            ftp.path = NULL;
        /* See if this would be an anonymous login */
        {
            int anony = 0;
            char *path;
  
            path = mallocw(FILE_PATH_SIZE);
            userlogin(ftp.username,"",&path,FILE_PATH_SIZE,&anony,"ftpperm");
            if(anony)
                usputs(ftp.control,AnonymousLogin);
            free(path);
        }
#ifdef MD5AUTHENTICATE
            time(&ftp.ttim);
            usprintf(ftp.control,givepass,ftp.ttim);
#else
            usputs(ftp.control,givepass);
#endif
            break;
        case TYPE_CMD:
        switch(arg[0]){
            case 'A':
            case 'a':       /* Ascii */
                ftp.type = ASCII_TYPE;
                usprintf(ftp.control,typeok,arg);
                break;
            case 'l':
            case 'L':
                while(*arg != ' ' && *arg != '\0')
                    arg++;
                if(*arg == '\0' || *++arg != '8'){
                    usputs(ftp.control,only8);
                    break;
                }
                ftp.type = LOGICAL_TYPE;
                ftp.logbsize = 8;
                usprintf(ftp.control,typeok,arg);
                break;
            case 'B':
            case 'b':       /* Binary */
            case 'I':
            case 'i':       /* Image */
                ftp.type = IMAGE_TYPE;
                usprintf(ftp.control,typeok,arg);
                break;
            default:        /* Invalid */
                usprintf(ftp.control,badtype,arg);
                break;
        }
            break;
        case QUIT_CMD:
            usputs(ftp.control,bye);
    		FtpUsers--;
#ifdef FTPSERV_MORE
			return (do_finish2 (&ftp, rnfrom));
#else
			return (do_finish2 (&ftp));
#endif
			break;

/* oz2lw - add support for REST <marker> method of restarting xfers */
        case REST_CMD:
            usprintf(ftp.control, restartmsg, arg);
            ftp.startpoint = atol(arg);
            break;     /* expecting RETR, STOR, or APPE next */

        case RETR_CMD:
        case RSME_CMD:
        switch(ftp.type){
            case IMAGE_TYPE:
            case LOGICAL_TYPE:
                mode = READ_BINARY;
                break;
            case ASCII_TYPE:
                mode = READ_TEXT;
                break;
        }
#if defined(MSDOS) || defined(FTPSERV_XPERM)
            if (!xpermcheck(&ftp, RETR_CMD, &file, arg)) {
#else
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,RETR_CMD,file)) {
#endif
                usputs(ftp.control,noperm);
            } else if((ftp.fp = fopen(file,mode)) == NULLFILE){
                usprintf(ftp.control,cantopen,file,strerror(errno));
            } else {
                log(ftp.control,"%s %s",*cmdp,file);  /* RSME or RETR */
                if(ftp.type == ASCII_TYPE && isbinary(ftp.fp)){
                    usprintf(ftp.control,binwarn,file);
                }
                sendit(&ftp,*cmdp,file);
            }
            free(file);
            break;
        case APPE_CMD:
        case STOR_CMD:
        case RPUT_CMD:
            file = pathname(ftp.cd,arg);
        switch(ftp.type){
            case IMAGE_TYPE:
            case LOGICAL_TYPE:
                if(cmdp-commands != STOR_CMD)
                    mode = APPEND_BINARY;
                else
                    mode = WRITE_BINARY;
                if(ftp.startpoint) /* preserve up to startpoint...*/
                    mode = READ_BINARY "+";  /* open read-update, seek, write */
                break;
            case ASCII_TYPE:
                if(cmdp-commands != STOR_CMD)
                    mode = APPEND_TEXT;
                else
                    mode = WRITE_TEXT;
                if(ftp.startpoint) /* preserve up to startpoint...*/
                    mode = READ_TEXT "+";  /* open read-update, seek, write */
                break;
        }
            if(!permcheck(ftp.path,((cmdp-commands!=STOR_CMD || ftp.startpoint) ? RPUT_CMD:STOR_CMD),file)){
                usputs(ftp.control,noperm);
            } else if((ftp.fp = fopen(file,mode)) == NULLFILE){
                usprintf(ftp.control,cantmake,file,strerror(errno));
            } else {
                log(ftp.control,"%s %s",*cmdp,file);  /* APPE/STOR/RPUT */
                recvit(&ftp,*cmdp,file);
            }
            free(file);
            break;
        case SIZE_CMD:
#if defined(MSDOS) || defined(FTPSERV_XPERM)
            if(!xpermcheck(&ftp, RETR_CMD, &file, arg)) {
#else
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,RETR_CMD,file)) {
#endif
                usputs(ftp.control,noperm);
            } else if((ftp.fp = fopen(file,READ_BINARY)) != NULLFILE){
                usprintf(ftp.control,filesize,filelength(fileno(ftp.fp)));
                fclose(ftp.fp);
            } else if(!access(file,0)) {
                usprintf(ftp.control,notaplain,file);
            } else {
                usprintf(ftp.control,nosuchfile,file);
            }
            free(file);
            break;
        case PORT_CMD:
            if(pport(&ftp.port,arg) == -1){
                usputs(ftp.control,badport);
            } else {
                usputs(ftp.control,portok);
            }
            break;
#ifndef CPM
        case LIST_CMD:
            i=1;
        case NLST_CMD:
    		if (cmdp-commands == NLST_CMD)
            	i=0;
#if defined(MSDOS) || defined(FTPSERV_XPERM)
            if(!xpermcheck(&ftp, RETR_CMD, &file, arg)) {
#else
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,RETR_CMD,file)) {
#endif
                usputs(ftp.control,noperm);
            } else if((ftp.fp = dir(file,i)) == NULLFILE){
                usprintf(ftp.control,nodir,file,strerror(errno));
            } else {
                sendit(&ftp,*cmdp,file);  /* LIST/NLST */
            }
            free(file);
            break;
        case CDUP_CMD:
            arg = "..";  /* and fall into the cwd command */
        case CWD_CMD:
#if defined(MSDOS) || defined(FTPSERV_XPERM)
            if(!xpermcheck(&ftp, RETR_CMD, &file, arg)) {
#else
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,RETR_CMD,file)) {
#endif
                usputs(ftp.control,noperm);
                free(file);
#ifdef  MSDOS
        /* Don'tcha just LOVE %%$#@!! MS-DOS? access() fails on some root dirs */
            } else if(strcmp(file,"/") == 0 || strcmp(file+1,":/") == 0 || access(file,0) == 0){
#else
            } else if(access(file,0) == 0){ /* See if it exists */
#endif

#ifdef MSDOS
                if((fpm = fopen(file,"r")) == NULLFILE) {  /* is it a dir? */
#else
                struct stat st;

                if(stat(file,&st)==0 && st.st_mode&S_IFDIR) {/* is it a dir? */
                    
#endif
                    /* Succeeded, record in control block */
                    free(ftp.cd);
                    ftp.cd = file;
  
                    /* If exists, send the contents of 'desc.ftp' in the new
                     * directory (unless the login passwd began with a '-').
                     */
                    strcpy(buf,file);
                    if(r!=2 && (fpm = fopen(strcat(buf,"/desc.ftp"),"r")) != NULL) {
                        sendmsgfile(ftp.control,250,buf,sizeof(buf),fpm);
                        fclose(fpm);
                    }
  
                    usprintf(ftp.control,successful,*cmdp);
                }
                else {
                    /* Failed because file is not a directory */
                    usprintf(ftp.control,notadir,file);
                    free(file);
#ifdef MSDOS
                    fclose(fpm);
#endif
                }
            } else {
            /* Failed, don't change anything */
                usprintf(ftp.control,nodir,file,strerror(errno));
                free(file);
            }
            break;
        case XPWD_CMD:
        case PWD_CMD:
            usprintf(ftp.control,pwdmsg,ftp.cd);
            break;
#else
        case LIST_CMD:
        case NLST_CMD:
        case CWD_CMD:
        case XPWD_CMD:
        case PWD_CMD:
#endif
#ifndef LZW
        case LZW_CMD:
#endif
        case ACCT_CMD:
            usputs(ftp.control,unimp);
            break;
        case HELP_CMD:
            usputs(ftp.control,help);
            for(cmdp = commands,i=buf[0]=0;*cmdp != NULLCHAR;cmdp++) {
                    strcat(buf,"   ");
                    strcat(buf,*cmdp);
                    if (i++ == 7) {
                        strupr(buf);
                        usprintf(ftp.control,"%s\n", buf);
                        i=buf[0]=0;
                    }
            }
            if (i) {
                strupr(buf);
                usprintf(ftp.control,"%s\n", buf);
            }
            usprintf(ftp.control,"214 Report problems to sysop@%s\n",Hostname);
            break;
        case NOOP_CMD:
            usputs(ftp.control,okay);
            break;
        case DELE_CMD:
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,DELE_CMD,file)){
                usputs(ftp.control,noperm);
            } else if(unlink(file) == 0){
                log(ftp.control,"DELE %s",file);
                usprintf(ftp.control,successful,*cmdp);
            } else {
                usprintf(ftp.control,delefail,strerror(errno));
            }
            free(file);
            break;
        case PASS_CMD:
            if(ftp.username == NULLCHAR)
                usputs(ftp.control,userfirst);
            else
                r=ftplogin(&ftp,arg);
            break;
#ifndef CPM
        case XMKD_CMD:
        case MKD_CMD:
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,MKD_CMD,file)){
                usputs(ftp.control,noperm);
#ifdef  UNIX
            } else if(mkdir(file,(mode_t)0777) == 0){
#else
            } else if(mkdir(file) == 0){
#endif
                log(ftp.control,"MKD %s",file);
                usputs(ftp.control,mkdok);
            } else {
                usprintf(ftp.control,cantmake,file,strerror(errno));
            }
            free(file);
            break;
        case XRMD_CMD:
        case RMD_CMD:
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,RMD_CMD,file)){
                usputs(ftp.control,noperm);
            } else if(rmdir(file) == 0){
                log(ftp.control,"RMD %s",file);
                usprintf(ftp.control,successful,*cmdp);
            } else {
                usprintf(ftp.control,delefail,strerror(errno));
            }
            free(file);
            break;
        case STRU_CMD:
            if(tolower(arg[0]) != 'f')
                usputs(ftp.control,unsupp);
            else
                usputs(ftp.control,okay);
            break;
        case MODE_CMD:
            if(tolower(arg[0]) != 's')
                usputs(ftp.control,unsupp);
            else
                usputs(ftp.control,okay);
            break;
        case SYST_CMD:
#if defined(UNIX_DIR_LIST) && defined(WS_FTP_KLUDGE)
/* O/S name must match Unix-format LIST to work properly with ws_ftp.  BUT
   if we claim we're a Unix system, assumptions about line endings will be WRONG,
   and this could lead to worse problems!  E.g. someone does a binary-mode xfer
   of a text file because they know it'll upload faster and think it'll be perfectly
   readable anyhow.  Not recommended! -- n5knx */
            usprintf(ftp.control,syst,"UNIX",shortversion);
#else
            usprintf(ftp.control,syst,System,shortversion);
#endif
            break;
        case PASV_CMD:
            /*
             * Send the PASV message. Use the IP address
             * on the local end of our control connection.
             */
            if (ftp.data != -1) /* left over error - kill the socket first */
               close_s(ftp.data);

            ftp.data = j2socket(AF_INET,SOCK_STREAM,0);
            j2listen(ftp.data,0);
            i = SOCKSIZE;
            j2getsockname(ftp.data,(char *)&lsocket,&i);
            i = SOCKSIZE;
            j2getsockname(ftp.control,(char *)&lcsocket,&i);
            lsocket.sin_addr.s_addr = lcsocket.sin_addr.s_addr;
            /*
             * send the address to the client.
             */
            SendPasv(ftp.control, &lsocket);
            break;
#ifdef FTPSERV_MORE
        case RNFR_CMD:
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,DELE_CMD,file))
                usputs(ftp.control,noperm);
            else if(access(file,6))  /* RW access? */
                usprintf(ftp.control,cantopen,file,strerror(errno));
            else {
                usputs(ftp.control,rntopending);
                free(rnfrom);
                rnfrom=j2strdup(file);
            }
            free(file);
            break;
        case RNTO_CMD:
            file = pathname(ftp.cd,arg);
            if(!rnfrom)
                usputs(ftp.control, badseq);
            else {
                if(!permcheck(ftp.path,STOR_CMD,file))
                    usputs(ftp.control,noperm);
                else if(rename(rnfrom,file))
                    usprintf(ftp.control,norename,strerror(errno));
                else
                    usprintf(ftp.control,successful,*cmdp);
                free(rnfrom);
                rnfrom=NULLCHAR;
            }
            free(file);
            break;
        case MDTM_CMD:
#if defined(MSDOS) || defined(FTPSERV_XPERM)
            if(!xpermcheck(&ftp, RETR_CMD, &file, arg)) {
#else
            file = pathname(ftp.cd,arg);
            if(!permcheck(ftp.path,RETR_CMD,file)) {
#endif
                usputs(ftp.control,noperm);
            }
            else {
                struct stat st;
                struct tm *tmp;

#ifdef MSDOS
/* Borland stat() will crash msdos if file doesn't exist!!!
 * 10/97 bc++3.1 still true!  So we use access() to test existence. N5KNX
 */
                if(access(file,0) || stat(file,&st))  /* access=0 => file/dir exists */
#else
                if(stat(file,&st))
#endif
                    usprintf(ftp.control,nosuchfile,file);
                else if(st.st_mode&S_IFDIR) /* is it a dir? */
                    usprintf(ftp.control,notaplain,file);
                else {
                    tmp=gmtime(&st.st_mtime);
                    usprintf(ftp.control,modtime,tmp->tm_year+1900,tmp->tm_mon+1,
                        tmp->tm_mday, tmp->tm_hour, tmp->tm_min, tmp->tm_sec);
                }
            }
            free(file);
            break;
#endif
    }
#endif /* !CPM */

	}	/* end of while loop that replaces GOTO 'loop:' */

    FtpUsers--;

#ifdef FTPSERV_MORE
	return (do_finish2 (&ftp, rnfrom));
#else
	return (do_finish2 (&ftp));
#endif
}
  
/* Shut down FTP server */
int
ftp0(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    int16 port;
  
    if(argc < 2)
        port = IPPORT_FTP;
    else
        port = atoi(argv[1]);
    return stop_tcp(port);
}
  
static
int
pport(sock,arg)
struct sockaddr_in *sock;
char *arg;
{
    int32 n;
    int i;
  
    n = 0;
    for(i=0;i<4;i++){
        n = atoi(arg) + (n << 8);
        if((arg = strchr(arg,',')) == NULLCHAR)
            return -1;
        arg++;
    }
    sock->sin_addr.s_addr = n;
    n = atoi(arg);
    if((arg = strchr(arg,',')) == NULLCHAR)
        return -1;
    arg++;
    n = atoi(arg) + (n << 8);
    sock->sin_port = (unsigned short) n;
    return 0;
}
  
/* Attempt to log in the user whose name is in ftp->username and password
 * in pass.  Return 0 if unsuccessful, 1 or 2 otherwise. Return 2 if passwd
 * began with a '-' (implying we should minimize extraneous reports). (Selcuk)
 */
static int
ftplogin(ftp,pass)
struct ftpserv *ftp;
char *pass;
{
    char *path;
    int len,anony = 0, r=1;
    FILE *fp;
    char buf[FILE_PATH_SIZE];
#ifdef MD5AUTHENTICATE
    int32 challenge;
#endif
  
    if(*pass == '-') {    /* no message.ftp, desc.ftp, reports */
      pass++;
      r = 2;
    }
    path = mallocw(FILE_PATH_SIZE);
#ifdef MD5AUTHENTICATE
	/* 17Nov2009, Maiko, 32 bit - use int32 not long */
	challenge = (int32)ftp->ttim;
    memcpy(path, (char *)&challenge, sizeof(challenge));  /* hide it in path */
#endif
    if(userlogin(ftp->username,pass,&path,FILE_PATH_SIZE,&anony,"ftpperm") == -1) {
        usputs(ftp->control,noperm);
        free(path);
        return 0;
    }
#ifdef MD5AUTHENTICATE
    /* if passwd had a '-' prefix, the anony integer has bit 2^7 set */
    if (anony & 0x0080) {
        r = 2;  /* '-' => minimize extraneous output */
        anony ^= 0x0080;
    }
#endif
    /* Set up current directory and path prefix */
    ftp->path = j2strdup(path);
    ftp->cd = firstpath(path);
    free(path);
  
    strcpy(buf,ftp->cd);
    /* If path ends on /, cut if off; this allows the message to be
     * send when login is to the root dir - WG7J
     */
    len = strlen(buf);
    if(buf[len-1] == '/')
        buf[len-1] = '\0';
  
    if(r!=2 && (fp = fopen(strcat(buf,"/message.ftp"),"r")) != NULL) {
        sendmsgfile(ftp->control,230,buf,sizeof(buf),fp);
        fclose(fp);
    }
  
    if(!anony){
        usprintf(ftp->control,logged,ftp->username);
        log(ftp->control,"%s logged in",ftp->username);
    } else {
        usputs(ftp->control,loggeda);
        log(ftp->control,"%s logged in, ID %s",ftp->username,pass);
    }
    return r;
}

/* 28Dec2004, replace GOTO 'send_err:' LABEL */
static int do_send_err (struct ftpserv *ftp, long total)
{
	fclose(ftp->fp);
    ftp->fp = NULLFILE;
    close_s(ftp->data);
    ftp->data = -1;
#ifdef LZW
    /* Done with data socket. Turn off lzw for now */
    ftp->uselzw = 0;
#endif
    if(total == -1)
        return -1;
    else
        return 0;
}

static int
sendit(ftp,command,file)
struct ftpserv *ftp;
char *command;
char *file;
{
    long total = 0L, starting;
    unsigned long check;
    struct sockaddr_in dport;
    char *cp;
    char fsizetext[20] = "";                /* N1BEE */
  
   if (ftp->data == -1) {  /* not PASV */
    ftp->data = j2socket(AF_INET,SOCK_STREAM,0);
    dport.sin_family = AF_INET;
    dport.sin_addr.s_addr = INADDR_ANY;
    dport.sin_port = IPPORT_FTPD;
    j2bind(ftp->data,(char *)&dport,SOCKSIZE);
    sprintf(fsizetext, "(%lu bytes)", filelength(fileno(ftp->fp)));
    usprintf(ftp->control,sending,command,file,fsizetext);  /* N1BEE */
    if(j2connect(ftp->data,(char *)&ftp->port,SOCKSIZE) == -1){
        fclose(ftp->fp);
        ftp->fp = NULLFILE;
        close_s(ftp->data);
        ftp->data = -1;
        usputs(ftp->control,noconn);
        return -1;
    }
   } else { /* PASV mode */
      /* wait for the client to open the connection */
      /* ftp->data has been setup already */
      j2accept(ftp->data,NULLCHAR,(int *)NULL);
      sprintf(fsizetext, "(%lu bytes)", filelength(fileno(ftp->fp)));
      usprintf(ftp->control,sending,command,file,fsizetext);  /* N1BEE */
   }

#ifdef LZW
    if(ftp->uselzw == 1)
        /* do lzwinit() for socket. */
        lzwinit(ftp->data,ftp->lzwbits,ftp->lzwmode);
#endif
    if(strcmp(command,"RSME") == 0)
	{
        cp = mallocw(40);
        recvline(ftp->control,cp,40);
        starting = atol(cp);
        /* If checksum field is not present go on anyway, for compatibility
         * with previous scheme. If present check it and barf if wrong.
         */
        if(strchr(cp,' ') != NULL)
		{
            check = (unsigned long)atol(strchr(cp,' '));
            check -= checksum(ftp->fp,starting);
            if(check != 0){
                free(cp);
                usputs(ftp->control,badcheck);
                j2shutdown(ftp->data,1);  /* Blow away data connection */
				return (do_send_err (ftp, total));
            }
        }
		else if (fseek(ftp->fp,starting,SEEK_SET) != 0)
		{
            free(cp);
            usputs(ftp->control,noconn);
            j2shutdown(ftp->data,2);  /* Blow away data connection */
			return (do_send_err (ftp, total));
        }
    }
    else if ((starting=ftp->startpoint) > 0)
	{   /* preceding REST command provided an offset */
        cp=NULL;
        ftp->startpoint=0;  /* only one chance */

        if(fseek(ftp->fp,starting,SEEK_SET) != 0)
		{
            free(cp);
            usputs(ftp->control,noconn);
            j2shutdown(ftp->data,2);  /* Blow away data connection */
			return (do_send_err (ftp, total));
        }
    }
  
    /* Do the actual transfer */
    total = sendfile(ftp->fp,ftp->data,ftp->type,0,NULL);
  
    if(total == -1){
        /* An error occurred on the data connection */
        usputs(ftp->control,noconn);
        j2shutdown(ftp->data,2);  /* Blow away data connection */
    } else {
        usputs(ftp->control,txok);
    }
	return (do_send_err (ftp, total));
}

static int
recvit(ftp,command,file)
struct ftpserv *ftp;
char *command;
char *file;
{
    struct sockaddr_in dport;
    long total, starting;
  
   if(ftp->data == -1) {  /* not PASV */
    ftp->data = j2socket(AF_INET,SOCK_STREAM,0);
    dport.sin_family = AF_INET;
    dport.sin_addr.s_addr = INADDR_ANY;
    dport.sin_port = IPPORT_FTPD;
    j2bind(ftp->data,(char *)&dport,SOCKSIZE);
    usprintf(ftp->control,sending,command,file,"");
    if(j2connect(ftp->data,(char *)&ftp->port,SOCKSIZE) == -1){
        fclose(ftp->fp);
        ftp->fp = NULLFILE;
        close_s(ftp->data);
        ftp->data = -1;
        usputs(ftp->control,noconn);
        return -1;
    }
   } else {  /* PASV mode */
      /* wait for the client to open the connection */
      /* ftp->data has been setup already */
      j2accept(ftp->data,NULLCHAR,(int *)NULL);
      usprintf(ftp->control,sending,command,file,"");
   }

#ifdef LZW
    if(ftp->uselzw == 1)
        /* do lzwinit() for socket. */
        lzwinit(ftp->data,ftp->lzwbits,ftp->lzwmode);
#endif
    if(strcmp(command,"RPUT") == 0){
        if((starting = getsize(ftp->fp)) == -1)
            starting = 0L;
        usprintf(ftp->control,"%ld %lu\n",starting,checksum(ftp->fp,starting));
        fseek(ftp->fp,starting,SEEK_SET);
    }
    else if(ftp->startpoint) {   /* preceding REST command provided an offset */
        fseek(ftp->fp,ftp->startpoint,SEEK_SET);
        ftp->startpoint=0;  /* only one chance */
    }
  
    total = recvfile(ftp->fp,ftp->data,ftp->type,0,Ftptdiscinit*1000);
  
#ifdef  CPM
    if(ftp->type == ASCII_TYPE)
        putc(CTLZ,ftp->fp);
#endif
    if(total == -1) {
        /* An error occurred while writing the file */
        usprintf(ftp->control,writerr,strerror(errno));
        j2shutdown(ftp->data,2);  /* Blow it away */
    } else
        usputs(ftp->control,rxok);
  
    close_s(ftp->data);
    ftp->data = -1;
    fclose(ftp->fp);
    ftp->fp = NULLFILE;
#ifdef LZW
    /* Done with data socket. Turn off lzw for now */
    ftp->uselzw = 0;
#endif
    if(total == -1)
        return -1;
    else
        return 0;
}
  
#if defined(MSDOS) || defined(FTPSERV_XPERM)
/* Extended permcheck: IF no access is allowed to <file> under dir <path>,
   AND <file> begins with '/', THEN see if access is permitted to <file> under
   the user's root (ie, first-listed in ftpusers) dir.  This is a KLUDGE to
   permit Jnos and programs like Netscape/Mosaic/Lynx to interoperate.  It is
   useful since Jnos allows a root dir different from "/" and when these dim-
   witted clients 'cd  /' and wind up in 'c:/pub' they get really confused.
   -- n5knx and n3yco
*/
static int
xpermcheck(ftp,op,filep,patharg)
struct ftpserv *ftp;
int op;
char **filep;
char *patharg;
{
    int access_ok;
    char *user_root;

    *filep = pathname(ftp->cd, patharg);   /* interpret relative to current dir */
    access_ok = permcheck(ftp->path, op, *filep);
    if (!access_ok && *patharg == '/') {
        free(*filep);
        user_root = firstpath(ftp->path);
        *filep = pathname(user_root, patharg+1);  /* interpret relative to root dir */
        free(user_root);
        access_ok = permcheck(ftp->path, op, *filep);
    }
    return access_ok;
}
#endif /* MSDOS || FTPSERV_XPERM */

static void
SendPasv(int s, struct sockaddr_in *socket)
{
    /* Send PORT a,a,a,a,p,p message */
    usprintf(s,"227 Entering Passive Mode. %u,%u,%u,%u,%u,%u\n",
    hibyte(hiword(socket->sin_addr.s_addr)),
    lobyte(hiword(socket->sin_addr.s_addr)),
    hibyte(loword(socket->sin_addr.s_addr)),
    lobyte(loword(socket->sin_addr.s_addr)),
    hibyte(socket->sin_port),
    lobyte(socket->sin_port));
}

#endif /* FTPSERVER */

/* Following code is accessed externally, so may be needed even if ftpserv isn't. */

#ifdef  MSDOS
/* Illegal characters in a DOS filename */
static char badchars[] = "\"[]|<>+=;,";
#endif
  
/* Return 1 if the file operation is allowed, 0 otherwise.  Called by mbox routines too! */
int
permcheck(path,op,file)
char *path;
int op;
char *file;
{
    char *cp,*privs;
    long perms;
  
    if(file == NULLCHAR || path == NULLCHAR)
        return 0;       /* Probably hasn't logged in yet */
  
#ifdef  MSDOS
    /* Check for characters illegal in MS-DOS file names */
  
#ifdef notdef
    for(cp = file;*cp != '\0';cp++){
        if(*cp != '\\' && *cp != '/' && *cp != '.' && *cp != ':'
            && *cp != '?' && *cp != '*' && dosfnchr(*cp) == 0)
            return 0;
    }
#endif
  
    if(strpbrk(file,badchars) != NULLCHAR)
        return 0;
  
#endif /* MSDOS */
  
#ifndef MAC
    /* The target file must be under the user's allowed search path */
    /* We let them specify multiple paths using path;path... -russ */
    /* Now full form: path[;path...] perm path[;path...] perm ... */
    {
        int pathlen;
  
        for(cp = path;;){
            /* Make sure format is valid, privs field should be present! */
            if((privs=strpbrk(cp," \t")) == NULLCHAR)
                return 0;
            /* Find length of path */
            pathlen = strcspn(cp,"; \t");
            while(pathlen) {
                /* Check filename with path */
                if(strncmp(file,cp,(unsigned)pathlen) == 0 && (
                    /* Some path validation */
                    file[pathlen] == '\0' ||
                    file[pathlen] == '/' ||
                    file[pathlen-1] == '/'))
                    break;
                /* Check next path */
                cp += pathlen;
                if(*cp == ';')  /* There is more in this compound path ! */
                    pathlen = strcspn(++cp,"; \t");
                else
                    pathlen = 0;
            }
            if(pathlen) /* we found a match, no check privs */
                break;
            /* now see if there is more after the privs... */
            privs = skipwhite(privs);
            if((cp = strpbrk(privs," \t")) == NULL)
                return 0;   /* Nothing there ! */
            /* skip spaces or tabs to get start of next 'path perm' entry */
            cp = skipwhite(cp);
        }
    }
#endif /* MAC */
  
    /* Now find start of privs */
    privs = skipwhite(privs);
    if(strnicmp(privs,"0x",2) == 0)
        perms = htol(privs);
    else
        perms = atol(privs);
  
    switch(op){
        case RETR_CMD:
        /* User must have permission to read files */
            if(perms & FTP_READ)
                return 1;
            return 0;
        case DELE_CMD:
        case RMD_CMD:
        case RPUT_CMD:
        /* User must have permission to (over)write files */
            if(perms & FTP_WRITE)
                return 1;
            return 0;
        case STOR_CMD:
        case MKD_CMD:
        /* User must have permission to (over)write files, or permission
         * to create them if the file doesn't already exist
         */
            if(perms & FTP_WRITE)
                return 1;
            if(access(file,2) == -1 && (perms & FTP_CREATE))
                return 1;
            return 0;
    }
    return 0;       /* "can't happen" -- keep lint happy */
}
  
