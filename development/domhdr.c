/* Domain header conversion routines
 * Copyright 1991 Phil Karn, KA9Q
 *
 * Additional support for the Domain Name Server
 * by Johan. K. Reinalda, WG7J,
 * based on previous work by Gerard v.d. Grinten, PA0GRI
 */
#include "global.h"
#include "mbuf.h"
#include "domain.h"
  
struct dict {
    struct dict *next;
    char *nm;          /* name already stored into reply buf */
    int16 nmoff;       /* offset to name in reply buf */
};
#define NULLDICT (struct dict *)0

static int dn_expand __ARGS((char *msg,char *compressed,char *full,int fullen));
static char *getq __ARGS((struct rr **rrpp,char *msg,char *cp));
static char *ntohrr __ARGS((struct rr **rrpp,char *msg,char *cp));
#ifdef DOMAINSERVER
static char *dn_compress __ARGS((char *msg,struct dict **dp,char *cp,char *name));
static char *htonrr __ARGS((char *msg,struct dict **dp,struct rr *rr,char *buffer));
static int16 clookup __ARGS((struct dict **dp,char *name,int16 offs));
#endif

/*
 * 19Mar2021, Maiko (VE4KLM), added socket (s) parameter for better error
 * logging, added error logging for failed expansion of dns information,
 * and put in a security (malformed packet) check for qdcount. Both Jean
 * and Janusz have reported DNS related crashes of JNOS lately, both are
 * showing malformed packets directed at the JNOS DNS service.
 *
 * Thanks to Jean (VE2PKT, VA2OM) for PI time and allowing me to use his
 * system to tcpdump -XX and put in debug coding to my hearts content.
 *
 * Thanks Janusz (HF1L, ex SP1LOP) for gdb and log data, nice work guys !
 *
 * This is just the first of probably several fixes which need to be done
 * to better protect the software from malformed traffic and attacks ...
 *
 * 23March2021, Maiko, Added ptype (query or reply) and also wondering
 * if I am chasing an internal stack size or phantom error, not sure.
 *
 * AND important, we need to use peerless nos logging, so now passing
 * the socket structure as first parameter, NOT the socket number !
 */ 

static char *dnsqstr = "request", *dnsrstr = "reply";	/* for ptype index */

#ifdef	DONT_COMPILE
/* 24Apr2023, Maiko, Now defined properly in global.h */
/* 01Feb2011, Maiko (VE4KLM), Something different, new LOG function */
extern void nos_log_peerless (struct sockaddr_in *fsock, char *fmt, ...);
#endif

/* 24Sep2013, Maiko, Code easier to read without line wraps, and I need
 * to put in better protection against various fault scenarios
 */
#define NLP     nos_log_peerless

