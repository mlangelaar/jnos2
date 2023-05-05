/*      SM0RGV data compression routines.
 * This file implements the Lempel-Ziv Welch (LZW) data compression
 * algorithm but with a variable code size, as in GIF format files.
 *
 * Copyright 1990 by Anders Klemets, SM0RGV. Permission granted for
 * non-commercial distribution only.
 */
  
#include <ctype.h>
#include "global.h"
#ifdef  LZW
#include "mbuf.h"
#include "proc.h"
#include "lzw.h"
#include "socket.h"
#include "usock.h"
#include "session.h"
#include "cmdparse.h"
#include "commands.h"

static int Lzwtrace = 0;
  
static void fastencode __ARGS((struct usock *up,char data));
static void morebits __ARGS((struct lzw *lzw));
static void cleartbl __ARGS((struct lzw *lzw));
static void addtobuffer __ARGS((struct usock *up,int32 code));
static void addchar __ARGS((char data,struct lzw *lzw));
static void getstring __ARGS((int32 code,struct lzw *lzw));
static char firstchar __ARGS((int32 code,struct lzw *lzw));
static void decodetbl __ARGS((int32 code,struct lzw *lzw));
static int32 nextcode __ARGS((struct usock *up));

extern void rxtx_data_compute __ARGS((struct tcb *tcb,int32 *sent,int32 *recvd));

int
dolzw(argc,argv,p)
int argc;
char *argv[];
void *p;
{
    if(argc > 1) {
        switch(tolower(*argv[1])) {
            case 'm':
                if(argc == 2) {
                    tprintf("LZW mode: %s\n",Lzwmode ? "fast" : "compact");
                } else {
                    Lzwmode = (tolower(*argv[2]) == 'f');
                }
                return 0;
            case 'b':
                argc--;
                argv++;
                return setintrc(&Lzwbits,"LZW bits",argc,argv,9,16);
            case 't':
                argc--;
                argv++;
                return setbool(&Lzwtrace,"Trace LZW compression ratios",argc,argv);
            case '=':
                if(argc == 3) {
                    struct session *sp;
                    if((sp = sessptr(argv[2])) != NULLSESSION) {
                        lzwinit(sp->s,Lzwbits,Lzwmode);
                    }
                }
                return 0;
        }
    }
    j2tputs("Usage: lzw <mode|bits|trace> <value>\n");
    return -1;
}
  
/* Initialize a socket for compression. Bits specifies the maximum number
 * of bits per codeword. The input and output code tables might grow to
 * about (2^bits * 3) bytes each. The bits parameter does only serve as a
 * recommendation for the decoding, the actual number of bits may be
 * larger, but not never more than 16.
 */
void
lzwinit(s,bits,mode)
int s;      /* socket to operate on */
int bits;   /* maximum number of bits per codeword */
int mode;   /* compression mode, LZWCOMPACT or LZWFAST */
{
    struct usock *up;
    if((up = itop(s)) == NULLUSOCK)
        return;
    up->zout = (struct lzw *) callocw(1,sizeof(struct lzw));
    up->zin = (struct lzw *) callocw(1,sizeof(struct lzw));
    up->zout->codebits = LZWBITS;
    if(bits < LZWBITS)
        up->zout->maxbits = LZWBITS;
    if(bits > 16 || bits == 0)
        up->zout->maxbits = 16;
    if(bits >= LZWBITS && bits <= 16)
        up->zout->maxbits = bits;
    up->zout->nextbit = 0;
    up->zout->prefix = -1;
    up->zout->version = -1;
    up->zout->code = -1;
    up->zout->mode = mode;
    up->zout->next = ZFLUSH + 1;    /* used only in LZWFAST mode */
    up->zin->codebits = LZWBITS;
    up->zin->maxbits = -1;
    up->zin->nextbit = 0;
    up->zin->prefix = -1;
    up->zin->version = -1;
    up->zin->code = -1;
    up->zin->next = ZFLUSH + 1;
    up->zin->mode = LZWCOMPACT;
    up->zin->buf = NULLBUF;
}
  
void
lzwfree(up)
struct usock *up;
{
    int32 sent=0, recvd=0;

    if (Lzwtrace) {
        if (up->type == TYPE_TCP && up->cb.tcb)
            rxtx_data_compute(up->cb.tcb,&sent,&recvd);

        if (up->zout != NULLLZW || up->zin != NULLLZW)
            j2tputs("Compression ratios: compressed/uncompressed:\n");
    }

