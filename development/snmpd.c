/*
 * JNOS 2.0
 *
 * $Id: snmpd.c,v 1.1 2015/04/22 01:51:45 root Exp root $
 *
 * 17Dec2010, Maiko Langelaar, VE4KLM - took very little time to write this
 * original code, which is what makes JNOS so cool as a development platform,
 * it's so easy to add new TCP or UDP services to a JNOS system - very nice.
 *
 * Basic SNMP server so I can get MRTG to poll JNOS itself for iface data,
 * as of 20Dec2010, number of packets is all that JNOS collects in the form
 * of iface statistics - I expect to add RX and TX Bytes to 'struct iface'
 * at some point in the near future - so for now, packets/sec is all we
 * can do ...
 *
 * 31Jan2011, Maiko, information I found that might be useful - To know the
 * interface speed, MRTG poll the mib ifSpeed. The default interface speed
 * is equal to ifSpeed, but if you've modified the speed with the bandwidth
 * command, the ifSpeed is also modified.
 *
 * 01Feb2011, Maiko, reorganizing the code, make it more logical, at some
 * point I should use a MIB lookup table approach, but with the number of
 * MIBs that I am dealing with right now it's not worth the effort (yet).
 *
 * Make the code so that it *survives* an snmpwalk - service was exiting,
 * would help to use 'continue' instead of 'break', also warn if not v1,
 * or if it's anything other than a GetRequest PDU - make more robust.
 *
 * snmpwalk -v1 -c jnos 192.168.1.201
 *
 * snmpget -v1 -c jnos 192.168.1.201 1.3.6.1.2.1.2.2.1.16.9
 *
 * Lastly, a NEW nos_log_peerless() function for UDP based clients ...
 * 
 * 29Apr2022, Maiko, the snmpwalk will never be perfect, also added
 * support for the NoSuchName (Object) PDU response, should have put
 * this in years ago, just never thought to till now, still not the
 * perfect way to do it, but seems 'better' at least ?
 */

#include "global.h"

#include <ctype.h>

#ifdef	SNMPD

#include "usock.h"

/*
 * IF-MIB::ifInOctets.N  -> 6.1.2.1.2.2.1.10 .N
 * IF-MIB::ifOutOctets.N -> 6.1.2.1.2.2.1.16 .N
 *
 * SNMPv2-MIB::sysUpTime.0 -> 6.1.2.1.1.3.0
 * SNMPv2-MIB::sysName.0 -> 6.1.2.1.1.5.0
 *
 * 22Apr2022, Maiko (VE4KLM), New mibs for Mark Phillips :]
 *
 * SNMPv2-MIB::sysLocation.0 -> 6.1.2.1.1.6.0
 * SNMPv2-MIB::sysContact.0 -> 6.1.2.1.1.4.0
 */

static char ifInOctets[9] = { 43, 6, 1, 2, 1, 2, 2, 1, 10 };
static char ifOutOctets[9] = { 43, 6, 1, 2, 1, 2, 2, 1, 16 };

static char sysUpTime[8] = { 43, 6, 1, 2, 1, 1, 3, 0 };
static char sysName[8] = { 43, 6, 1, 2, 1, 1, 5, 0 };

/* 22Apr2022, Maiko (VE4KLM), New mibs for Mark Phillips :] */

static char sysLocation[8] = { 43, 6, 1, 2, 1, 1, 6, 0 };
static char sysContact[8] = { 43, 6, 1, 2, 1, 1, 4, 0 };

/*
 * IF-MIB::ifName    -> 6.1.2.1.31.1.1.1.1 .N (optional)
 *
 * 29Apr2014, new
 *
 * IF-MIB::ifName.Counter -> 6.1.2.1.31.1.1.1.2.1
 *
 * 30Apr2014, new, used to terminate ifName walkthrough, although I suspect it
 * does not matter what MIB value is used here if the value type set to 0x41,
 * although (wait) after some reading of net-snmp.org FAQ, if MRTG keeps asking
 * for the next and the next and so on, JNOS should just give a next available,
 * in the case of linux, if that's the end of all ifNames, linux will return a
 * counter32 for the last interface (which is what you see above), but it's up
 * to the querying tool (MRTG) to recognise the last result lies outside the
 * area of interest (quoting the FAQ), so the value type is also irrelevant. I
 * think I have got this figured out, maybe (future project) have JNOS load
 * uptodate MIB definitions (well, a limited set anyways) ? We'll see ...
 *
 * Adding more MIBs as time goes by ...
 *
 */
