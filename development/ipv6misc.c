
#include <string.h>
#include <stdio.h>

/* 03Apr2023, Maiko, for new j2_ipv6_asc2ntwk function further below */
#include "global.h"
#include "sockaddr.h"

/*
 * 27Mar2023, Maiko (VE4KLM) - an attempt (from scratch)
 * to show IPV6 in short form, not quite correct yet, but
 * good enough to shorten it to a readable form ...
 *
 * For an actual executable, make sure to #define DEMO below,
 * then 'make ipv6short', then run ./ipv6short to illustrate.
 *
 * For JNOS internal use, #undef DEMO, and pass 0 verbose !
 */

#undef DEMO

char *ipv6shortform (char *addr, int verbose)
{
	static char outbuffer[40];

	char *iptr, cutbuffer[8][5], *cptr;

	int cnt, dnt, blanks;

	if (verbose)
		printf ("stage 1 - break into chunks ...\n");

	for (iptr = addr, cnt = 0; *iptr; cnt++)
	{
		cptr = cutbuffer[cnt];

		while (*iptr && *iptr != ':')
			*cptr++ = *iptr++;

		*cptr = 0;

		if (verbose)
			printf (" [%s]", cutbuffer[cnt]);

		if (*iptr == ':')
			iptr++;
	}

	if (verbose)
		printf ("\nstage 2 - get rid of leading zeros ...\n");

	for (cnt = 0; cnt < 8; cnt++)
	{
		cptr = cutbuffer[cnt];

		while (*cptr && *cptr == '0')
			cptr++;

		strcpy (cutbuffer[cnt], cptr);

		if (verbose)
			printf (" [%s]", cutbuffer[cnt]);
	}

	if (verbose)
		printf ("\nstage 3 - reassemble chunks ...\n");

	for (cptr = outbuffer, cnt = 0 ; cnt < 8; cnt++)
	{
		if (*cutbuffer[cnt] != 'X' && *cutbuffer[cnt])
			cptr += sprintf (cptr, "%s", cutbuffer[cnt]);

		if (cnt != 7)
			*cptr++ = ':';
	}

	if (verbose)
		printf ("%s", outbuffer);

	if (verbose)
		printf ("\nstage 4 - do a few passes to minimize delimiters ");

	for (dnt = 0; dnt < 2; dnt++)
	{
		if (verbose)
			printf (".");

	cptr = strstr (outbuffer, "::");
	if (cptr)
	{
		cptr += 2;	/* need minimum 2 */
		iptr = cptr;	/* save the spot to shift back to */

		/* 18Apr2023, Maiko, All wrong, fix this us */
		while (*cptr && *cptr == ':')
			cptr++;

		/*
		 * So cptr is now at next group of numbers or at the end, and instead
		 * of incrementing cnt, it should contain length of data left between
		 * cptr and end of string for memcpy further down, yeah, that's it.
		 */

		// printf ("outbuffer [%s]\n", outbuffer);
		// printf ("strlen %d\n", strlen (outbuffer));
		// printf ("cptr [%s]\n", cptr);

		/* make sure to include string termination in shift, the +1 ... */
		cnt = outbuffer + strlen (outbuffer) - cptr + 1;

#ifdef	ALL_WRONG
		cnt = 0;
		while (*cptr && *cptr == ':')
		{
			cnt++;
			*cptr++;
		}
#endif
		// printf ("number of bytes to shift %d\n", cnt);

		/* shift over to the left to get rid of extra stuff */
		memcpy (iptr, cptr, cnt); 
	}

	}

	if (verbose)
		printf ("\n%s\n", outbuffer);

	return outbuffer;
}

#ifdef	DEMO

// static unsigned char *ipaddr = "2600:3c03:e003:3700:0000:0000:0000:0002";
static unsigned char *ipaddr = "2607:f8b0:4006:821:0000:0000:0000:2003";

main ()
{
	char *ptr = ipv6shortform (ipaddr, 1);
}

#endif	/* end of DEMO */

#ifndef	DEMO

/*
 * 03Apr2023, Maiko, set internal ipv6 address given a full length
 * text based representation. I am getting at three cases of where
 * I need this functionality, so copy and paste is getting wasteful
 * and lazy for that matter. Move it into it's own function now !
 */
int j2_ipv6_asc2ntwk (char *addr, struct j2sockaddr_in6 *socketv6)
{
	char segment[3], *ptr = addr;
	unsigned int segment_i, cnt;
	long segment_l;

	/* taken from the ipv6addr stuff in ipv6cmd.c (for now) */

	for (cnt = 0; cnt < 16; cnt++)
	{
		if (!(*ptr)) break;	/* out of input data */

		strncpy (segment, ptr, 2);

		segment_l = strtol (segment, NULL, 16);

		segment_i = (unsigned int)segment_l;

		socketv6->sin6_addr.s6_addr[cnt] = (unsigned char)segment_i;

		ptr += 2;	/* advance to next segment */

		if (*ptr == ':')	/* skip the delimiter */
			ptr++;
	}

	/* returns 1 if valid */
	return (cnt == 16);
}

#endif

