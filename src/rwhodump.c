/* Tracing to sockets, mods April '95 by Johan. K. Reinalda, WG7J */
#include <time.h>
#include "global.h"
#include "mbuf.h"
#include "trace.h"
#include "socket.h"  

#ifndef __GNUC__
static
#endif
struct outmp {
    char out_line[9];   /* tty name */
    char out_name[9];   /* user id */
    int32 out_time; /* time on */
};
  
#ifndef __GNUC__
static
#endif
struct whod {
    char wd_vers;
    char wd_type;
    char wd_fill[2];
    int32  wd_sendtime;
    int32  wd_recvtime;
    char wd_hostname[33];
    int32  wd_loadav[3];
    int32  wd_boottime;
    struct    whoent {
        struct    outmp we_utmp;
        int32  we_idle;
    } wd_we[1024 / sizeof (struct whoent)];
};
  
static int ntohrwhod __ARGS((struct whod *wp,struct mbuf **bpp));
  
void
rwho_dump(s,bpp)
int s;
struct mbuf **bpp;
{
    int i;
    int32 t;
    char *cp;
    struct whod rwhod;
    if(bpp == NULLBUFP || *bpp == NULLBUF)
        return;
    usprintf(s,"RWHO: ");
    if(ntohrwhod(&rwhod,bpp) == -1) {
        usprintf(s,"bad data\n");
        return;
    }
    if(rwhod.wd_vers != 1)
        usprintf(s,"version %d  ",rwhod.wd_vers);
    if(rwhod.wd_type != 1)
        usprintf(s,"type %d  ",rwhod.wd_type);
    cp = ctime(&rwhod.wd_sendtime);
    usprintf(s,"send %.24s  ",cp);
    t = rwhod.wd_recvtime;
    if(t / 86400L)
        usprintf(s,"%ld:",t/86400L);
    t %= 86400L;
    usprintf(s,"recv %02ld:%02ld\n",t / 3600,(t % 3600)/60);
    usprintf(s,"      host %s  loadvg %ld %ld %ld  ",rwhod.wd_hostname,
    rwhod.wd_loadav[0],rwhod.wd_loadav[1],
    rwhod.wd_loadav[2]);
    usprintf(s,"boot %s",ctime(&rwhod.wd_boottime));
    i = 0;
    while(rwhod.wd_we[i].we_utmp.out_line[0] != '\0') {
        usprintf(s,"      %-12s%-12s",rwhod.wd_we[i].we_utmp.out_name,
        rwhod.wd_we[i].we_utmp.out_line);
        t = rwhod.wd_we[i].we_idle;
        if(t / 86400L)
            usprintf(s,"%ld:",t/86400L);
        else
            usprintf(s,"  ");
        t %= 86400L;
        usprintf(s,"%02ld:%02ld    ",t / 3600, (t % 3600)/60);
        usprintf(s,"%s",ctime(&rwhod.wd_we[i].we_utmp.out_time));
        ++i;
    }
}
static int
ntohrwhod(wp,bpp)
struct whod *wp;
struct mbuf **bpp;
{
    int i;
    char wbuf[60];
    if(pullup(bpp,wbuf,60) != 60)
        return -1;
    wp->wd_vers = wbuf[0];
    wp->wd_type = wbuf[1];
    wp->wd_fill[0] = wbuf[2];
    wp->wd_fill[1] = wbuf[3];
    wp->wd_sendtime = get32(&wbuf[4]);
    wp->wd_recvtime = get32(&wbuf[8]);
    memcpy(wp->wd_hostname,&wbuf[12],32);
    wp->wd_hostname[32] = '\0';
    wp->wd_loadav[0] = get32(&wbuf[44]);
    wp->wd_loadav[1] = get32(&wbuf[48]);
    wp->wd_loadav[2] = get32(&wbuf[52]);
    wp->wd_boottime = get32(&wbuf[56]);
    for(i = 0; i < 42; ++i) {
        if(pullup(bpp,wbuf,24) != 24) {
            wp->wd_we[i].we_utmp.out_line[0] = '\0';
            return 0;
        }
        memcpy(wp->wd_we[i].we_utmp.out_line,wbuf,8);
        wp->wd_we[i].we_utmp.out_line[8] = '\0';
        memcpy(wp->wd_we[i].we_utmp.out_name,&wbuf[8],8);
        wp->wd_we[i].we_utmp.out_name[8] = '\0';
        wp->wd_we[i].we_utmp.out_time = get32(&wbuf[16]);
        wp->wd_we[i].we_idle = get32(&wbuf[20]);
    }
    return 0;
}