/* 24Apr2023, Maiko, Just pass a *void now to handle both IPV4 and IPV6 sockets */
int ntohdomain (/*struct sockaddr_in *fsock*/ void *fsock , int ptype, register struct dhdr *dhdr, struct mbuf **bpp)
{
    int16 tmp,len;
    register int16 i;
    char *msg,*cp;
    struct rr **rrpp;

    len = len_p(*bpp);
    msg = mallocw(len);
    pullup(bpp,msg,len);
    memset((char *)dhdr,0,sizeof(struct dhdr));

    // NLP (fsock, "ntohdomain (len %d) %x %x %x %x", len, msg[0], msg[1], msg[2], msg[3]);
 
    dhdr->id = get16(&msg[0]);
    tmp = get16(&msg[2]);
    if(tmp & 0x8000)
        dhdr->qr = 1;
    dhdr->opcode = (tmp >> 11) & 0xf;
    if(tmp & 0x0400)
        dhdr->aa = 1;
    if(tmp & 0x0200)
        dhdr->tc = 1;
    if(tmp & 0x0100)
        dhdr->rd = 1;
    if(tmp & 0x0080)
        dhdr->ra = 1;
    dhdr->rcode = tmp & 0xf;
    dhdr->qdcount = get16(&msg[4]);
    dhdr->ancount = get16(&msg[6]);
    dhdr->nscount = get16(&msg[8]);
    dhdr->arcount = get16(&msg[10]);
 
    /*
     * 19Mar2021, Maiko (VE4KLM), Time to start protecting DNS, even simple
     * things like this little snippet I caught on 'superuser.com' lately :
     *
     *  qdcount useless now - BIND has always rejected QDCOUNT != 1
     *
     * This alone will provide lots more protection against random data.
     *
     * I also noticed, nobody is checking return value of this function ...
     */
     if (dhdr->qdcount != 1)
     {
        NLP (fsock, "malformed dns %s", ptype ? dnsqstr : dnsrstr);
        return -1;    
     }

    /* Now parse the variable length sections */
    cp = &msg[12];
  
    /* Question section */
    rrpp = &dhdr->questions;
    for(i=0;i<dhdr->qdcount;i++){
        if((cp = getq(rrpp,msg,cp)) == NULLCHAR){
            free(msg);
            NLP (fsock, "unable to expand dns question");
            return -1;
        }
        (*rrpp)->source = RR_QUESTION;
        rrpp = &(*rrpp)->next;
    }
    *rrpp = NULLRR;
  
    /* Answer section */
    rrpp = &dhdr->answers;
    for(i=0;i<dhdr->ancount;i++){
        if((cp = ntohrr(rrpp,msg,cp)) == NULLCHAR){
            free(msg);
            NLP (fsock, "unable to expand dns answer");
            return -1;
        }
        (*rrpp)->source = RR_ANSWER;
        rrpp = &(*rrpp)->next;
    }
    *rrpp = NULLRR;
  
    /* Name server (authority) section */
    rrpp = &dhdr->authority;
    for(i=0;i<dhdr->nscount;i++){
        if((cp = ntohrr(rrpp,msg,cp)) == NULLCHAR){
            free(msg);
            NLP (fsock, "unable to expand dns authority");
            return -1;
        }
        (*rrpp)->source = RR_AUTHORITY;
        rrpp = &(*rrpp)->next;
    }
    *rrpp = NULLRR;
  
    /* Additional section */
    rrpp = &dhdr->additional;
    for(i=0;i<dhdr->arcount;i++){
        if((cp = ntohrr(rrpp,msg,cp)) == NULLCHAR){
            free(msg);
            NLP (fsock, "unable to expand dns additional");
            return -1;
        }
        (*rrpp)->source = RR_ADDITIONAL;
        rrpp = &(*rrpp)->next;
    }
    *rrpp = NULLRR;
    free(msg);
    return 0;
}
static char *
getq(rrpp,msg,cp)
struct rr **rrpp;
char *msg;
char *cp;
{
    register struct rr *rrp;
    int len;
    char *name;
  
    *rrpp = rrp = (struct rr *)callocw(1,sizeof(struct rr));
    name = mallocw(512);
    len = dn_expand(msg,cp,name,512);
    if(len == -1){
        free(name);
        return NULLCHAR;
    }
    cp += len;
    rrp->name = j2strdup(name);
    rrp->type = get16(cp);
    cp += 2;
    rrp->class = get16(cp);
    cp += 2;
    rrp->ttl = 0;
    rrp->rdlength = 0;
    free(name);
    return cp;
}
/* Read a resource record from a domain message into a host structure */
static char *
ntohrr(rrpp,msg,cp)
struct rr **rrpp; /* Where to allocate resource record structure */
char *msg;      /* Pointer to beginning of domain message */
char *cp;       /* Pointer to start of encoded RR record */
{
    register struct rr *rrp;
    int len;
    char *name;
  
    *rrpp = rrp = (struct rr *)callocw(1,sizeof(struct rr));
    name = mallocw(512);
    if((len = dn_expand(msg,cp,name,512)) == -1){
        free(name);
        return NULLCHAR;
    }
    cp += len;
    rrp->name = j2strdup(name);
    rrp->type = get16(cp);
    cp += 2;
    rrp->class = get16(cp);
    cp+= 2;
    rrp->ttl = get32(cp);
    cp += 4;
    rrp->rdlength = get16(cp);
    cp += 2;

    // log (-1, "rdlength %d rtype %d", rrp->rdlength, rrp->type);

    pwait (NULL);	/* in case a crash, hopefully this logs above */ 

    switch(rrp->type){
        case TYPE_A:
        /* Just read the address directly into the structure */
            rrp->rdata.addr = get32(cp);
            cp += 4;
            break;
#ifdef	IPV6
	/*
	 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
	 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
	 */
        case TYPE_AAAA:
#ifdef DONT_COMPILE
	/* 15Apr2023, Maiko, I'm being silly, just define it
	 * as an actually buffer in the structure, no malloc !
	 */
		rrp->rdata.addr6 = mallocw(16);	/* 13Apr2023, Maiko, Oops */
#endif
            copyipv6addr (cp, rrp->rdata.addr6);
	    cp += 16;
            break;
#endif
        case TYPE_CNAME:
        case TYPE_MB:
        case TYPE_MG:
        case TYPE_MR:
        case TYPE_NS:
        case TYPE_PTR:
        /* These types all consist of a single domain name;
         * convert it to ascii format
         */
            len = dn_expand(msg,cp,name,512);
            if(len == -1){
                free(name);
                return NULLCHAR;
            }
            rrp->rdata.name = j2strdup(name);
            rrp->rdlength = strlen(name);
            cp += len;
            break;
        case TYPE_HINFO:
            len = *cp++;
            rrp->rdata.hinfo.cpu = mallocw(len+1);
            memcpy( rrp->rdata.hinfo.cpu, cp, len );
            rrp->rdata.hinfo.cpu[len] = '\0';
            cp += len;
  
            len = *cp++;
            rrp->rdata.hinfo.os = mallocw(len+1);
            memcpy( rrp->rdata.hinfo.os, cp, len );
            rrp->rdata.hinfo.os[len] = '\0';
            cp += len;
            break;
        case TYPE_MX:
            rrp->rdata.mx.pref = get16(cp);
            cp += 2;
        /* Get domain name of exchanger */
            len = dn_expand(msg,cp,name,512);
            if(len == -1){
                free(name);
                return NULLCHAR;
            }
            rrp->rdata.mx.exch = j2strdup(name);
            cp += len;
            break;
        case TYPE_SOA:
        /* Get domain name of name server */
            len = dn_expand(msg,cp,name,512);
            if(len == -1){
                free(name);
                return NULLCHAR;
            }
            rrp->rdata.soa.mname = j2strdup(name);
            cp += len;
  
        /* Get domain name of responsible person */
            len = dn_expand(msg,cp,name,512);
            if(len == -1){
                free(name);
                return NULLCHAR;
            }
            rrp->rdata.soa.rname = j2strdup(name);
            cp += len;
  
            rrp->rdata.soa.serial = get32(cp);
            cp += 4;
            rrp->rdata.soa.refresh = get32(cp);
            cp += 4;
            rrp->rdata.soa.retry = get32(cp);
            cp += 4;
            rrp->rdata.soa.expire = get32(cp);
            cp += 4;
            rrp->rdata.soa.minimum = get32(cp);
            cp += 4;
            break;
        case TYPE_TXT:
        /* Just stash */
            rrp->rdata.data = mallocw(rrp->rdlength);
            memcpy(rrp->rdata.data,cp,rrp->rdlength);
            cp += rrp->rdlength;
            break;
        default:
        /* Ignore (note no data is retained, DESPITE rrp->rdlength) */
            cp += rrp->rdlength;
            break;
    }
    free(name);
    return cp;
}
  