static char ifName[10] = { 43, 6, 1, 2, 1, 31, 1, 1, 1, 1 };

static char ifNameCounter[11] = { 43, 6, 1, 2, 1, 31, 1, 1, 1, 2, 1 };

static char ifDesc[9] = { 43, 6, 1, 2, 1, 2, 2, 1, 2 };		/* 30Apr2014 */

static char ifDescCounter[10] = { 43, 6, 1, 2, 1, 2, 2, 1, 3, 1 };

static char ifTypeCounter[10] = { 43, 6, 1, 2, 1, 2, 2, 1, 4, 1 };

static int Snmpd = -1;

static int snmpd_debug = 0;	/* 04Jan2011, Maiko, debug flag for logging */

enum mibtype { UpTime, Name };	/* 01Feb2011, Maiko, Haven't used enums for ages */

#define	J2_SNMPD_DEBUG

#ifdef	DONT_COMPILE
/* 24Apr2023, Maiko, now defined in global.h where it should be defined */
/* 01Feb2011, Maiko (VE4KLM), Something different, new LOG function */
extern void nos_log_peerless (struct sockaddr_in *fsock, char *fmt, ...);
#endif

/* 24Sep2013, Maiko, Code easier to read without line wraps, and I need
 * to put in better protection against various fault scenarios
 */
#define NLP	nos_log_peerless

/* 17Apr2016, Maiko (VE4KLM), New ACL structure (just playing right now) */

typedef struct lcapmns2j {
	char *rocommunity;
	char *ippattern;
	struct lcapmns2j *nxt;
} J2SNMPACL;

static J2SNMPACL *j2snmpacl = (J2SNMPACL*)0;

/* 23Apr2022, Maiko (VE4KLM), Now supporting sysContact and sysLocation */

static char *j2sysLocation = (char*)0;
static char *j2sysContact = (char*)0;

static int ourcommunity (int len, char *community, struct sockaddr_in *fsock)
{
	J2SNMPACL *ptr;

	int retval = 0;

	/* form a string of the community */
	char *incoming_community = malloc (len + 1);
	memcpy (incoming_community, community, len);
	*(incoming_community + len) = 0;

	/* get a string representing the calling ip address */
	char *incoming_ipaddr = inet_ntoa (fsock->sin_addr.s_addr);

	for (ptr = j2snmpacl; ptr ; ptr = ptr->nxt)
	{
		if (!strcmp (ptr->rocommunity, incoming_community))
		{
			if ((ptr->ippattern == (char*)0) || (!strncmp (ptr->ippattern,
					incoming_ipaddr, strlen (ptr->ippattern))))
			{
				retval = 1;
				break;
			}
		}
	}

	if (!retval) NLP (fsock, "[%s] not allowed", incoming_community);

	free (incoming_community);

	return retval;
}

#ifdef	J2_SNMPD_DEBUG

static void snmpd_dump (struct sockaddr_in *fsock, int len, /* unsigned */ char *ptr)
{
	static char dumpbuffer[500];

	char *sptr = dumpbuffer;

	int cnt = 0;

	if (!len) return;

	while (cnt < len)
	{
		/*
		 * 17Apr2016, Maiko (VE4KLM), need to mask the value
		 * read from a signed char pointer before assigning
		 * to an integer or to a sprintf argument
		 */
		if (!isprint (*ptr))
			sptr += sprintf (sptr, "[%02x]", *ptr & 0xff);
		else
			*sptr++ = *ptr;

		cnt++;
		ptr++;
	}

	*sptr = 0;

	nos_log_peerless (fsock, "DUMP (%d) >>>%s<<<", len, dumpbuffer);
}

#endif

