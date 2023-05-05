/*
 * A version of Ward Christensen's file transfer protocol for
 * Unix System V or 4.2 bsd.
 *
 *        Emmet P. Gray, ..!ihnp4!uiucuxc!fthood!egray, 16 Aug 85
 *
 * Modified by Sanford Zelkovitz   08/18/86
 * Last modification date = 05/20/87
 * Modified for KA9Q NOS BBS - WA3DSP 2/93
 */
  
#ifndef UNIX
#include <io.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <stdarg.h>
#include "global.h"
#ifdef TIPSERVER
#ifdef XMODEM
#include "dirutil.h"
#include "timer.h"
#include "socket.h"
#include "mailbox.h"
  
#ifdef __cplusplus
extern "C" {
#endif
extern int access __ARGS((const char *, int));
extern int unlink __ARGS((const char *));
#ifdef __cplusplus
}
#endif
  
#define SPEED 2400       /* Serial line Baudrate */
  
#define MAXERRORS 10     /* max number of times to retry */
#define SECSIZE   128    /* CP/M sector, transmission block */
#define CPMEOF    26     /* End Of File (for CP/M) */
/* SOH=Start Of Header; STX=Start of 1K block; EOT=End Of Transmission */
/* ACK=ACKnowledge; NAK=Negative AcKnowledge; CAN=CANcel; BS=Backspace */
#define SOH       1
#define STX       2
#define EOT       4
#define ACK       6
#define NAK       21
#define CAN       24
#define BS        8
  
static int xrecvfile(char*,struct mbx *m);
static int xsendfile(char*,struct mbx *m);
static char getchar_t(int socket);
static void update_crc(unsigned char c, unsigned char *crc1, unsigned char *crc2);
static void error(struct mbx *m);
static void print_text(struct mbx *m,char *fmt, ...);
static void rawmode(struct mbx *);
static void restoremode(struct mbx *);
  
int doxmodem(char mode,char *filename,void *p)
{
    int exit_return=0, oldflush;
    struct mbx *m;
  
    m = (struct mbx *)p;
    oldflush=j2setflush(m->user,-1);
  
    switch (mode) {
        case 'r':
        case 'R':
            exit_return=xrecvfile(filename,m);
            break;
        case 's':
        case 'S':
            exit_return=xsendfile(filename,m);
            break;
        default :
            print_text(m,"Xmodem: Invalid Option\n");
    }
    restoremode(m);
    usputc(m->user,'\n');
    usflush(m->user);
    j2setflush(m->user,oldflush);
    return(exit_return);
}
  
/* send a file to the remote */
static int
xsendfile(char *tfile,struct mbx *m)
{
    FILE *fp;
    unsigned char chr, checksum, block, sector[SECSIZE];
    unsigned char crc1, crc2, mode, errcount, errcount2, two_can;
    int i, nbytes, speed=SPEED;
    long size, min, sec;
  
    if ((size=fsize(tfile))==-1){
        print_text(m,"xmodem: Can't open '%s' for read\n", tfile);
        return(1);
    }
  
    if ((fp = fopen(tfile, READ_BINARY)) == NULLFILE) {
        print_text(m,"xmodem: Can't open '%s' for read\n", tfile);
        return(1);
    }
  
    size=(size/128L)+1L;
    sec = size * 128L * 15L / speed;
    min = sec / 60L;
    sec = sec - min * 60L;
    print_text(m,"\nFile open: %d records\n", size);
    print_text(m,"Send time: %ld min, %ld sec at %d baud\n", min, sec, speed);
    print_text(m,"To cancel: use CTRL-X numerous times\n");
    print_text(m,"Waiting receive ready signal\n");
  
    j2pause(3000);
    rawmode(m);
    errcount = 0;
    mode = 0;
    two_can=0;
    block = 1;
  
    while (errcount < MAXERRORS) {
        chr = getchar_t(m->user);
        if (chr == NAK)                        /* checksum mode */
            break;
        if (chr == 'C') {                /* CRC mode */
            mode = 1;
            break;
        }
        if (chr == CAN) {
            if (two_can)  {
                j2pause(3000);
                print_text(m,"\nxmodem: Abort request received\n");
                fclose(fp);
                return(1);
            }
            two_can=1;
        } else {
            two_can=0;
        }
  
        errcount++;
    }
    if (errcount >= MAXERRORS) {
        j2pause(3000);
        print_text(m,"xmodem: Timed out on acknowledge\n");
        fclose(fp);
        return(1);
    }
    two_can=0;
    while ((nbytes= fread(sector,1,128, fp))!=0) {
        if (nbytes < SECSIZE) {              /* fill short sector */
            for (i=nbytes; i < SECSIZE; i++)
                sector[i] = CPMEOF;
        }
        errcount = 0;
        while (errcount < MAXERRORS)
		{
            usputc(m->user,SOH);        /* the header */
            usputc(m->user,block);      /* the block number */
            chr = ~block;
            usputc(m->user,chr);        /* it's complement */
            checksum = 0;
            crc1 = 0;
            crc2 = 0;
            for (i=0; i < SECSIZE; i++) {
                usputc(m->user,sector[i]);
                if (mode)
                    update_crc(sector[i],&crc1,&crc2);
                else
                    checksum += sector[i];
            }
            if (mode) {
                update_crc(0,&crc1,&crc2);
                update_crc(0,&crc1,&crc2);
                usputc(m->user,crc1);
                usputc(m->user,crc2);
  
            }
            else
                usputc(m->user,checksum);
  
            usflush(m->user);
            errcount2=0;

			while (1)	/* replaces 'rec_loop:' GOTO LABEL */
			{
  
            chr = getchar_t(m->user);
            if (chr == CAN)
			{
                if (two_can)
				{
                    j2pause(3000);
                    print_text(m,"\nxmodem: Abort request received\n");
                    fclose(fp);
                    return(1);
                }
                two_can=1;
            } else {
                two_can=0;
            }
  
            if (chr == ACK)
                break;                /* got it! */
                         /* noise on line? */
            if (chr != NAK ) {
                ++errcount2;
                if (errcount2>=MAXERRORS) {
                    error(m);
                    fclose(fp);
                    return 1;
                }
                continue; /* GOTO rec_loop; */
            }

			break;	/* force out of while loop */

			} 	/* end of WHILE loop that replaces 'rec_loop:' GOTO */

            if (chr == ACK)	/* additional while loop requires this */
                break;

            errcount++;
        }
        if (errcount >= MAXERRORS) {
            error(m);
            fclose(fp);
            return(1);
        }
        block++;
    }
    errcount = 0;
    while (errcount < MAXERRORS) {
        usputc(m->user,EOT);
        usflush(m->user);
        if (getchar_t(m->user) == ACK)
        {
            fclose(fp);
            j2pause(6000);
            log(m->user,"Xmodem: Download - %s",tfile);
            print_text(m,"Xmodem: File sent OK\n");
            return(0);
        }
        errcount++;
    }
    fclose(fp);
    j2pause(3000);
    error(m);
    return(1);
}

