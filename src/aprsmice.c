
/*
 * APRS Services for JNOS 111x, TNOS 2.30 & TNOS 2.4x
 *
 * April,2001-June,2004 - Release (C-)1.16+
 *
 * Designed and written by VE4KLM, Maiko Langelaar
 *
 * For non-commercial use (Ham Radio) only !!!
 *
 * This is my implementation of the MIC-E decoder routine that let's me
 * decipher MIC-E data and send it to the world wide APRServe network.
 *
 */

/*
 * The following must be defined when linking into TNOSaprs. If you want
 * to test the routine as a standalone program, then #undefine the following
 * and do a 'make aprsmice'. That will generate a program called 'aprsmice'
 * which will parse the hard coded test data further below and output the
 * string sent to the world wide APRServe network (provided the test data
 * is in the proper MIC-E format).
 *
 */

#define	PRODUCTION_LIBRARY	/* undefine this for standalone test program */

#ifndef	PRODUCTION_LIBRARY

#define	APRSD

#define	APRS_MIC_E

#include <stdio.h>
#include <stdarg.h>

/*
 * KISS: Port 0 Data
 * AX25: W7NTF-14->T7PRVQ v W7NTF-5 UI pid=Text (0xf0)
 * 0000  a8 6e a0 a4 ac a2 60 ae 6e 9c a8 8c 40 fc ae 6e  (n $,"`.n.(.@|.n
 * 0010  9c a8 8c 40 6b 03 f0 27 32 32 76 6c 23 58 6b 2f  .(.@k.p'22vl#Xk/
 * 0020  5d 22 35 4d 7d 0d                                ]"5M}.
 *
 * User data starts with 27, after 03 (cmd) and f0 (pid).
 */

unsigned char destcall[7] = {

	'T' << 1, '7' << 1, 'P' << 1, 'R' << 1, 'V' << 1, 'Q' << 1, '0' << 1
};

unsigned char userdata[100] = {

	0x03, 0xf0, 0x27, 0x32, 0x32, 0x76, 0x6c, 0x23, 0x58, 0x6b, 0x2f,
	0x5d, 0x22, 0x35, 0x4d, 0x7d, 0x0d, 0x00
};

main (int argc, char **argv)
{
	unsigned char output[100];

	if (aprs_mice (destcall, userdata, output))
		printf ("[%s]\n", output);
	else
		printf ("this is not a mic-e packet\n");
}

#else

#include "global.h"

#endif

#ifdef	APRSD

#include "aprs.h"	/* 11Jul2001, Added prototype file */

#ifdef	APRS_MIC_E

static const char *messages[] = {
	"Off Duty",
	"En Route",
	"In Service",
	"Returning",
	"Committed",
	"Special",
	"Priority",
	"Emergency"
};

#ifndef	PRODUCTION_LIBRARY

int aprs_debug = 1;

static void aprslog (int val, char *format, ...)
{
	static char logbuffer[100];

	va_list	args;

	va_start (args, format);

	vsprintf (logbuffer, format, args);

	printf ("%s\n", logbuffer);
}

#endif

/*
 *
 * 20Jun2001, This is my own MIC-E decoding function that I wrote
 * from scratch, based on the information in Chapter 10 of the APRS
 * specification. I did have to refer to the APRSD 2.14 code as well,
 * since it is not documented anywhere how one is to format the info
 * sent to the world wide APRServe network and FINDU site.
 *
 * Tested using live TNC data from W7NTF-14. Thanks to Gary (W7NTF) for
 * being my test subject to make this work with his Kenwood D700.
 *
 */

extern int micemode;	/* 02Jul2003, Maiko, Defined in aprssrv.c */

/*
 * 17Sep2005, Maiko, Need a function outside this module,
 * to tell me whether a DTI is a MIC-E or not, and to cut
 * duplication of code, since this code was already here,
 * located directly in the aprs_mice () function.
 */

int mice_dti (unsigned char dti)
{
	int mice = 0;

	switch (dti)
	{
		case 0x60:
		case 0x27:
		case 0x1c:
		case 0x1d:
			mice = 1;
			break;
	}

	return mice;
}