/* 01Feb2011, Maiko, Help me better identify what's coming in */
static void log_ber_type6 (struct sockaddr_in *fsock, int blen, char *bptr, char *desc)
{
	char btmp[80], *ptr = btmp, *bptr2 = bptr;
	int bcnt;

	btmp[0] = 0;

	for (bcnt = 0; bcnt < blen; bcnt++, bptr2++)
		ptr += sprintf (ptr, " %d", (int)(*bptr2));

	nos_log_peerless (fsock, "OID [%s ] %s", btmp, desc);
}

/* 28Apr2014, Maiko (VE4KLM), returns response frame to ifName PDU requests */
static char *ifNameResp (char *odptr, char *ber_ptr, int iface_num, int *vblen)
{
	int len, iface_cnt = 1;	/* starts with 1, not 0 */

	struct iface *ifp;

   	for (ifp = Ifaces; ifp != NULLIF; iface_cnt++, ifp = ifp->next)
		if (iface_num == iface_cnt)
			break;

	if (ifp)
		len = strlen (ifp->name);
	else
		len = 1;	/* used to update big varbind */

	/* varbind type */
	*odptr++ = 0x30;

	if (ifp)
	{
		/* varbind len */
		*odptr++ = 0x0f + (unsigned char)len;

		/* oid */
		*odptr++ = 0x06;
		*odptr++ = 0x0b;

		memcpy (odptr, ber_ptr, 0x0a);
		odptr += 0x0a;
		*odptr++ = (unsigned char)iface_num;

		/* Value */
		*odptr++ = 0x04;
		*odptr++ = (unsigned char)len;

		odptr += sprintf (odptr, "%s", ifp->name);
	}
	else	/* 30Apr2014, Maiko, Got ifName walk through completed */
	{
		/* varbind len */
		*odptr++ = 0x10;

		/* oid */
		*odptr++ = 0x06;
		*odptr++ = 0x0b;

		memcpy (odptr, ifNameCounter, 0x0b);	/* ifDesc is next */
		odptr += 0x0b;

		/* Value */
		*odptr++ = 0x41;
		*odptr++ = 0x01;
		*odptr++ = 0x00;
	}

	*vblen += 0x11 + len;

	return odptr;
}

/* 30Apr2014, Function to return description names */
static char *ifDescResp (char *odptr, char *ber_ptr, int iface_num, int *vblen)
{
	int len, iface_cnt = 1;	/* starts with 1, not 0 */

	struct iface *ifp;

   	for (ifp = Ifaces; ifp != NULLIF; iface_cnt++, ifp = ifp->next)
		if (iface_num == iface_cnt)
			break;

	if (ifp)
	{
		/* 04May2014, Maiko, Make sure descr is NOT null !!! */
		if (ifp->descr)
			len = strlen (ifp->descr);
		else
			len = 0;
	}
	else len = 1;	/* used to update big varbind */

	/* varbind type */
	*odptr++ = 0x30;

	if (ifp)
	{
		/* varbind len */
		*odptr++ = 0x0e + (unsigned char)len;

		/* oid */
		*odptr++ = 0x06;
		*odptr++ = 0x0a;

		memcpy (odptr, ber_ptr, 0x09);
		odptr += 0x09;
		*odptr++ = (unsigned char)iface_num;

		/* Value */
		*odptr++ = 0x04;
		*odptr++ = (unsigned char)len;

		/* 04May2014, Maiko, Make sure descr is NOT null !!! */
		if (ifp->descr)
			odptr += sprintf (odptr, "%s", ifp->descr);
	}
	else	/* 30Apr2014, Maiko, Got ifDesc walk through completed */
	{
		/* varbind len */
		*odptr++ = 0x0f;

		/* oid */
		*odptr++ = 0x06;
		*odptr++ = 0x0a;

		memcpy (odptr, ifDescCounter, 0x0a);	/* ifType is next */
		odptr += 0x0a;

		/* Value */
		*odptr++ = 0x41;
		*odptr++ = 0x01;
		*odptr++ = 0x00;
	}

	*vblen += 0x10 + len;

	return odptr;
}