/* 30Dec2004, Replace 'nak:' LABEL with this function */
static unsigned char do_nak (struct mbx *m, unsigned char errcount)
{
	usputc(m->user,NAK);	/* do it over */
	usflush(m->user);
	errcount++;
	return errcount;
}
  
/* receive a file from the remote */
static int
xrecvfile(char *tfile,struct mbx *m)
{
    FILE *fp;
    unsigned char hdr, blk, cblk, tmp, cksum, crc1, crc2;
    unsigned char c1, c2, sum, block, sector[SECSIZE];
    unsigned char first, mode, errcount, two_can;
    int i;
  
    if (!access(tfile, 00)) {
        print_text(m,"xmodem: File %s already exists\n",tfile);
        return(1);
    }
  
    if ((fp = fopen(tfile, WRITE_BINARY)) == NULLFILE) {
        print_text(m,"xmodem: Can't open '%s' for write\n", tfile);
        return(1);
    }
    print_text(m,"File open - ready to receive\n");
    print_text(m,"To cancel: use CTRL-X numerous times\n");
    j2pause(3000);
    rawmode(m);
    errcount = 0;
    block = 1;
    first=0;
    two_can=0;
  
    j2pause(3000);
    while (errcount < MAXERRORS) {
        if (errcount < (MAXERRORS / 2)) {
            usputc(m->user,'C');                /* try CRC mode first */
            usflush(m->user);
            mode = 1;
        }
        else {
            usputc(m->user,NAK);                /* then checksum */
            usflush(m->user);
            mode = 0;
        }
        if ((hdr = getchar_t(m->user)) == SOH) {
            first=1;
            break;
        }
        if (hdr == CAN) {
            if (two_can){
                j2pause(3000);
                print_text(m,"\nxmodem: Abort request received\n");
                fclose(fp);
                unlink(tfile);
                return(1);
            }
            two_can=1;
        } else {
            two_can=0;
        }
        errcount++;
    }
    if (errcount >= MAXERRORS) {
        j2pause(3000);
        print_text(m,"\nxmodem: Timed out on acknowledge\n");
        fclose(fp);
        unlink(tfile);
        return(1);
    }
    errcount = 0;
    two_can=0;
    while (errcount < MAXERRORS) {
  
        if (first) {
            hdr=SOH;
            first=0;
        } else
            hdr = getchar_t(m->user);
  
        if (hdr == CAN) {
            if (two_can){
                j2pause(3000);
                print_text(m,"\nxmodem: Abort request received\n");
                fclose(fp);
                unlink(tfile);
                return(1);
            }
            two_can=1;
            continue;
        } else {
            two_can=0;
        }
  
        if (hdr == EOT)                        /* done! */
            break;
  
        if (hdr != SOH) {             /* read in junk for 6 seconds */
            j2alarm(6000);
            while(errno != EALARM)
                rrecvchar(m->user);
            j2alarm(0);   /* cancel alarm */
            first=0;
			errcount = do_nak (m, errcount);
			continue;
        }
        blk = getchar_t(m->user);
        cblk = getchar_t(m->user);
        crc1 = 0;
        crc2 = 0;
        sum = 0;
        for (i=0; i < SECSIZE; i++) {
            sector[i] = getchar_t(m->user);
            if (mode)
                update_crc(sector[i],&crc1,&crc2);
            else
                sum += sector[i];
        }
        if (mode) {
            c1 = getchar_t(m->user);
            c2 = getchar_t(m->user);
        }
        else
            cksum = getchar_t(m->user);
        if (blk != block && blk != (block - 1))
		{
			errcount = do_nak (m, errcount);
			continue;
		}
        tmp = ~blk;
        if (cblk != tmp)
		{
			errcount = do_nak (m, errcount);
			continue;
		}
        if (mode) {
            update_crc(0,&crc1,&crc2);
            update_crc(0,&crc1,&crc2);
            if (c1 != crc1 || c2 != crc2)
			{
				errcount = do_nak (m, errcount);
				continue;
			}
        }
        else {
            if (cksum != sum)
			{
				errcount = do_nak (m, errcount);
				continue;
			}
        }
        if (block == blk) {
            fflush(fp);
            if (fwrite(sector, sizeof(sector[0]), SECSIZE, fp)!=SECSIZE){
                error(m);
                print_text(m,"         File write error - Partial file deleted\n");
                fclose(fp);
                unlink(tfile);
                return (1);
            }
        }
        block = blk + 1;
        usputc(m->user,ACK);                        /* got it! */
        usflush(m->user);
        errcount = 0;
        continue;
  
		errcount = do_nak (m, errcount);
    }
    if (errcount == MAXERRORS) {
        error(m);
        fclose(fp);
        unlink(tfile);
        return(1);
    }
    usputc(m->user,ACK);
    usflush(m->user);
    j2pause(3000);
    fclose(fp);
    log(m->user,"Xmodem: Upload - %s",tfile);
    print_text(m,"Xmodem: File received OK\n");
    return(0);
}
  