/* Convert a compressed domain name to the human-readable form */
static int
dn_expand(msg,compressed,full,fullen)
char *msg;              /* Complete domain message */
char *compressed;       /* Pointer to compressed name */
char *full;             /* Pointer to result buffer */
int fullen;             /* Length of same */
{
    unsigned int slen;      /* Length of current segment */
    register char *cp;
    int clen = 0;   /* Total length of compressed name */
    int indirect = 0;       /* Set if indirection encountered */
    int nseg = 0;           /* Total number of segments in name */
  
    cp = compressed;
    for(;;){
        slen = uchar(*cp++);    /* Length of this segment */
        if(!indirect)
            clen++;
        if((slen & 0xc0) == 0xc0){
            if(!indirect)
                clen++;
            indirect = 1;
            /* Follow indirection */
            cp = &msg[((slen & 0x3f)<<8) + uchar(*cp)];
            slen = uchar(*cp++);
        }
        if(slen == 0)   /* zero length == all done */
            break;
        fullen -= slen + 1;
        if(fullen < 0)
            return -1;
        if(!indirect)
            clen += slen;
        while(slen-- != 0)
            *full++ = *cp++;
        *full++ = '.';
        nseg++;
    }
    if(nseg == 0){
        /* Root name; represent as single dot */
        *full++ = '.';
        fullen--;
    }
    *full++ = '\0';
    fullen--;
    return clen;    /* Length of compressed message */
}
  
  
  
