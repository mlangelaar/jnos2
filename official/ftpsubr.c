/* Routines common to both the FTP client and server
 * Copyright 1991 Phil Karn, KA9Q
 */
 /* Mods by G1EMM */
#include "global.h"
#include "mbuf.h"
#include "socket.h"
#include "proc.h"
#include "mailbox.h"
#include "bm.h"
#include "ftp.h"
#include "ftpcli.h"
  
/* Send a file (opened by caller) on a network socket.
 * Normal return: count of bytes sent
 * Error return: -1
 */
long
sendfile(fp,s,mode,hash,m)
FILE *fp;   /* File to be sent */
int s;      /* Socket to be sent on */
int mode;   /* Transfer mode */
int hash;   /* Print hash marks every BLKSIZE bytes */
struct mbx *m;
{
    struct mbuf *bp;
    int c,oldf;
    long total = 0;
    long hmark = 0;
#ifdef MAILBOX
    int lin;
    int usemore = 0;
#endif
  
    switch(mode){
        default:
        case LOGICAL_TYPE:
        case IMAGE_TYPE:
            sockmode(s,SOCK_BINARY);
            for(;;){
                bp = ambufw(BLKSIZE);
                if((bp->cnt = fread(bp->data,1,BLKSIZE,fp)) == 0){
                    free_p(bp);
                    break;
                }
                total += bp->cnt;
                if(send_mbuf(s,bp,0,NULLCHAR,0) == -1){
                    total = -1;
                    break;
                }
                while(hash == V_HASH && total >= hmark+1000){
                    tputc('#');
                    hmark += 1000;
                }
                while(hash == V_BYTE && total >= hmark+1000){
                    tprintf("Bytes sent : %ld\r", total);
                    hmark += 1000;
                }
            }
            break;
        case ASCII_TYPE:
#ifdef MAILBOX
            if(m && ((lin=m->morerows) != 0))
                usemore = 1;    /* Display More prompt */
#endif
            oldf = j2setflush(s,-1);
        /* Let the newline mapping code in usputc() do the work */
            sockmode(s,SOCK_ASCII);
            while((c = getc(fp)) != EOF){
#if !defined(UNIX) && !defined(__TURBOC__)
                if(c == '\r'){
                /* Needed only if the OS uses a CR/LF
                 * convention and getc doesn't do
                 * an automatic translation
                 */
                    continue;
                }
#endif
                if(usputc(s,(char)c) == -1){
                    total = -1;
                    break;
                }
                total++;
#ifdef MAILBOX
            /* More prompting added - WG7J */
                if(c == '\n'){
                    if(usemore && --lin <= 0){
                        if(charmode_ok(m))
                            c = tkeywait(TelnetMorePrompt,0,
                                (int)(OPT_IS_DEFINED(m,TN_LINEMODE)&OPT_IS_SET(m,TN_LINEMODE)));
                        else  /* For AX.25 and NET/ROM connects - WG7J */
                            c = mykeywait(BbsMorePrompt,m);
                        if(c == -1 || c == 'q' || c == 'Q')
                            break;
                        if(c == '\n' || c == '\r')
                            lin = 1;
                        else
                            lin = m->morerows;
                    }
  
                }
#endif
                while(hash == V_HASH && total >= hmark+1000){
                    tputc('#');
                    hmark += 1000;
                }
                while(hash == V_BYTE && total >= hmark+1000){
                    tprintf("Bytes sent : %ld\r", total);
                    hmark += 1000;
                }
            }
            usflush(s);
            j2setflush(s,oldf);
            break;
    }
    if(hash)
        tputc('\n');
    return total;
}
/* Receive a file (opened by caller) from a network socket.
 * Normal return: count of bytes received
 * Error return: -1
 *
 * 30Sep2009, Maiko, timeout is now an int32, not a long variable
 */
