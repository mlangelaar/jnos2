
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 */

#include <ctype.h>

#include "global.h"

#ifdef	APRSD

#include "mbuf.h"

/* 01Feb2002, Maiko, DOS compile for JNOS will complain without these */
#ifdef	MSDOS
#include "iface.h"
#include "arp.h"
#include "slip.h"
#endif

#include "ax25.h"
#include "udp.h"

#include "aprs.h"	/* 16May2001, VE4KLM, Added prototypes */

static char data_APZxxx[300];	/* make sure we have 'lots' of room */

extern int32 internal_hostid;	/* the only kludge so far */

extern int aprs_poe;	/* 05Sep2001, Maiko, Flag Point of Entry call */

/*
 * 30Aug2001, Maiko, New function to format the POE (point of entry) callsign,
 * so that packets sent to the internet APRS system will now be traceable to
 * the gateway that received the packets and injected them into the system.
 *
 * 05Sep2001, Maiko, Even though not everyone is in agreement, those that I
 * see as the major players seem to agree that '-0n' is at least backwards
 * compatible and will do no harm to the existing system. I will now make
 * this an option (ie, sysops can switch off if needed).
 *
 * 25Oct2002, Maiko, Switched to using 'IGATE,I' as the POE format, as it
 * seems to have become the 'standard' of choice in the past year or so.
 *
 * 18May2004, Maiko, No longer static function, required outside of this
 * module for the experimental TIP server stuff for Kantronics TNCs.
 */

char *format_POE_call (char *ptr)
{
#ifdef	USE_OLD_WAY
	char *lptr = logon_callsign;

	int ssid = 0;

	*ptr++ = ',';

	while (*lptr && *lptr != '-')
		*ptr++ = *lptr++;

	if (*lptr++ == '-')
		ssid = atoi (lptr) % 10;

	/* 30Aug2001, Maiko, Stick with generic (-Gn) gateway for now */
	/* 05Sep2001, Maiko, Switched to using (-0n), leading 0 in SSID */
	ptr += sprintf (ptr, "-0%d", ssid);
#else
	/*
	 * 25Oct2002, Maiko, The 'standard' for the past year seems to
	 * have become 'IGATE,I'. Also, now that the 'Q' structure has been
	 * implemented in the APRS-IS, it's probably a good idea for me to
	 * stick with the new 'standard' so that the 'Q' system works
	 * properly with the NOSaprs software.
	ptr += sprintf (ptr, ",%s,I", logon_callsign);
	 */
	ptr += sprintf (ptr, ",qAR,%s", logon_callsign);
#endif
	return ptr;
}

int aprs_send (char *data, int len)
{
	struct mbuf *bp;
	struct socket lsock,fsock;

	lsock.address = INADDR_ANY;
	lsock.port = 1315;

	/*
	 * 30May2001, VE4KLM, If this is not set, then set it to the address
	 * of this TNOS box. This basically means that the 'aprs internal'
	 * entry in autoexec.nos is no longer required except in special cases.
	 * ALOT of TNOS systems now have linux doing the encap and tunnel, so
	 * for those systems, there is no longer any need for 'aprs internal'.
	 */
	if (internal_hostid == 0)
		internal_hostid = Ip_addr;

	fsock.address = internal_hostid;	/* kludge for now */
	fsock.port = 93;

	if ((bp = alloc_mbuf (len)) == NULLBUF)
		return -1;

	strncpy ((char*)bp->data, data, len);

	bp->cnt = (int16)len;

	send_udp (&lsock, &fsock, 0, 0, bp, 0, 0, 0);

	return 0;
}

static char aprs_dest[2] = { 'A' << 1, 'P' << 1 };
static char beac_dest[2] = { 'B' << 1, 'E' << 1 };

/* 17May2001, Maiko, Oops forgot to add ID as a possible dest call */
static char id_dest[2] = { 'I' << 1, 'D' << 1 };

/*
 * 20Aug2001, VE4KLM, Put in NOGATE and RFONLY digi calls, decided to move
 * the optout of igate code into this module instead. That way I can keep
 * stats on who opted out, as well, there's no point sending to the main
 * server just to be discarded anyways
 */