#ifdef DOMAINSERVER
  
/* Most of this code is based on the DNS server in PA0GRI's 910828
 * Ported to the current NOS code by Johan. K. Reinalda, WG7J
 * for version and bug/feature info, see domain.c
 */

/* Search name dictionary, returning offset code, or inserting if not found */
static int16
clookup(dp,name,offs)
struct dict **dp;
char *name;
int16 offs;
{
    struct dict *p;

    for (p = *dp; p; p=p->next)
	{
        if (!strcmp(name,p->nm))
            return p->nmoff;
    }

	/* 22Dec2005, Maiko, Changed malloc() to mallocw() instead ! */
    if ((p = (struct dict *)mallocw(sizeof(struct dict)))!=NULLDICT)
	{
        p->next = *dp;  /* add to head of list */
        p->nm = j2strdup(name);
        p->nmoff = (offs & 0x3FFF) | 0xC000;
        *dp = p;
    }

    return 0;
}

static char *
dn_compress(msg,dp,cp,name)
char *msg;
struct dict **dp;
char *cp;
char *name;
{
    int len,dlen;
    int16 ccode;
    char *cp1;
    dlen = strlen(name);
    for(;;){
        /* Look for next dot */
        cp1 = strchr(name,'.');
        if(cp1 != NULLCHAR)
            len = (int)(cp1-name); /* More to come */
        else
            len = dlen;     /* Last component */
        *cp++ = len;            /* Write length of component */
        if(len == 0)
            return cp;
        if((ccode=clookup(dp,name,(int16)(cp-msg-1)))!=0) { /* name is in dictionary? */
            cp = put16(cp-1,ccode);  /* yes, use indirect reference */
            return cp;
        }
        /* Copy component up to (but not including) dot */
        strncpy(cp,name,len);
        cp += len;
        if(cp1 == NULLCHAR){
            *cp++ = 0;      /* Last one; write null and finish */
            return cp;
        }
        name += len+1;
        dlen -= len+1;
    }
}
  
