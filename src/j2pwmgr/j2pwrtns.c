
/*
 *
 * 30Sep2019, Maiko (VE4KLM), no more libcrypt ! See the README file.
 *
 * New way to store passwords for JNOS related operations, this is came
 * about from the need to keep the passwords for Winlink Secure Login safe
 * from prying eyes. Then I got to thinking perhaps it's time to start doing
 * the same for general JNOS passwords (ie, ftpusers, popusers, etc) in the
 * near future (but not yet). That is why I introduced the 'type' field, 1
 * for Winlink Secure Login, and 0 (default) for JNOS passwords.
 *
 * Coded during October of 2015 by Maiko Langelaar (VE4KLM)
 *
 * Based on ideas I found in code forums and crypt man pages ...
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <ctype.h>	/* 01Oct2019, Maiko (VE4KLM), compiler complaining about tolower */

#ifdef	ENCRYPT_PASSWORD_BLOCK
#include <crypt.h>
#endif

extern char *strlwr (char*);

#ifdef TESTPROGRAM
static int debug = 1;
#else
static int debug = 0;
#endif

/*
 * 02Nov2015, Maiko (VE4KLM), 'might' need it for stand alone program
 */
#ifdef	NOJ2STRLWR

/*
 * 30Oct2015, Maiko (VE4KLM), strupr() is part of JNOS source and
 * not on any linux distributions that I have been using, but just
 * in case, we should encase this in a compile time option ...
 */

char *strlwr (char *s)
{
    char *p = s;

    while (*p) *p = tolower (*p), p++;

    return s;
}

#endif

#ifdef	ENCRYPT_PASSWORD_BLOCK

/* 02Nov2015, Maiko (VE4KLM), moved here, need it for lib purposes */

static char keyset = 0;

void j2setkey ()
{
	char key[64] =
	{
		1,0,0,1,0,1,0,0, 1,0,1,0,1,0,1,1, 1,1,0,0,0,1,0,1, 1,1,1,1,0,0,0,1,
		0,0,0,1,0,0,1,0, 1,1,1,0,0,0,1,0, 1,0,1,1,0,1,0,0, 1,0,1,0,1,0,1,1
	};

	if (!keyset)
	{
		keyset = 1;

		setkey (key);
	}
}

#endif

/*
 * Function to convert a 64 byte block of bits representing an
 * 8 byte character string to that string. This block comes from
 * what is returned from the encrypt() (DECRYPT) function.
 *
 * 23Oct2015, Coded by Maiko Langelaar / VE4KLM
 *
 * 30Oct2015, decided to return password as a memory pointer, don't
 * like the idea of passing via function arg and stack variables !
 *
 * MAKE SURE : you free the pointer returned by this function !
 *
 * based off ideas from man pages and coding forums and so on ...
 *
static void j2conv_block2data (char *block, char *data)
 *
 */

char *j2conv_block2data (char *block)
{
	int cnt, bits;

	char *odata, *data;

	data = malloc (9);	/* passwords max 8 bytes */

	for (odata = data, cnt = 0; cnt < 8; cnt ++, data++)
	{
		for (*data = 0, bits = 0; bits < 8; bits++, block++)
		{
			if (debug)
				printf ("%d", *block);

			/* looked it up was using 2, no, use 1, x << y = x * 2^y */
			if (*block)
				*data |= (1 << bits);
		}

		if (debug)
			printf (" : %02x\n", *data);
	}

	*data = 0;	/* treat it as a string and null terminate it */

	/* 30Oct2015, Maiko (VE4KLM), take off padding, internal use only  */
	data = odata; while (*data && *data != ' ') data++; *data = 0;

	if (debug)
		printf ("converting block format to [%s]\n", odata);

	return odata;
}

/*
 * 24Oct2015, Maiko (VE4KLM), get type:password for specified user
 *
 * 30Oct2015, was void, now returning string as a malloc() pointer
 *
void j2userpasswd (int type, char *username, char *password)
 *
 */

char *j2userpasswd (int type, char *username)
{
	char filedpasswd[65], rusername[20];

	char passwdblock[65], *pbptr = passwdblock;

	int rtype, cnt;

#ifdef	DONT_COMPILE
	/* 15Jun2020, Maiko, This is bad, altering the passed callsign string */

	strlwr (username);	/* 30Oct2015, Maiko (VE4KLM), force lower */
#endif

	/* 02Nov2015, Maiko, okay incorporate account type in filename */
	sprintf (filedpasswd, "users/%s.%d.dat", username, type);

	strlwr (filedpasswd);	/* 15Jun2020, Maiko (VE4KLM), force lower on the FILENAME ! */

	/*
	 * originally I was thinking having one password file per user, and
	 * having all the different account types in the one file, but not so
	 * sure now, so let's just use separate files - code much simpler. I
	 * can always merge them into one file down the road if desired ...
	 */

	FILE *fp = fopen (filedpasswd, "r");

	if (fp)
	{
		fscanf (fp, "%d:%[^:]:%[^:]:", &rtype, rusername, filedpasswd);
		fclose (fp);
	}
	else return (char*)0;	/* JNOS now prints out error, no more perror() */

	if (debug)
		printf ("%s\n", filedpasswd);

	for (cnt = 0; cnt < 64; cnt++, pbptr++)
	{
		/* aggggg (not 1 and 0, more like '1' and '0', grrrrr */
		*pbptr = (filedpasswd[cnt] == '1' ? 1 : 0);

		if (debug)
			printf ("%d", *pbptr);
	}

	if (debug)
		printf ("\n");

#ifdef	ENCRYPT_PASSWORD_BLOCK

	j2setkey ();	/* 02Nov2015, Maiko (VE4KLM), setkey now in my libs */

	/*
	 * 30Oct2015, Maiko (VE4KLM), decrypt the password block, then convert
	 * to string which is needed in forward.c to do Winlink Secure Login.
	 */

	encrypt (passwdblock, 1);	/* decode */
#endif
	return (j2conv_block2data (passwdblock));	/* not using stack */
}