    if(up->zout != NULLLZW) {
	/* 12Oct2009, Maiko, Use "%d" for int32 vars */
        if (Lzwtrace && up->zout->cnt != 0L)
            tprintf("Output: %d/%d = %d\n",
                sent, up->zout->cnt,
                (up->zout->cnt - sent)*100/up->zout->cnt);
        cleartbl(up->zout);
        free((char *)up->zout);
        up->zout = NULLLZW;
    }
    if(up->zin != NULLLZW) {
	/* 12Oct2009, Maiko, Use "%d" for int32 vars */
        if (Lzwtrace && up->zin->cnt != 0L)
            tprintf("Input:  %d/%d = %d\n",
                recvd, up->zin->cnt, 
                (up->zin->cnt - recvd)*100/up->zin->cnt);
        cleartbl(up->zin);
        free_q(&up->zin->buf);
        free((char *)up->zin);
        up->zin = NULLLZW;
    }
}
/* LZW encoding routine.
 * See if the string specified by code + data is in the string table. If so,
 * set prefix equal to the code of that string. Otherwise insert code + data
 * in the string and set prefix equal to data.
 */
void
lzwencode(s,data)
int s;
char data;
{
    struct usock *up;
    register struct lzw *lzw;
    int32 code, pagelim;
    register unsigned int i,j;
  
    if((up = itop(s)) == NULLUSOCK)
        return;
    lzw = up->zout;
    code = up->zout->prefix;
    /* the first byte sent will be the version number */
    if(lzw->version == -1) {
        lzw->version = ZVERSION;
        up->obuf->data[up->obuf->cnt++] = lzw->version;
        /* then send our recommended maximum number of codebits */
        up->obuf->data[up->obuf->cnt++] = lzw->maxbits;
    }
    lzw->cnt++;
    if(code == -1) {
        lzw->prefix = (int32) uchar(data);
        return;
    }
    if(lzw->mode == LZWFAST) {
        fastencode(up,data);
        return;
    }
    pagelim = ((int32) 1 << lzw->codebits) / LZWBLK + 1;
    if(code <= ZFLUSH)
        i = j = 0;
    else {
        i = (code - ZFLUSH) / LZWBLK;
        j = (code - ZFLUSH) % LZWBLK;
    }
    if(lzw->tu.tbl == (struct zentry **)0)
        lzw->tu.tbl = (struct zentry **) callocw(1,
        sizeof(struct zentry *) * pagelim);
    for(;;) {
        if(itop(s) == NULLUSOCK) /* the connection has been closed */
            return;
        if(up->zout == NULLLZW) /* ...or is about to be closed */
            return;
        if(lzw->tu.tbl[i] == (struct zentry *)0) {
            lzw->tu.tbl[i] = (struct zentry *)
            mallocw(LZWBLK * sizeof(struct zentry));
            memset((char *)lzw->tu.tbl[i], 0xff,
            LZWBLK * sizeof(struct zentry));
        }
        while(j < LZWBLK) {
            if(lzw->tu.tbl[i][j].data == data &&
            lzw->tu.tbl[i][j].code == (int16) code) {
                lzw->prefix = (int32)(i * LZWBLK + j +
                ZFLUSH + 1);
                return;
            }
            if(lzw->tu.tbl[i][j].code == 0xffff) {
                lzw->tu.tbl[i][j].code = (int16) code;
                lzw->tu.tbl[i][j].data = data;
                addtobuffer(up,code);
                lzw->prefix = (int32) uchar(data);
                lzw->next++;
                if(lzw->next == (int32) 1 << lzw->codebits)
                    /* the current table is now full */
                    morebits(lzw);
                if(lzw->next + 1 == (int32)
                1 << lzw->maxbits) {
                /* The last codeword has been reached.
                 * (Or last - 1.) Signal this and start all
                 * over again.
                 */
                    addtobuffer(up,lzw->prefix);
                    if(lzw->next + 1 == (int32)
                        1 << lzw->codebits)
                        morebits(lzw);
                    addtobuffer(up,ZCC);
                    cleartbl(lzw);
                }
                return;
            }
            ++j;
        }
        pwait(NULL);
        ++i;
        j = 0;
    }
}
  