/* exceeded the maximum number of retry's */
static void
error(struct mbx *m)
{
    int i;
  
    for(i=0;i<9;i++) {
        usputc(m->user,CAN);
    }
    usflush(m->user);
    j2pause(1000);
    for(i=0;i<9;i++) {
        usputc(m->user,BS);
    }
    usflush(m->user);
    j2pause(3000);
    print_text(m,"\nxmodem: Exceeded error limit...aborting\n");
    return;
}
  
/* update the CRC bytes */
static void
update_crc(unsigned char c ,unsigned char *crc1 ,unsigned char *crc2)
{
    register int i, temp;
    register unsigned char carry, c_crc1, c_crc2;
  
    for (i=0; i < 8; i++) {
        temp = c * 2;
        c = temp;                        /* rotate left */
        carry = ((temp > 255) ? 1 : 0);
        temp = *crc2 * 2;
        *crc2 = temp;
        *crc2 |= carry;                        /* rotate with carry */
        c_crc2 = ((temp > 255) ? 1 : 0);
        temp = *crc1 * 2;
        *crc1 = temp;
        *crc1 |= c_crc2;
        c_crc1 = ((temp > 255) ? 1 : 0);
        if (c_crc1) {
            *crc2 ^= 0x21;
            *crc1 ^= 0x10;
        }
    }
    return;
}
  
/* getchar with a 5 sec time out */
static char
getchar_t(int s)
{
    char c;
        /* only have 5 sec... */
    j2alarm(5000);
        /* Wait for something to happen */
    c=rrecvchar(s);
    j2alarm(0);
    return(c);
}
  
/* put the stdin/stdout in the "raw" mode */
static void
rawmode(struct mbx *m)
{
    seteol(m->user,0);
    seteol(m->tip->s,0);
    sockmode(m->tip->s,SOCK_BINARY);
    sockmode(m->user,SOCK_BINARY);
    m->tip->raw=1;
}
  
static void
restoremode(struct mbx *m)
{
    while(socklen(m->user,0) != 0)
        recv_mbuf(m->user,NULL,0,NULLCHAR,0);
    seteol(m->user,"\n");
    seteol(m->tip->s,"\n");
    sockmode(m->user,SOCK_ASCII);
    sockmode(m->tip->s,SOCK_ASCII);
    m->tip->raw=0;
}
  
void
print_text(struct mbx *m,char *fmt, ...)
{
    va_list ap;
    char buf[80];
  
    restoremode(m);
    va_start(ap,fmt);
    vsprintf(buf,fmt,ap);
    va_end(ap);
    usputs(m->user,buf);
    usflush(m->user);
}
  
#endif
#endif