/* 04May2014, Maiko, InOctets and OutOctets now in a function, note extra arg */
static char *ifOctetsResp (char *odptr, char *ber_ptr, int iface_num, int *vblen, int incoming)
{
	int iface_cnt = 1;	/* starts with 1, not 0 */

	struct iface *ifp;

	char htmp[9];

	extern char hextochar (char*);

   	for (ifp = Ifaces; ifp != NULLIF; iface_cnt++, ifp = ifp->next)
		if (iface_num == iface_cnt)
			break;

	/* varbind type */
	*odptr++ = 0x30;

	if (ifp)
	{
		/* Varbind len */
		*odptr++ = 0x12;

		/* OID */
		*odptr++ = 0x06;
		*odptr++ = 0x0a;

		memcpy (odptr, ber_ptr, 0x09);
		odptr += 0x09;
		*odptr++ = (unsigned char)iface_num;

		/* Value */
		*odptr++ = 0x41;
		*odptr++ = 0x04;

		if (ifp)
		{
			if (incoming)
				sprintf (htmp, "%08x", ifp->rawrecbytes);
			else
				sprintf (htmp, "%08x", ifp->rawsndbytes);
		}
		else
			sprintf (htmp, "%08x", 0);

		*odptr++ = hextochar (&htmp[0]);
		*odptr++ = hextochar (&htmp[2]);
		*odptr++ = hextochar (&htmp[4]);
		*odptr++ = hextochar (&htmp[6]);

		*vblen += 0x14;
	}
	else
	{
		/* varbind len */
		*odptr++ = 0x10;

		/* oid */
		*odptr++ = 0x06;
		*odptr++ = 0x0a;

		memcpy (odptr, ifTypeCounter, 0x0a);	/* MTU is next */
		odptr += 0x0a;

		/* Value */
		*odptr++ = 0x02;
		*odptr++ = 0x02;

		*odptr++ = 0x40;	/* mtu values ??? */
		*odptr++ = 0x34;

		*vblen += 0x12;
	}

	return odptr;
}

#ifdef	DONT_COMPILE

	/* 29Apr2022, Maiko, Replaced by new systemResp function */

/*
 * 04May2014, I can't possibly write response functions for every request
 * that comes in, and I don't believe MRTG needs all of this info, I am a
 * bit surprised how much of a walk it does for this. Let's see if using
 * this function (a way of saying - sorry can't give you the info) will
 * make still allow MRTG to work. My problem with the ipAdEntIfIndex is
 * that it looks like it uses Ip address as a key, and most of my ifaces
 * have no IP so how is that supposed to work ? Try this - X fingers !
 */
static char *sysNameResp (char *odptr, char *ber_ptr, int *vblen)
{
	extern char *Hostname;

	int hlen = strlen (Hostname);

	/* Varbind type and len */
	*odptr++ = 0x30;
	*odptr++ = (unsigned char)(hlen + 12);

	/* OID */
	*odptr++ = 0x06;
	*odptr++ = 0x08;
	memcpy (odptr, ber_ptr, 0x08);
	odptr += 0x08;

	/* Value */
	*odptr++ = 0x04;
	*odptr++ = (unsigned char)strlen (Hostname);
	odptr += sprintf (odptr, "%s", Hostname);

	*vblen += hlen;

	*vblen += 14;

	return odptr;
}

#endif	/* DONT_COMPILE */

/*
 * 22Apr2022, Maiko (VE4KLM), New function to support mibs for Mark Phillips :]
 *
 * When I have this working properly, replace sysNameResp with this new one,
 * since this 'new' function is in fact the original sysNameResp () above, but
 * I have modified it so I can pass a string to it, instead of hardcoding the
 * hostname inside of it - saves major code duplication, which is important.
 */

static char *systemResp (char *odptr, char *ber_ptr, int *vblen, char *infostr)
{
	int hlen = strlen (infostr);

	/* Varbind type and len */
	*odptr++ = 0x30;
	*odptr++ = (unsigned char)(hlen + 12);

	/* OID */
	*odptr++ = 0x06;
	*odptr++ = 0x08;
	memcpy (odptr, ber_ptr, 0x08);
	odptr += 0x08;

	/* Value */
	*odptr++ = 0x04;
	*odptr++ = (unsigned char)strlen (infostr);
	odptr += sprintf (odptr, "%s", infostr);

	*vblen += hlen;

	*vblen += 14;

	return odptr;
}