/* Used when LZWFAST mode is selected. Memory usage approx. (2^bits * 5)
 * bytes.
 */
static void
fastencode(up,data)
struct usock *up;
char data;
{
    register struct zfast *z;
    register struct mbuf *bp;
    register struct lzw *lzw = up->zout;
    int32 code = up->zout->prefix;
    register int16 cnt, h;
  
    if(lzw->tu.bpp == NULLBUFP)
        lzw->tu.bpp = (struct mbuf **) callocw(1,
        sizeof(struct mbuf *) * ZHASH);
    h = lobyte(code);
    h ^= hibyte(code);
    h ^= data;
    h = h % ZHASH;
    if(lzw->tu.bpp[h] == NULLBUF)
        lzw->tu.bpp[h] = ambufw(LZWBLK);
    bp = lzw->tu.bpp[h];
    cnt = bp->cnt / sizeof(struct zfast);
    z = (struct zfast *) bp->data;
    while(cnt > 0) {
        if(z->data == data && z->code == (int16) code) {
            lzw->prefix = (int32) z->owncode;
            return;
        }
        z++;
        if(--cnt == 0) {
            if(bp->next == NULLBUF)
                break;
            bp = bp->next;
            cnt = bp->cnt / sizeof(struct zfast);
            z = (struct zfast *) bp->data;
        }
    }
    if(bp->size - bp->cnt >= (int16)sizeof(struct zfast)) {
        z = (struct zfast *) &bp->data[bp->cnt];
        bp->cnt += sizeof(struct zfast);
    }
    else {
        bp->next = ambufw(LZWBLK);
        z = (struct zfast *) bp->next->data;
        bp->next->cnt = sizeof(struct zfast);
    }
    z->code = (int16) code;
    z->data = data;
    z->owncode = (int16) lzw->next++;
    addtobuffer(up,code);
    lzw->prefix = (int32) uchar(data);
    if(lzw->next == (int32) 1 << lzw->codebits) {
        /* Increase the number of bits per codeword */
        morebits(lzw);
    }
    if(lzw->next + 1 == (int32) 1 << lzw->maxbits) {
        /* The last codeword has been reached. (Or last - 1.)
         * Signal this and start all over again.
         */
        addtobuffer(up,lzw->prefix);
        if(lzw->next + 1 == (int32) 1 << lzw->codebits)
            morebits(lzw);
        addtobuffer(up,ZCC);
        cleartbl(lzw);
    }
    pwait(NULL);
}
  
/* increase the number of significant bits in the codewords, and increase
 * the size of the string table accordingly.
 */
static void
morebits(lzw)
struct lzw *lzw;
{
    struct zentry **newt;
    int32 pagelim, oldp;
    oldp = ((int32) 1 << lzw->codebits) / LZWBLK + 1;
    lzw->codebits++;
    if(lzw->mode == LZWFAST)
        return;
    pagelim = ((int32) 1 << lzw->codebits) / LZWBLK + 1;
    newt = (struct zentry **) callocw(1,sizeof(struct zentry *)*pagelim);
    memcpy(newt,lzw->tu.tbl,sizeof(struct zentry *) * oldp);
    free((char *)lzw->tu.tbl);
    lzw->tu.tbl = newt;
}
  
static void
cleartbl(lzw)
struct lzw *lzw;
{
    int32 pagelim,i;
    pagelim = ((int32) 1 << lzw->codebits) / LZWBLK + 1;
    lzw->codebits = LZWBITS;
    lzw->prefix = -1;
    lzw->next = ZFLUSH + 1;
    if(lzw->tu.p == NULL)
        return;
    if(lzw->mode == LZWCOMPACT)
        for(i=0; i < pagelim; ++i)
            free((char *)lzw->tu.tbl[i]);
    else
        for(i=0; i < ZHASH; ++i)
            free_p(lzw->tu.bpp[i]);
    free((char *)lzw->tu.p);
    lzw->tu.p = NULL;
}
/* Add a codeword to the code stream. Nextbit indicates at which bit in the
 * code stream should be written first. The codeword ZFLUSH is used to
 * pad the buffer to a byte boundary when the buffer will be flushed.
 * The remaining bits of the ZFLUSH codeword are sent in the next buffer.
 */