static char optout1[AXALEN] = { 'N' << 1, 'O' << 1, 'G' << 1, 'A' << 1, 'T' << 1, 'E' << 1, '0' << 1 };
static char optout2[AXALEN] = { 'R' << 1, 'F' << 1, 'O' << 1, 'N' << 1, 'L' << 1, 'Y' << 1, '0' << 1 };

/*
 * 20Aug2001, VE4KLM, New function to quickly determine whether passed
 * digicall (internal format) is one of the optout callsigns
 */
static int optout (char *digicall)
{
	if (addreq (digicall, optout1))
		return 1;

	if (addreq (digicall, optout2))
		return 1;

	return 0;
}

/*
 * 20Aug2001, VE4KLM, Moved the digi processing code into a function instead,
 * because I had duplicate copies of this code in aprs_processing function,
 * so now we just call this function instead.
 */

static char *process_digis (struct ax25 *hdr, char *ptr)
{
	register int cnt;

	char tmp[AXBUF];

	if (hdr->ndigis)
	{
		for (cnt = 0; cnt < hdr->ndigis; cnt++)
		{
			/* 20Aug2001, VE4KLM, Check if user wants to opt out */
			if (optout (hdr->digis[cnt]))
			{
				if (aprs_debug)
				{
					aprslog (-1, "User [%s] opted out",
						pax25 (tmp, hdr->source));
				}
				return (char*)0;	/* this indicates an OPT OUT !!! */
			}

			/* 05Sep2001, VE4KLM, Better do this right, and actually check
			 * to see if the repeated BIT is set in the digi list
			 *
			ptr += sprintf (ptr, ",%s", pax25 (tmp, hdr->digis[cnt]));
			 *
			 */

			ptr += sprintf (ptr, ",%s%s", pax25 (tmp, hdr->digis[cnt]),
 					(hdr->digis[cnt][ALEN] & REPEATED) ? "*" : "");
		}

		/*
		 * the last digi needs an asterisk to identify it
		 * 05Sep2001, Maiko, This is not required anymore, since the above
		 * check for the H (repeated) BIT should take care of it now.
		 *
		ptr += sprintf (ptr, "*");
	 	 *
		 */
	}

	return ptr;
}

/*
 * Main APRS Frame Processing Call - called from within the ax25.c module
 */

extern	int	micemode;	/* 02Jul2003, Maiko, Defined in aprssrv.c */

static unsigned char aprs_data[305];