int snmpd1 (int argc, char **argv, void *p)
{
    struct sockaddr_in lsocket,fsock;
    int cnt, len, ber_type, ber_len, vblen, pdlen, olen, pdu_type;
    char data[200], *ber_ptr, *vblenptr, *pdulenptr, *olenptr;
	char idata[256], *idptr;
	char odata[256], *odptr, *odptr_save;
    struct mbuf *bp;

	/* 29Apr2022, Maiko, for 'error response' NoSuchObject */
    char edata[256];
	char *eptr;
	int elen;

    j2psignal (Curproc, 0);
    chname (Curproc,"snmpd");
    lsocket.sin_family = AF_INET;
    lsocket.sin_addr.s_addr = INADDR_ANY;
    lsocket.sin_port = 161;

    if ((Snmpd = j2socket(AF_INET,SOCK_DGRAM,0)) == -1)
    {
        j2tputs(Nosock);
        return -1;
    }

    j2bind(Snmpd,(char *)&lsocket,sizeof(lsocket));

	while (1)
	{
        len = sizeof(fsock);

        if (recv_mbuf(Snmpd,&bp,0,(char *)&fsock,&len) == -1)
		{
			NLP (&fsock, "recv_mbuf failed");
            break;
		}

		/* make darn sure these pointers are reset here !!! */
		idptr = idata; odptr = odata;

		if ((*odptr = PULLCHAR (&bp)) != 0x30)
		{
			/* 23Sep2013, Maiko, This is not a reason to terminate snmpd ! */	
			NLP (&fsock, "expecting sequence data type, got %d", (int)*odptr);
			continue;
		}

		odptr++;
		olenptr = odptr;	/* update this after response is finished */
		odptr++;

 		if ((len = PULLCHAR (&bp)) == -1)
		{
			NLP (&fsock, "no sequence length");
			continue;
		}

		if (pullup (&bp, idata, len) != len)
		{
			NLP (&fsock, "expecting %d bytes, sequence cut short", len);
			continue;
		}

#ifdef	J2_SNMPD_DEBUG
		if (snmpd_debug)
			snmpd_dump (&fsock, len, idata);
#endif
		/* SNMP Version */

		if ((*odptr++ = *idptr++) != 0x02)
			continue;
		if ((*odptr++ = *idptr++) != 0x01)
			continue;
		if ((*odptr++ = *idptr++) != 0x00)
		{
			nos_log_peerless (&fsock, "only SNMPv1 is supported");
			continue;
		}

		/* SNMP Community String */

		if ((*odptr++ = *idptr++) != 0x04)
			continue;

		*odptr++ = len = *idptr++;

		if (snmpd_debug)
			nos_log_peerless (&fsock, "community [%.*s]", len, idptr);

		/* 17Apr2016, Maiko (VE4KLM), need to start enforcing our community */
		if (!ourcommunity (len, idptr, &fsock))
		{
			continue;
		}

		memcpy (odptr, idptr, len);
		odptr += len;
		idptr += len;

		olen = 5 + len;

		/* SNMP PDU - Type and Length */
	/*
	 * 17Apr2016, Maiko (VE4KLM), for sake of getting rid of compiler
	 * errors, this char vs unsigned char can sneak up on you, so now
	 * you have to do silly things like mask signed char values that
	 * might contain an 8th bit, with the 0xff mask before giving to
	 * an int variable. Welcome to the world of C programming :]
	 */
		pdu_type = *idptr++ & 0xff;

		if ((pdu_type != 0xa0) && (pdu_type != 0xa1))
		{
			nos_log_peerless (&fsock,
				"only GetRequest and GetNextRequest are supported");

			continue;
		}

		*odptr++ = 0xa2;

		pdulenptr = odptr;	/* update this after pdu is finished */
		odptr++;

 		idptr++;	/* skip over PDU length, I don't really use it */

		/* 29Apr2022, Maiko, Initialize error buffer with everything received so far */
		elen = (int)(odptr - odata);	
		memcpy (edata, odata, elen);
		eptr = edata + elen;

		/* Request ID, Error, and Error Index */

		for (pdlen = 0, cnt = 0; cnt < 3; cnt++)
		{
			if ((*eptr++ = *odptr++ = *idptr++) != 0x02)	/* 29Mark2022, include error buffer */
			{
				log (-1, "expecting 0x02 ...");
            	break;
			}

			*eptr++ = *odptr++ = len = *idptr++;	/* 29Apr2022, Maiko, include error buffer */

			memcpy (odptr, idptr, len);
		/*
		 * 29Apr2022, Maiko, Set up error buffer if we need it
		 */
			memcpy (eptr, idptr, len);
			if (cnt == 1)
				*eptr = 0x02;
			else if (cnt == 2)
				*eptr = 0x01;
			eptr += len;

			odptr += len;
			idptr += len;

			pdlen += len;
			pdlen += 2;
		}

		/* Varbind List - Type and Length */

		if ((*eptr++ = *odptr++ = *idptr++) != 0x30)	/* 29Apr2022, Maiko, include error buf */
			continue;

		vblenptr = odptr;	/* update this after all varbind's are added */
		odptr++;
 		len = *idptr++;

		*eptr++ = len;	/* 29Apr2022, Maiko */

		memcpy (data, idptr, len);
		memcpy (eptr, idptr, len);
		idptr += len;
		eptr +=len;

		/*
		 * 29Apr2022, Maiko (VE4KLM), based on traces, that's all we need
		 * to do as far as setting up error buffer for NoSuchObject/Name,
		 * so if we can't support an incoming OID ? send edata response.
		 */

		/* Parse the varbind list */

		ber_ptr = data;

		vblen = 0;

		while (len > 0)
		{
			ber_type = *ber_ptr++;
			len--;

			ber_len = *ber_ptr++;
			len--;

			if (snmpd_debug)
				nos_log_peerless (&fsock, "type %d len %d", ber_type, ber_len);

			if (ber_type == 0x30)
				continue;

			/*
			 * 05May2014, Maiko, If we get a MIB we do not support, then we
			 * need to send 'something' back to the SNMP client to let it know
			 * that we can't give it what it wants. Use odptr to determine if
			 * we were able to service a request or not.
			 */
			odptr_save = odptr;

			/*
			 * 01Feb2011, Maiko, Start to better organize this
			 * should actually have a lookup table
			 */
			if (ber_type == 6)
			{
				/* 01Feb2011, Maiko, Help me better identify what's coming in */
				if (snmpd_debug)
					log_ber_type6 (&fsock, ber_len, ber_ptr, "");

				/* 28Apr2014 - walking the ifaces, the next and the next */
				if (ber_len == 11)
				{
					/* the 11th byte for ifName will be next iface to lookup */
					if (!memcmp (ifName, ber_ptr, 10))
					{
						/* last byte specifies the interface number */
						int iface_num = *(ber_ptr + 10);

						/* 04May2014, Maiko, increment iface_num if GetNextReq */
						if (pdu_type == 0xa1)
							iface_num++;

						odptr = ifNameResp (odptr, ber_ptr, iface_num, &vblen);
					}
				}
				else if (ber_len == 10)
				{
					/* 30Apr2014 - the 10th byte will be next iface to lookup */
					if (!memcmp (ifDesc, ber_ptr, 9))
					{
						/* last byte specifies the interface number */
						int iface_num = *(ber_ptr + 9);

						/* 04May2014, Maiko, increment iface_num if GetNextReq */
						if (pdu_type == 0xa1)
							iface_num++;

						odptr = ifDescResp (odptr, ber_ptr, iface_num, &vblen);
					}
					/* 27Apr2014 - walking the ifaces, give them the 1rst one */
					else if (!memcmp (ifName, ber_ptr, 10))
					{
						odptr = ifNameResp (odptr, ber_ptr, 1, &vblen);
					}
					else if (!memcmp (ifInOctets, ber_ptr, 9))
					{
						/* last byte specifies the interface number */
						int iface_num = *(ber_ptr + 9);

						/* 04May2014, Maiko, increment iface_num if GetNextReq */
						if (pdu_type == 0xa1)
							iface_num++;

						odptr = ifOctetsResp (odptr, ber_ptr, iface_num, &vblen, 1);
					}
					else if (!memcmp (ifOutOctets, ber_ptr, 9))
					{
						/* last byte specifies the interface number */
						int iface_num = *(ber_ptr + 9);

						/* 04May2014, Maiko, increment iface_num if GetNextReq */
						if (pdu_type == 0xa1)
							iface_num++;

						odptr = ifOctetsResp (odptr, ber_ptr, iface_num, &vblen, 0);
					}
				}
				else if (ber_len == 9)	/* 30Apr2014 */
				{
					/* 04May2014 - Octets now in their own function */
					if (!memcmp (ifInOctets, ber_ptr, 9))
						odptr = ifOctetsResp (odptr, ber_ptr, 1, &vblen, 1);

					/* 04May2014 - Octets now in their own function */
					else if (!memcmp (ifOutOctets, ber_ptr, 9))
						odptr = ifOctetsResp (odptr, ber_ptr, 1, &vblen, 0);

					/* 30Apr2014 - walking descriptions, give them 1rst one */
					else if (!memcmp (ifDesc, ber_ptr, 9))
						odptr = ifDescResp (odptr, ber_ptr, 1, &vblen);
				}
				else if (ber_len == 8)
				{
					/* TimeTicks */
					if (!memcmp (sysUpTime, ber_ptr, 8))
					{
						if (snmpd_debug)
							nos_log_peerless (&fsock, "sysUpTime");

						/* Varbind type and len */
						*odptr++ = 0x30;
						*odptr++ = 0x10;

						/* OID */
						*odptr++ = 0x06;
						*odptr++ = 0x08;
						memcpy (odptr, ber_ptr, 0x08);
						odptr += 0x08;

						/* Value */
						*odptr++ = 0x43;
						*odptr++ = 0x04;

					/* 20Dec2010, Maiko (VE4KLM), Uptime is zero for now */
						*odptr++ = 0x00;
						*odptr++ = 0x00;
						*odptr++ = 0x00;
						*odptr++ = 0x00;

						vblen += 18;
					}
					/* Octet String */
					else if (!memcmp (sysName, ber_ptr, 8))
					{
						/*
						 * 29Apr2022, Maiko, replaced by new systemResp
						 * odptr = sysNameResp (odptr, ber_ptr, &vblen);
						 */

						extern char *Hostname;

						odptr = systemResp (odptr, ber_ptr, &vblen, Hostname ? Hostname : "not defined");
					}
				/*
				 * 22Apr2022, Maiko (VE4KLM), New mibs for Mark Phillips :]
				 *
				 * The values are obviously hardcoded for testing, but I am
				 * tired from my aikido class. I'll write the code, so sysop
				 * can set these from command line, but some other time !
				 *
				 * 23Apr2022, Maiko, Added globals j2sysContact and j2sysLocation,
				 * so these can now be set with the JNOS console command 'snmp'.
				 */
					/* Octet String */
					else if (!memcmp (sysLocation, ber_ptr, 8))
						odptr = systemResp (odptr, ber_ptr, &vblen, j2sysLocation ? j2sysLocation : "not defined");

					/* Octet String */
					else if (!memcmp (sysContact, ber_ptr, 8))
						odptr = systemResp (odptr, ber_ptr, &vblen, j2sysContact ? j2sysContact : "not defined");

				}

		/* 05May2014, Maiko, No support function, force client off track */

				if (odptr == odptr_save)
				{
					log_ber_type6 (&fsock, ber_len, ber_ptr, "N/A");

					/* 29Apr2022, Maiko (VE4KLM), send preconfigured error buffer */
					*olenptr = (int)(eptr - edata);
					memcpy (odata, edata, *olenptr);
					*olenptr -= 2;	/* j2send does this, that's why */
					vblen = 0;	/* not used for edata */

					break;
				}
			}

			ber_ptr += ber_len;

			len -= ber_len;
		}

		*vblenptr = vblen;	/* make sure to update varbind list length */

		*pdulenptr = vblen + 2 + pdlen;	/* update pdu length */

		*olenptr = *pdulenptr + olen + 2; /* update entire message length */

#ifdef	J2_SNMPD_DEBUG
		if (snmpd_debug)
			snmpd_dump (&fsock, *olenptr + 2, odata);
#endif
		/* Okay, now send the SNMP data back out to whoever asked for it */
       	len = sizeof(fsock);
		j2sendto (Snmpd, odata, *olenptr + 2, 0, (char*)&fsock, len);
	}

	log (-1, "snmpd terminating !!!");

	free_p(bp);
	close_s(Snmpd);
	Snmpd = -1;

    return 0;
}