int aprs_mice (unsigned char *dest, unsigned char *udata, unsigned char *out)
{
	unsigned char latInfo[6], longInfo[3], speedInfo[3], symtab, table;
	unsigned char LatBit = 0, LongBit = 0, L100 = 0, msg, *dptr = dest;
	int speed, course, mice = 0;
	unsigned char dti = '!';	/* default DTI to no messaging */
	register int cnt;

	udata += 2;		/* 18Jun2001, VE4KLM, skip control and pid characters */

	/*
	 * First make sure this is a 'possible' mic-e frame. No point
	 * consuming CPU and extracting data that isn't mic-e data.
	 */
	mice = mice_dti (*udata);	/* 17Sep2005, Maiko, Now a function */

	udata++;	/* skip the DTI */

	/* if not a possible mic-e frame, then leave now */
	if (!mice)
		return 0;

	/* PROCESS THE DESTINATION ADDRESS FIELD FIRST !!! */

	/* extract and validate latitude information */
	for (msg = 0, cnt = 0; cnt < 6; cnt++, dptr++)
	{
		/* 3rd highest bit must be set or it is not a mic-e frame */
		if (!(*dptr & 0x20))
		{
			if (aprs_debug)
				aprslog (-1, "3rd highest bit is not set");
			return 0;
		}

		/* Least significant bit must be set to the value zero,
		 * or else this is not a mic-e frame */
		if (*dptr & 0x01)
		{
			if (aprs_debug)
				aprslog (-1, "least significant bit is set");
			return 0;
		}

		/* extract message bits */
		if (cnt < 3)
			msg |= ((*dptr & 0x80) >> (7 - cnt));

		/* get latitude bit */
		else if (cnt == 3)
			LatBit = *dptr & 0x80;

		/* check for 100's of longitude degrees */
		else if (cnt == 4)
			L100 = *dptr & 0x80;

		/* get longitude bit */
		else if (cnt == 5)
			LongBit = *dptr & 0x80;

		/* extract the latitute 4-bit nibble */
		latInfo[cnt] = (*dptr >> 1) & 0x0f;

		/* validate the latitude data */
		switch (cnt)
		{
			case 0:
			case 1:
				if (latInfo[cnt] > 90) mice = 0;
				break;

			case 2:
			case 3:
			case 4:
			case 5:
				if (latInfo[cnt] > 59) mice = 0;
				break;
		}

		/* discard out of range latitudes */
		if (!mice)
		{
			if (aprs_debug)
				aprslog (-1, "latitude [%d] out of range", cnt);
			return 0;
		}
	}

	/* Least significant bit must be set to zero or else this
	 * is not a mic-e frame */
	if (*dptr & 0x01)
	{
		if (aprs_debug)
			aprslog (-1, "least significant bit is set");
		return 0;
	}

	/* 02Jul2003, Maiko, We still use this function to validate the
	 * data before passing unaltered to the APRS IS. I think it is a
	 * smart thing to make sure the MIC-E is reasonably legit before
	 * we just blindly dump it to the APRS IS simply based on DTI.
	 */
	if (micemode == 1)
		return 1;

	msg = ~msg & 0x7;

	/*
	 * Okay, now PROCESS THE USER DATA - the 'udata' pointer points to
	 * the first byte of the longitude information. Remember we skipped
	 * the mic-e flag at the very start before processing the destination
	 * address of this frame
	 */

	dptr = udata;

	/* extract longitude information */
	for (cnt = 0; cnt < 3; cnt++, dptr++)
		longInfo[cnt] = *dptr - 28;

	if (L100)
		longInfo[0] += 100;

	if (longInfo[0] >= 180 && longInfo[0] <= 189)
		longInfo[0] -= 80;
	else if (longInfo[0] >= 190 && longInfo[0]  <= 199)
		longInfo[0] -= 190;

	if (longInfo[1] >= 60)
		longInfo[1] -= 60;

	/* extract speed and course information */
	for (cnt = 0; cnt < 3; cnt++, dptr++)
		speedInfo[cnt] = *dptr - 28;

	speed = (int)speedInfo[0] * 10 + (int)speedInfo[1] / 10;
	if (speed >= 800) speed -= 800;

	course = (int)speedInfo[1] % 10 + (int)speedInfo[3];
	if (course >= 400) course -= 400;

	/* get the symbol and table data */
	symtab = *dptr++;
	table = *dptr++;

	/* this conditional I took from the APRSD 2.14 code. If station is a
	 * Kenwood, then show it as being able to do messaging (I should be
	 * carefull referencing this far into the user data memory. If I read
	 * out of bounds on some systems (like JNOS I think) it could crash,
	 * so really I should be checking if the data goes this high before
	 * I reference the pointer. NOTE TO MYSELF - CHECK OUT OF BOUNDS !
	 */
	if (*dptr == ']' || *dptr == '>')
		dti = '=';

	/*
	 * Now let's put together an output string to send to an APRS server
	 */

	out += sprintf ((char*)out, "%c%d%d%d%d.%d%d", dti, latInfo[0],
		latInfo[1], latInfo[2], latInfo[3], latInfo[4], latInfo[5]);

	if (LatBit)
		*out++ = 'N';
	else
		*out++ = 'S';

	out += sprintf ((char*)out, "%c%03d%02d.%02d", table, longInfo[0],
		longInfo[1], longInfo[2]);

	if (LongBit)
		*out++ = 'W';
	else
		*out++ = 'E';

	out += sprintf ((char*)out, "%c%03d/%03d/Mic-E/M%d/%s", symtab,
		course, speed, msg, messages[msg]);

	return mice;
}

#endif	/* end of APRS_MIC_E */

#endif	/* end of APRSD */