int aprs_processing (struct ax25 *hdr, struct iface *iface, struct mbuf *bp)
{
	char tmp[AXBUF], *ptr, *ptr2;
	int len, micepacket = 0;	/* 02Jul2003, New MIC-E options */
#ifdef	APRS_MIC_E
	char unsigned miceout[200];
#endif
	struct mbuf *tbp;

	/*
	 * 18Nov2005, Maiko, Oops, should have fixed this a long time ago.
	 * Anyone who ran an APRS enabled JNOS binary, but didn't have APRS
	 * configured, would see JNOS crash on each incoming APRS frame. The
	 * new conditionals below should fix this problem once and for all.
	 */
	if (logon_callsign == (char*)0 || aprs_port == (char*)0)
		return 0;

	len = len_p (bp);

	/* protection against buffer overflow perhaps (shud never happen) */
	if (len > 300)
		len = 300;

	if (dup_p (&tbp, bp, 0, len) != len)
	{
		aprslog (-1, "aprs_processing, dup_p failed, len %d", len);

		/*
		 * Do NOT free bp since it's used later on in
		 * the ax_recv() code.
		 * free_p (bp);
		 */

		return 0;
	}

	pullup (&tbp, (char*)aprs_data, len);

	/*
	 * 09Dec2020, Maiko (VE4KLM), if 0xc0 is at the end, then remove it,
	 * that's a remnant from the WinRPRP interface, it's a kludge removing
	 * it in here, it should be taken out inside winrpr.c, but suddenly it
	 * appears doing it inside winrpr.c is more involved then I thought.
	 *  (and I don't want to break anything just to fix this issue)
	 */

	if ((len > 0) && (aprs_data[len-1] == 0xc0))
	{
		/*
		 * log (-1, "removing 0xc0 from the end");
		 * confirmed working 10Dec2020, Maiko
		 */

		len--;
		aprs_data[len] = 0;
	}

#ifdef	APRS_MIC_E

	/*
	 * 15Jun2001, VE4KLM, Developing MIC-E support, new function, aprs_mice,
	 * to decode the destination call and user data, to see if it is indeed
	 * a MIC-E packet.
	 * 02Jul2003, Maiko, We now have option to ignore MIC-E completely.
	 */
	if (micemode)
		micepacket = aprs_mice ((unsigned char*)hdr->dest, aprs_data, miceout);

	/* 02Jul2003, Maiko, Only convert this if 'aprs mice convert' is set */
	if (micepacket && (micemode == 2))
	{
		/* don't forget to update the heard table */
		/* 18Jun2001, VE4KLM, don't forget to skip control and pid characters */
		/* 16Aug2001, Maiko, Changed parameter list, more info to use now */
		update_aprshrd (hdr, iface, *(aprs_data+2));

		ptr = data_APZxxx;

		/* Replace the MIC-E dest call with this IGATE's dest call */
		/* 20Jun2001, VE4KLM, Unproto call now defined in aprs.h */
		ptr += sprintf (ptr, "%s>%s", pax25 (tmp, hdr->source), APRSDC);

		/*
	 	 * 20Aug2001, VE4KLM, Unproto path processing now in it's own
		 * function (to cut code duplication). IF a NULL is returned,
		 * then it means user opted out or something else.
		 */
		if ((ptr = process_digis (hdr, ptr)) == (char*)0)
			return 1;

		/* 30Aug2001, Maiko, Now have a function to insert the POE call,
		 * however this is now on hold. See comments further down in this
		 * code regarding the POE stuff.
		 *
		 * 05Sep2001, Maiko, Decided to add this, since there has been
		 * some agreement on backwards compatibility. It's not 100 percent
		 * agreed, but the major players don't seem to mind using '-0N', so
		 * I'll make it an option for now. What is really unfortunate is the
		 * fact that the APRS Internet System has been around for some time
		 * now, and *still* they don't have an official APRS IS Spec :-(
		 *
		 * If sysop does not want POE injection, it can be switched off.
		 */
		if (aprs_poe)
			ptr = format_POE_call (ptr);

		/*
		 * 18Jun2001, VE4KLM, Better add the newline and make sure
		 * the string terminator is also sent to the world wide APRS
		 * network.
		 */
		ptr += sprintf (ptr, ":%s\n", miceout);

		/* use UDP to send it to myself (I use UDP to talk inside TNOS) */
		aprs_send (data_APZxxx, strlen (data_APZxxx) /*+ 1*/);

		return 1;
	}

#endif

	/*
	 * At present - only allow packets destined for callsigns 'BEACON'
	 * and 'ID', or any callsign beginning with the letters 'AP'.
	 * 02Jul2003, Now also allow RAW mic-e packets (validation is done
	 * earlier on in the routine) if we are configured for that.
	 */
	if ((hdr->dest[0] == aprs_dest[0] && hdr->dest[1] == aprs_dest[1]) ||
		(hdr->dest[0] == beac_dest[0] && hdr->dest[1] == beac_dest[1]) ||
		(hdr->dest[0] == id_dest[0] && hdr->dest[1] == id_dest[1]) ||
		micepacket)
	{
		/* If we get in here, then we have a possible APRS frame */

		ptr2 = (char*)aprs_data + 2;	/* skip ax25 ctl and pid fields */

		len = len - 2;

		/*
		 * Now validate the Data Type Identifier to see if this is
		 * really an APRS frame. Note, not all possible identifiers
		 * may be supported by this TNOS APRS Server implementation.
		 * 17May2001, VE4KLM, Now pass pointer to 'dti_is_valid ()'.
		 * Before I simply passed the DTI char, but now I need to
		 * pass the entire data string for stronger data checking.
		 * 09Jan2002, New DTI validation now in aprssrv module.
		 */

#ifdef	APRSC
		/* 08Jun2004, Maiko, Client needs to be able to see 3rd party */
		if (valid_dti (*ptr2) || *ptr2 == '}')
#else
		if (valid_dti (*ptr2))
#endif
		{
			/* 11Jan2002, Maiko, Attempt at better DTI data validation */
			if (!valid_dti_data (*ptr2, ptr2 + 1))
				return 1;

			/*
			 * 19Jul2001, VE4KLM, Oops - If we get a packet with our source
			 * call, then likely it happened because some local digipeater
			 * repeated it and we picked it up. Definitely ignore it !! And
			 * you never know, it could be an imposter (however unlikely).
			 */
			if (!strcmp (pax25 (tmp, hdr->source), logon_callsign))
			{
				if (aprs_debug)
					aprslog (-1, "dropping a packet originated by us");

				return 1;
			}

            /* 14May2001, VE4KLM, Now track User stats */
			/* 16Aug2001, Maiko, Changed parameter list, more info to use now */
            update_aprshrd (hdr, iface, *ptr2);

			/*
			 * If we get in here, then it's almost for sure an APRS
			 * frame. Now build a full message to send to an aprs.net
			 * server - prepend the data with a source path header,
			 * and send out via internal UDP to our TNOS server.
		 	 */

			ptr = data_APZxxx;

#ifdef	APRSC
			/*
			 * 08Jun2004, Maiko, Just pass third party as is (without
			 * the '}' suffix of course). It's already in the format
			 * required by the NOSaprs server and internet system.
			 */
			if (*ptr2 == '}')
				ptr += sprintf (ptr, "%.*s", len-1, ptr2+1);

			else
			{
#endif
			ptr += sprintf (ptr, "%s>", pax25 (tmp, hdr->source));

			ptr += sprintf (ptr, "%s", pax25 (tmp, hdr->dest));

			/*
		 	 * 20Aug2001, VE4KLM, Unproto path processing now in it's own
			 * function (to cut code duplication). IF a NULL is returned,
			 * then it means user opted out or something else.
			 */
			if ((ptr = process_digis (hdr, ptr)) == (char*)0)
				return 1;

			/*
			 * 29Aug2001, VE4KLM, Experimental insertion of MYCALL-Gn so that
			 * the packet can now be traced to which IGATE was used to inject
			 * it into the internet APRS system. Discussed at length with Bob
		 	 * Bruninga (WB4APR) and others in the APRS community, BUT ...
			 *
			 * 30Aug2001, I have now put this idea on hold because it seems
			 * the debate is still going on, and I don't see a light to the
			 * end of the tunnel yet... The latest proposal is now to have a
			 * leading 0 (zero) in the SSID which would keep the POE callsign
			 * valid. Waiting for the outcome (if there is one). In the mean
			 * time, I'll have to keep the POE out of the loop for now :-(
			 *
			 * 30Aug2001, Maiko, Created a function to insert the POE call
			 *
		 	 * 05Sep2001, Maiko, Decided to start using this as an option,
			 * please see further comments I have scribbled in at the other
			 * call made to the format_POE_call () function earlier on in
			 * this module.
			 *
			 * If sysop does not like POE injection, it can be switched off.
			 */
			if (aprs_poe)
				ptr = format_POE_call (ptr);

			ptr += sprintf (ptr, ":%.*s\n", len, ptr2);

			/* aprslog (-1, "APZxxx Data [%d] [%s]", len, data_APZxxx); */

#ifdef	APRSC
			}
#endif
			len = ptr - data_APZxxx;	/* 25Mar2018, do NOT include NULL */
 
			/* use UDP to send it to myself (I use UDP to talk inside TNOS) */
			aprs_send (data_APZxxx, len);
		}

		return 1;
	}

	return 0;
}

#endif