/* Translate a resource record from host format to network format */
static char *
htonrr(msg,dp,rr,buffer)
struct dict **dp;
struct rr *rr;
char *msg,*buffer;
{
    struct rr *rrp;
    char *cp, *p;
    int i, len;
  
    cp = buffer;
    for(rrp = rr; rrp != NULLRR; rrp = rrp->next) {
        cp = dn_compress(msg,dp,cp,rrp->name);
        cp = put16(cp,rrp->type);
        cp = put16(cp,rrp->class);
        cp = put32(cp,rrp->ttl);
        p = cp;     /* This is where the length goes ! */
        cp += 2;    /* Save the space for lenght field */
  
        switch(rrp->type) {
            case TYPE_A:
                cp = put32(cp,rrp->rdata.addr);
                break;
#ifdef	IPV6
			/*
			 * 10Apr2023, Maiko (VE4KLM), Adding IPV6 support
			 *  (rfc 3596 (Oct 2003), obsoletes 3152, 1886)
			 */
            case TYPE_AAAA:
                copyipv6addr (rrp->rdata.addr6, cp);
				cp += 16;
                break;
#endif
            case TYPE_SOA:
                cp = dn_compress(msg,dp,cp,rrp->rdata.soa.mname);
                cp = dn_compress(msg,dp,cp,rrp->rdata.soa.rname);
                cp = put32(cp,rrp->rdata.soa.serial);
                cp = put32(cp,rrp->rdata.soa.refresh);
                cp = put32(cp,rrp->rdata.soa.retry);
                cp = put32(cp,rrp->rdata.soa.expire);
                cp = put32(cp,rrp->rdata.soa.minimum);
                break;
            case TYPE_HINFO:
                *cp++ = len = strlen(rrp->rdata.hinfo.cpu);
                strncpy(cp,rrp->rdata.hinfo.cpu,len);
                cp += len;
                *cp++ = len = strlen(rrp->rdata.hinfo.os);
                strncpy(cp,rrp->rdata.hinfo.os,len);
                cp += len;
                break;
            case TYPE_MX:
                cp = put16(cp,rrp->rdata.mx.pref);
                cp = dn_compress(msg,dp,cp,rrp->rdata.mx.exch);
                break;
            case TYPE_CNAME:
            case TYPE_MB:
            case TYPE_MG:
            case TYPE_MR:
            case TYPE_NS:
            case TYPE_PTR:
                cp = dn_compress(msg,dp,cp,rrp->rdata.data);
                break;
            case TYPE_TXT:
                cp = put16(cp,rrp->rdlength);
                for(i=0 ; i < rrp->rdlength ; i++)
                    *cp++ = rrp->rdata.data[i];
                break;
#if 0
            case TYPE_MINFO:    /* Unsupported type */
                cp = dn_compress(msg,dp,cp,rrp->rdata.minfo.rmailbx);
                cp = dn_compress(msg,dp,cp,rrp->rdata.minfo.emailbx);
            case TYPE_MD:       /* Unsupported type */
            case TYPE_MF:       /* Unsupported type */
            case TYPE_NULL:     /* Unsupported type */
            case TYPE_WKS:      /* Unsupported type */
                cp = dn_compress(msg,dp,cp,rrp->rdata.data);
                break;
#else
/* Fall into default handler, ie, store nothing in the data portion
   for these unsupported types.
   Note what we do here MUST agree with what ntohrr() does, or we'll
   end up dn_compress'ing NULLPTR->data!
*/
#endif
/*          default:  recall no data was stored in ntohrr() */
        }
    /* Calculate the lenght of the RR */
        len = (int) (cp - p - 2);
        put16(p,len);       /* and set it */
    }
    return cp;
}
  
int
htondomain(dhdr,buffer,buflen)
struct dhdr *dhdr;
char *buffer;   /* Area for query */
int16 buflen;   /* Length of same */
{
    char *cp;
    struct rr *rrp;
    struct dict *dp = NULLDICT;
    struct dict *p,*pn;
    int16 parameter;
    int i, count;
  
    cp = buffer;
    cp = put16(cp,dhdr->id);
    if(dhdr->qr)
        parameter = 0x8000;
    else
        parameter = 0;
    parameter |= (dhdr->opcode & 0x0f) << 11;
    if(dhdr->aa)
        parameter |= DOM_AUTHORITY;
    if(dhdr->tc)
        parameter |= DOM_TRUNC;
    if(dhdr->rd)
        parameter |= DOM_DORECURSE;
    if(dhdr->ra)
        parameter |= DOM_CANRECURSE;
    parameter |= (dhdr->rcode & 0x0f);
    cp = put16(cp,parameter);
    cp = put16(cp,dhdr->qdcount);
    cp = put16(cp,dhdr->ancount);
    cp = put16(cp,dhdr->nscount);
    cp = put16(cp,dhdr->arcount);
    if((count = dhdr->qdcount) > 0) {
        rrp = dhdr->questions;
        for(i = 0; i < count; i++) {
            cp = dn_compress(buffer,&dp,cp,rrp->name);
            cp = put16(cp,rrp->type);
            cp = put16(cp,rrp->class);
            rrp = rrp->next;
        }
    }

    if(dhdr->ancount > 0)
        cp = htonrr(buffer,&dp,dhdr->answers,cp);

    if(dhdr->nscount > 0)
        cp = htonrr(buffer,&dp,dhdr->authority,cp);

    if(dhdr->arcount > 0)
        cp = htonrr(buffer,&dp,dhdr->additional,cp);

    for(p=dp; p; p=pn) {
#ifdef DICTTRACE
        log(-1,"dict: %4x %s", p->nmoff, p->nm);
#endif
        pn=p->next;
        free(p->nm);
        free(p);
    }

    return (int)(cp - buffer);
}
  
  
#endif /* DOMAINSERVER */
  
