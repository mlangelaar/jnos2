/* Internet Control Message Protocol (ICMP)
 * Copyright 1991 Phil Karn, KA9Q
 */
#include "global.h"
#include "mbuf.h"
#include "iface.h"
#include "ip.h"
#include "icmp.h"
#include "netuser.h"
#ifdef UNIX
#define printf tcmdprintf
#endif
  
struct mib_entry Icmp_mib[] = {
    { "",                     { 0 } },
    { "icmpInMsgs",           { 0 } },
    { "icmpInErrors",         { 0 } },
    { "icmpInDestUnreachs",   { 0 } },
    { "icmpInTimeExcds",      { 0 } },
    { "icmpInParmProbs",      { 0 } },
    { "icmpInSrcQuenchs",     { 0 } },
    { "icmpInRedirects",      { 0 } },
    { "icmpInEchos",          { 0 } },
    { "icmpInEchoReps",       { 0 } },
    { "icmpInTimestamps",     { 0 } },
    { "icmpInTimestampReps",  { 0 } },
    { "icmpInAddrMasks",      { 0 } },
    { "icmpInAddrMaskReps",   { 0 } },
    { "icmpOutMsgs",          { 0 } },
    { "icmpOutErrors",        { 0 } },
    { "icmpOutDestUnreachs",  { 0 } },
    { "icmpOutTimeExcds",     { 0 } },
    { "icmpOutParmProbs",     { 0 } },
    { "icmpOutSrcQuenchs",    { 0 } },
    { "icmpOutRedirects",     { 0 } },
    { "icmpOutEchos",         { 0 } },
    { "icmpOutEchoReps",      { 0 } },
    { "icmpOutTimestamps",    { 0 } },
    { "icmpOutTimestampReps", { 0 } },
    { "icmpOutAddrMasks",     { 0 } },
    { "icmpOutAddrMaskReps",  { 0 } }
};

/* Process an incoming ICMP packet */
void
icmp_input(iface,ip,bp,rxbroadcast)
struct iface *iface;    /* Incoming interface (ignored) */
struct ip *ip;          /* Pointer to decoded IP header structure */
struct mbuf *bp;        /* Pointer to ICMP message */
int rxbroadcast;
{
    struct icmplink *ipp;
    struct mbuf *tbp;
    struct icmp icmp;       /* ICMP header */
    struct ip oip;          /* Offending datagram header */
    int16 type;             /* Type of ICMP message */
    int16 length;
    int ok_trace;
    char temp[40];
  
    icmpInMsgs++;
    if(rxbroadcast){
        /* Broadcast ICMP packets are to be IGNORED !! */
        icmpInErrors++;
        free_p(bp);
        return;
    }
    length = ip->length - IPLEN - ip->optlen;
    if(cksum(NULLHEADER,bp,length) != 0){
        /* Bad ICMP checksum; discard */
        icmpInErrors++;
        free_p(bp);
        return;
    }
    ntohicmp(&icmp,&bp);
  
    /* Process the message. Some messages are passed up to the protocol
     * module for handling, others are handled here.
     */
    type = icmp.type;
  
    switch(uchar(type)){
        case ICMP_TIME_EXCEED:  /* Time-to-live Exceeded */
        case ICMP_DEST_UNREACH: /* Destination Unreachable */
        case ICMP_QUENCH:       /* Source Quench */
        switch(uchar(type)){
            case ICMP_TIME_EXCEED:  /* Time-to-live Exceeded */
                icmpInTimeExcds++;
                break;
            case ICMP_DEST_UNREACH: /* Destination Unreachable */
                icmpInDestUnreachs++;
                break;
            case ICMP_QUENCH:       /* Source Quench */
                icmpInSrcQuenchs++;
                break;
        }
            ntohip(&oip,&bp);       /* Extract offending IP header */
            if(Icmp_trace){
                switch(uchar(type)){
                    case ICMP_TIME_EXCEED:
                        ok_trace=1;
                        sprintf(temp,"%s",
                        smsg(Exceed,NEXCEED,uchar(icmp.code)));
                        break;
                    case ICMP_DEST_UNREACH:
                        ok_trace=1;
                        sprintf(temp,"%s",
                        smsg(Unreach,NUNREACH,uchar(icmp.code)));
                        break;
                    default:
                        ok_trace=0;
                        sprintf(temp,"%u",uchar(icmp.code));
                        break;
                }
  
                if ((Icmp_trace==1) || (Icmp_trace==2 && ok_trace)){
  
                    printf("ICMP from %s:",inet_ntoa(ip->source));
                    printf(" dest %s %s ",inet_ntoa(oip.dest),
                    smsg(Icmptypes,ICMP_TYPES,uchar(type)));
                    printf(" %s\n",temp);
  
                }
            }
            for(ipp = Icmplink;ipp->funct != NULL;ipp++)
                if(ipp->proto == oip.protocol)
                    break;
            if(ipp->funct != NULL){
                (*ipp->funct)(ip->source,oip.source,oip.dest,icmp.type,
                icmp.code,&bp);
            }
            break;
        case ICMP_ECHO:         /* Echo Request */
        /* Change type to ECHO_REPLY, recompute checksum,
         * and return datagram.
         */
            icmpInEchos++;
            icmp.type = ICMP_ECHO_REPLY;
            if((tbp = htonicmp(&icmp,bp)) == NULLBUF){
                free_p(bp);
                return;
            }
            icmpOutEchoReps++;
            ip_send(ip->dest,ip->source,ICMP_PTCL,ip->tos,0,tbp,length,0,0);
            return;
        case ICMP_REDIRECT:     /* Redirect */
            icmpInRedirects++;
            break;
        case ICMP_PARAM_PROB:   /* Parameter Problem */
            icmpInParmProbs++;
            break;
        case ICMP_ECHO_REPLY:   /* Echo Reply */
            icmpInEchoReps++;
            echo_proc(ip->source,ip->dest,&icmp,bp);
            bp = NULLBUF;   /* so it won't get freed */
            break;
        case ICMP_TIMESTAMP:    /* Timestamp */
            icmpInTimestamps++;
            break;
        case ICMP_TIME_REPLY:   /* Timestamp Reply */
            icmpInTimestampReps++;
            break;
        case ICMP_INFO_RQST:    /* Information Request */
            break;
        case ICMP_INFO_REPLY:   /* Information Reply */
            break;
    }
    free_p(bp);
}
/* Return an ICMP response to the sender of a datagram.
 * Unlike most routines, the callER frees the mbuf.
 */