static void
addtobuffer(up,code)
struct usock *up;
int32 code;
{
    if(up->zout->code != -1) {
        /* Insert remaining bits of ZFLUSH codeword */
        up->obuf->data[up->obuf->cnt] =
        up->zout->code >> up->zout->flushbit;
        if(up->zout->flushbit + 8 >= up->zout->codebits) { /* done */
            up->zout->nextbit = (up->zout->codebits -
            up->zout->flushbit) % 8;
            if(up->zout->nextbit == 0)
                ++up->obuf->cnt;
            up->zout->code = -1;
        }
        else {
            /* not done yet */
            ++up->obuf->cnt;
            up->zout->flushbit += 8;
            addtobuffer(up,code);
            return;
        }
    }
    /* normal codewords are treated here */
    if(up->zout->nextbit == 0) {
        /* we are at a byte boundary */
        if(code == ZFLUSH) {
            up->zout->flushbit = 0;
            up->zout->code = ZFLUSH;
            return;
        }
        up->obuf->data[up->obuf->cnt++] = (char) code;
    }
    else
        up->obuf->data[up->obuf->cnt++] |= code << up->zout->nextbit;
    if(code == ZFLUSH) {
        /* interrupt here and save the rest of the ZFLUSH
         * codeword for later.
         */
        up->zout->flushbit = 8 - up->zout->nextbit;
        up->zout->code = ZFLUSH;
        return;
    }
    up->obuf->data[up->obuf->cnt] = code >> (8 - up->zout->nextbit);
    /* start on a third byte if necessary */
    if(16 - up->zout->nextbit < up->zout->codebits)
        up->obuf->data[++up->obuf->cnt] =
        code >> (16 - up->zout->nextbit);
    up->zout->nextbit = (up->zout->nextbit + up->zout->codebits) % 8;
    if(up->zout->nextbit == 0)
        ++up->obuf->cnt;
}
  
void
lzwflush(up)
struct usock *up;
{
    /* interrupt the encoding process and send the prefix codeword */
    if(up->zout->prefix != -1) {
        addtobuffer(up,up->zout->prefix);
        if(up->zout->next + 1 == (int32) 1 << up->zout->codebits)
            morebits(up->zout);
        up->zout->prefix = -1;
    }
    /* signal this by sending the ZFLUSH codeword */
    addtobuffer(up,ZFLUSH);
}
  
int
lzwdecode(up)
struct usock *up;
{
    int32 code,data;
    if(up->zin->version == -1 && (up->zin->version = PULLCHAR(&up->ibuf))
        == -1)
        return -1;
    if(up->zin->maxbits == -1) {
        /* receive a recommended value for the maximum no of bits */
        if((up->zin->maxbits = PULLCHAR(&up->ibuf)) == -1)
            return -1;
        if(up->zout->maxbits > up->zin->maxbits) {
            if(up->zout->codebits > up->zin->maxbits) {
                /* We are already using more bits than our
                 * peer want us to, so clear the encoding
                 * table immediately.
                 */
                addtobuffer(up,up->zout->prefix);
                if(up->zout->next + 1 == (int32)
                    1 << up->zout->codebits)
                    morebits(up->zout);
                addtobuffer(up,ZCC);
                cleartbl(up->zout);
            }
            up->zout->maxbits = up->zin->maxbits;
        }
    }
    for(;;) {
        if((data = PULLCHAR(&up->zin->buf)) != -1)
            return (int) data;
        if((code = nextcode(up)) == -1)
            return -1;
        decodetbl(code,up->zin);
        up->zin->cnt += len_p(up->zin->buf);
    }
}
  
static void
addchar(data,lzw)
char data;
struct lzw *lzw;
{
    lzw->buf = pushdown(lzw->buf,1);
    *lzw->buf->data = data;
}
static void
getstring(code,lzw)
int32 code;
struct lzw *lzw;
{
    int i,j;
    for(;;) {
        if(code < ZFLUSH) {
            addchar(uchar(code),lzw);
            return;
        }
        i = (code - ZFLUSH - 1) / LZWBLK;
        j = (code - ZFLUSH - 1) % LZWBLK;
        addchar(lzw->tu.tbl[i][j].data,lzw);
        code = (int32) lzw->tu.tbl[i][j].code;
    }
}
static char
firstchar(code,lzw)
int32 code;
struct lzw *lzw;
{
    int i,j;
    for(;;) {
        if(code < ZFLUSH)
            return uchar(code);
        i = (code - ZFLUSH - 1) / LZWBLK;
        j = (code - ZFLUSH - 1) % LZWBLK;
        code = (int32) lzw->tu.tbl[i][j].code;
    }
}
static void
decodetbl(code,lzw)
int32 code;
struct lzw *lzw;
{
    register unsigned int i,j;
    int32 pagelim;
  
