/* Miscellaneous interger and IP address format conversion subroutines
 * Copyright 1991 Phil Karn, KA9Q
 */
#define LINELEN 256
#include <ctype.h>
#include "global.h"
#include "netuser.h"
#include "domain.h"
  
int Net_error;
  
/* Convert Internet address in ascii dotted-decimal format (44.0.0.1) to
 * binary IP address
 */
int32
aton(s)
register char *s;
{
    int32 n;
  
    register int i;
  
    n = 0;
    if(s == NULLCHAR)
        return 0;
    for(i=24;i>=0;i -= 8){
        /* Skip any leading stuff (e.g., spaces, '[') */
        while(*s != '\0' && !isdigit(*s))
            s++;
        if(*s == '\0')
            break;
        n |= (int32)atoi(s) << i;
        if((s = strchr(s,'.')) == NULLCHAR)
            break;
        s++;
    }
    return n;
}
  
/* Convert an internet address (in host byte order) to a dotted decimal ascii
 * string, e.g., 255.255.255.255\0
 */
char *
inet_ntoa(a)
int32 a;
{
    static char buf[25];
    char *name;
  
    if(DTranslate && (name = resolve_a(a,!DVerbose)) != NULLCHAR) {
        strncpy(buf, name, 24);
        buf[24] = '\0';
        free(name);
    } else {
        sprintf(buf,"%u.%u.%u.%u",
        hibyte(hiword(a)),
        lobyte(hiword(a)),
        hibyte(loword(a)),
        lobyte(loword(a)) );
    }
    return buf;
}
  
/* Convert an internet address (in host byte order) to a unformated string
 */
char *
inet_ntobos(a)
int32 a;
{
    static char buf[5];
  
    buf[0]=hibyte(hiword(a));
    buf[1]=lobyte(hiword(a));
    buf[2]=hibyte(loword(a));
    buf[3]=lobyte(loword(a));
    buf[4]='\0';
  
    return buf;
}
  
/* Convert hex-ascii string to long integer */
long
htol(s)
char *s;
{
    long ret;
    char c;
  
    ret = 0;
    while((c = *s++) != '\0'){
        c &= 0x7f;
        if(c == 'x')
            continue;       /* Ignore 'x', e.g., '0x' prefixes */
        if(c >= '0' && c <= '9')
            ret = ret*16 + (c - '0');
        else if(c >= 'a' && c <= 'f')
            ret = ret*16 + (10 + c - 'a');
        else if(c >= 'A' && c <= 'F')
            ret = ret*16 + (10 + c - 'A');
        else
            break;
    }
    return ret;
}
char *
pinet(s)
struct socket *s;
{
    static char buf[80];
    char port[10];
  
    switch(s->port) {
        case 7:                 /* Echo data port */
            sprintf(port,"echo");
            break;
        case 9:                 /* Discard data port */
            sprintf(port,"discard");
            break;
        case 20:                /* FTP Data port */
            sprintf(port,"ftpd");
            break;
        case 21:                /* FTP Control port */
            sprintf(port,"ftp");
            break;
        case 23:                /* Telnet port */
            sprintf(port,"telnet");
            break;
        case 25:                /* Mail port */
            sprintf(port,"smtp");
            break;
        case 37:        /* Time port */
            sprintf(port,"time");
            break;
        case 53:        /* Domain Nameserver */
            sprintf(port,"domain");
            break;
        case 69:                /* TFTP Data port */
            sprintf(port,"tftpd");
            break;
        case 79:                /* Finger port */
            sprintf(port,"finger");
            break;
        case 80:                /* HTTP port */
            sprintf(port, "http");
            break;
        case 87:                /* Ttylink port */
            sprintf(port,"ttylink");
            break;
        case 109:       /* POP2 port */
            sprintf(port,"pop2");
            break;
        case 110:       /* POP3 port */
            sprintf(port,"pop3");
            break;
        case 113:       /* Ident port */
            sprintf(port,"ident");
            break;
        case 119:               /* NNTP port */
            sprintf(port,"nntp");
            break;
        case 520:               /* Routing Information Protocol */
            sprintf(port,"rip");
            break;
        case 1234:              /* Pulled out of the air */
            sprintf(port,"remote");
            break;
        case 3600:
            sprintf(port,"convers");
            break;
/* 02Sep2020, Maiko (VE4KLM), Adding in mods from Brian (N1URO) */
#ifdef TNODE
        case 3694:              /* URONode inbound tcp port - N1URO*/
            sprintf(port,"node");
            break;
#endif
        default:
            sprintf(port,"%u",s->port);
            break;
    }
  
    sprintf(buf,"%s:%s",inet_ntoa(s->address),port);
    return buf;
}
  