int
icmp_output(ip,data,type,code,args)
struct ip *ip;          /* Header of offending datagram */
struct mbuf *data;      /* Data portion of datagram */
char type,code;         /* Codes to send */
union icmp_args *args;
{
    struct mbuf *bp = NULLBUF;
    struct icmp icmp;       /* ICMP protocol header */
    int16 dlen;             /* Length of data portion of offending pkt */
    int16 length;           /* Total length of reply */
  
    if(ip == NULLIP)
        return -1;
    if(uchar(ip->protocol) == ICMP_PTCL){
        /* Peek at type field of ICMP header to see if it's safe to
         * return an ICMP message
         */
        switch(uchar(data->data[0])){
            case ICMP_ECHO_REPLY:
            case ICMP_ECHO:
            case ICMP_TIMESTAMP:
            case ICMP_TIME_REPLY:
            case ICMP_INFO_RQST:
            case ICMP_INFO_REPLY:
                break;  /* These are all safe */
            default:
            /* Never send an ICMP error message about another
             * ICMP error message!
             */
                return -1;
        }
    }
    /* Compute amount of original datagram to return.
     * We return the original IP header, and up to 8 bytes past that.
     */
    dlen = min(8,len_p(data));
    length = dlen + ICMPLEN + IPLEN + ip->optlen;
    /* Take excerpt from data portion */
    if(data != NULLBUF && dup_p(&bp,data,0,dlen) == 0)
        return -1;      /* The caller will free data */
  
    /* Recreate and tack on offending IP header */
    if((data = htonip(ip,bp,IP_CS_NEW)) == NULLBUF){
        free_p(bp);
        icmpOutErrors++;
        return -1;
    }
    icmp.type = type;
    icmp.code = code;
    icmp.args.unused = 0;
    switch(uchar(icmp.type)){
        case ICMP_PARAM_PROB:
            icmpOutParmProbs++;
            icmp.args.pointer = args->pointer;
            break;
        case ICMP_REDIRECT:
            icmpOutRedirects++;
            icmp.args.address = args->address;
            break;
        case ICMP_ECHO:
            icmpOutEchos++;
            break;
        case ICMP_ECHO_REPLY:
            icmpOutEchoReps++;
            break;
        case ICMP_INFO_RQST:
            break;
        case ICMP_INFO_REPLY:
            break;
        case ICMP_TIMESTAMP:
            icmpOutTimestamps++;
            break;
        case ICMP_TIME_REPLY:
            icmpOutTimestampReps++;
            icmp.args.echo.id = args->echo.id;
            icmp.args.echo.seq = args->echo.seq;
            break;
        case ICMP_ADDR_MASK:
            icmpOutAddrMasks++;
            break;
        case ICMP_ADDR_MASK_REPLY:
            icmpOutAddrMaskReps++;
            break;
        case ICMP_DEST_UNREACH:
            if(icmp.code == ICMP_FRAG_NEEDED)
                icmp.args.mtu = args->mtu;
            icmpOutDestUnreachs++;
            break;
        case ICMP_TIME_EXCEED:
            icmpOutTimeExcds++;
            break;
        case ICMP_QUENCH:
            icmpOutSrcQuenchs++;
            break;
    }
  
    icmpOutMsgs++;
    /* Now stick on the ICMP header */
    if((bp = htonicmp(&icmp,data)) == NULLBUF){
        free_p(data);
        return -1;
    }
    return ip_send(INADDR_ANY,ip->source,ICMP_PTCL,ip->tos,0,bp,length,0,0);
}