    if(code > lzw->next) {
        log(Curproc->input,"LZW protocol error, process %s",Curproc->name);
        return;
    }
    if(lzw->buf == NULLBUF) {
        lzw->buf = ambufw(512);
        /* allow us to use pushdown() later */
        lzw->buf->data += lzw->buf->size;
    }
    if(lzw->prefix == -1) {
        getstring(code,lzw);
        lzw->prefix = code;
        return;
    }
    pagelim = ((int32) 1 << lzw->codebits) / LZWBLK + 1;
    if(lzw->tu.tbl == (struct zentry **)0)
        lzw->tu.tbl = (struct zentry **) callocw(1,
        sizeof(struct zentry *) * pagelim);

	if (code >= ZFLUSH)	/* replaces GOTO 'intable' LABEL */
	{

    i = (code - ZFLUSH - 1) / LZWBLK;
    j = (code - ZFLUSH - 1) % LZWBLK;
    if(lzw->tu.tbl[i] == (struct zentry *)0) {
        lzw->tu.tbl[i] = (struct zentry *)
        mallocw(LZWBLK * sizeof(struct zentry));
        memset((char *)lzw->tu.tbl[i], 0xff,
        LZWBLK * sizeof(struct zentry));
    }
    if(lzw->tu.tbl[i][j].code == 0xffff) {
        lzw->tu.tbl[i][j].data = firstchar(lzw->prefix,lzw);
        lzw->tu.tbl[i][j].code = (int16) lzw->prefix;
        getstring(code,lzw);
        lzw->next = code + 1;
        lzw->prefix = code;
        if(lzw->next + 1 == (int32) 1 << lzw->codebits)
            morebits(lzw);
        return;
    }

	}	/* end of (code >= ZFLUSH), replaces 'intable' LABEL */

    getstring(code,lzw);
    i = (lzw->next - ZFLUSH - 1) / LZWBLK;
    j = (lzw->next - ZFLUSH - 1) % LZWBLK;
    if(lzw->tu.tbl[i] == (struct zentry *)0) {
        lzw->tu.tbl[i] = (struct zentry *)
        mallocw(LZWBLK * sizeof(struct zentry));
        memset((char *)lzw->tu.tbl[i], 0xff,
        LZWBLK * sizeof(struct zentry));
    }
    lzw->tu.tbl[i][j].data = firstchar(code,lzw);
    lzw->tu.tbl[i][j].code = (int16) lzw->prefix;
    lzw->next++;
    lzw->prefix = code;
    if(lzw->next + 1 == (int32) 1 << lzw->codebits)
        morebits(lzw);
}
  
/* extract the next codeword from the input stream */
static int32
nextcode(up)
struct usock *up;
{
    int32 code;
    if(up->zin->code == -1) {   /* read the first character */
        if((code = PULLCHAR(&up->ibuf)) == -1)
            return -1;
        up->zin->code = code >> up->zin->nextbit;
    }
    if(up->ibuf == NULLBUF)     /* next byte is not available yet */
        return -1;
    /* continue assembling the codeword */
    up->zin->code |= ((int32) uchar(*up->ibuf->data) <<
        (8 - up->zin->nextbit)) & (((int32) 1 <<
        up->zin->codebits) - 1);
    if(16 - up->zin->nextbit < up->zin->codebits) {
        (void) PULLCHAR(&up->ibuf);
        up->zin->nextbit -= 8; /* pull bits from a third byte */
        return nextcode(up);
    }
    code = up->zin->code;
    up->zin->code = -1;
    up->zin->nextbit = (up->zin->nextbit + up->zin->codebits) % 8;
    if(up->zin->nextbit == 0)
        (void) PULLCHAR(&up->ibuf);
    if(code == ZCC) {
        cleartbl(up->zin);
        return nextcode(up);
    }
    if(code == ZFLUSH) {
        up->zin->prefix = -1;
        return nextcode(up);
    }
    return code;
}
#endif