long recvfile (FILE *fp, int s, int mode, int hash, int32 timeoutms)
{
    int cnt,c;
    struct mbuf *bp;
    long total = 0;
    long hmark = 0;
  
    switch(mode){
        default:
        case LOGICAL_TYPE:
        case IMAGE_TYPE:
            sockmode(s,SOCK_BINARY);
#ifdef FTPDATA_TIMEOUT
            while(j2alarm(timeoutms), (cnt = recv_mbuf(s,&bp,0,NULLCHAR,0)) != 0){
#else
            while((cnt = recv_mbuf(s,&bp,0,NULLCHAR,0)) != 0){
#endif
                if(cnt == -1){
                    total = -1;
                    break;
                }
#ifdef FTPDATA_TIMEOUT
/* unneeded?       j2alarm(0); */
#endif
                total += cnt;
                while(hash == V_HASH && total >= hmark+1000){
                    tputc('#');
                    hmark += 1000;
                }
                while(hash == V_BYTE && total >= hmark+1000){
                    tprintf("Bytes recv : %ld\r", total);
                    hmark += 1000;
                }
                if(fp != NULLFILE){
                    if(write_p(fp,bp) == -1){
                        free_p(bp);
                        total = -1;
                        break;
                    }
                    free_p(bp);
                } else {
                    send_mbuf(Curproc->output, bp, 0, NULLCHAR, 0);
                }
            }
            break;
        case ASCII_TYPE:
            sockmode(s,SOCK_ASCII);
#ifdef FTPDATA_TIMEOUT
            j2alarm(timeoutms);
#endif
            while((c = recvchar(s)) != EOF){
                if(fp != NULLFILE){
#if !defined(UNIX) && !defined(__TURBOC__) && !defined(AMIGA)
                    if(c == '\n'){
                    /* Needed only if the OS uses a CR/LF
                     * convention and putc doesn't do
                     * an automatic translation
                     */
                        putc('\r',fp);
                    }
#endif
                    if(putc(c,fp) == EOF){
                        total = -1;
                        break;
                    }
                } else {
                    tputc((char)c);
                }
                total++;
#ifdef FTPDATA_TIMEOUT
                if ((total % 250) == 0)  /* have we gotten 250 more chars */
                    j2alarm(timeoutms);  /* set timer back up again */
#endif
                while(hash == V_HASH && total >= hmark+1000){
                    tputc('#');
                    hmark += 1000;
                }
                while(hash == V_BYTE && total >= hmark+1000){
                    tprintf("Bytes recv : %ld\r", total);
                    hmark += 1000;
                }
            }
        /* Detect an abnormal close */
            if(socklen(s,0) == -1)
                total = -1;
            break;
    }
#ifdef FTPDATA_TIMEOUT
    j2alarm(0);
#endif
    if(hash)
        tputc('\n');
    return total;
}
/* Determine if a file appears to be binary (i.e., non-text).
 * Return 1 if binary, 0 if ascii text after rewinding the file pointer.
 *
 * Used by FTP to warn users when transferring a binary file in text mode.
 */
int
isbinary(fp)
FILE *fp;
{
    int c,i;
    int rval;
  
    rval = 0;
    for(i=0;i<512;i++){
        if((c = getc(fp)) == EOF)
            break;
        if(c & 0x80){
            /* High bit is set, probably not text */
            rval = 1;
            break;
        }
    }
    /* Assume it was at beginning */
    fseek(fp,0L,SEEK_SET);
    return rval;
}
  
long
getsize(fp)
FILE *fp;
{
    fseek(fp,0L,SEEK_END);  /* Go to end of file */
    return ftell(fp);   /* Return file pointer position */
}
  
/* Compute checksum of the first n bytes */
unsigned long
checksum(fp,n)
FILE *fp;
long n;
{
    unsigned long sum;
    long i;
    int c;
  
    rewind(fp);
    sum = 0;
    for(i=1;i<=n;i++){
        if((c = fgetc(fp)) == EOF)
            break;
        sum += (unsigned long)c;
    }
    return sum;
}