/* 17Apr2016, Maiko (VE4KLM), Adding some new subcommands, general usage */

static void usageSNMPcfg ()
{
	extern int getusage (char*, char*); /* 28May2020, Maiko (VE4KLM), new usage function */

	/*
	 * 23Apr2022, Maiko (VE4KLM), now using the usage file snmp.txt,
	 * located in /jnos/usage directory, you can customize the file.
	 *
        tprintf ("usage: snmp ");
        tprintf (" [ ifaces | ro community [ipaddr pattern]*\n");
        tprintf (" [ sysContact [text] | sysLocation [text]\n");
	 */

	getusage ("", "snmp");
}

/*
 * 23Feb2012, Maiko (VE4KLM), New function so I can query config info.
 * 17Apr2016, Maiko (VE4KLM), added very basic community and ip acl.
 */
int dosnmp (int argc, char **argv, void *p)
{
	struct iface *ifp;

	int iface_cnt = 0;

	J2SNMPACL *ptr;

	/*
	 * 17Apr2016, Maiko (VE4KLM), Really need to have access control, so
	 * it's time to put in new commands to set the community and do some
	 * type of ip address ACL, nothing fancy at this time ...
	 */
	if (argc > 1)
	{
		/* 
		 * 23Apr2022, Maiko (VE4KLM), added sysContact and sysLocation
		 *  (requested by Mark Phillips, NI2O/G7LTT/ex-KC2ENI)
		 *
		 * Make the code more efficient later on, right now it's just
		 * a copy and paste of basically the same code, for starters.
		 */
		if (!stricmp (argv[1], "sysContact"))
		{
			if (argc == 2)
			{
				if (j2sysContact)
					tprintf ("%s\n", j2sysContact);
				else
					tprintf ("not defined\n");
			}
			else 
			{
				if (j2sysContact)
					free (j2sysContact);

				j2sysContact = j2strdup (argv[2]);
			}
		}
		else if (!stricmp (argv[1], "sysLocation"))
		{
			if (argc == 2)
			{
				if (j2sysLocation)
					tprintf ("%s\n", j2sysLocation);
				else
					tprintf ("not defined\n");
			}
			else 
			{
				if (j2sysLocation)
					free (j2sysLocation);

				j2sysLocation = j2strdup (argv[2]);
			}
		}
		else

		if (!stricmp (argv[1], "ro"))
		{
			if (argc == 3 || argc == 4)
			{
					ptr = malloc (sizeof(J2SNMPACL));

					ptr->rocommunity = strdup (argv[2]);

					if (argc == 4)
						ptr->ippattern = strdup (argv[3]);
					else
						ptr->ippattern = (char*)0;

					ptr->nxt = j2snmpacl;

					j2snmpacl = ptr;
			}
			else
			{
				for (ptr = j2snmpacl; ptr ; ptr = ptr->nxt)
				{
					tprintf ("%s %s\n", ptr->rocommunity,
						ptr->ippattern ? ptr->ippattern : "anyone");
				}
			}
			return 0;
		}
		else if (!stricmp (argv[1], "ifaces"))
		{
			tprintf ("iface #  name\n");
    
			for (ifp = Ifaces; ifp != NULLIF; iface_cnt++, ifp = ifp->next)
				tprintf ("      %2d %s\n", iface_cnt, ifp->name);

			tprintf ("\n");

		} else usageSNMPcfg ();

	} else usageSNMPcfg ();

	return (0);
}

#endif	/* end of SNMPD */

