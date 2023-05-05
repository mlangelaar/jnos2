
/*
 * Implementation of the WinLink password challenge/response protocol
 *
 * Coded by Maiko Langelaar / VE4KLM - working prototype 22Oct2015
 *
 * Based on sample VB code (WinlinkAuthTestVB) provided to me courtesy of
 * the Winlink Development Team. This new code is required for JNOS to be
 * able to successfully do secure login to any Winlink CMS server.
 *
 * NOTE : requires installation of linux 'openssl-devel' package !
 *
 * Also adapted their test Program for C - compile and run as follows :
 *
 *   cc -DTESTPROGRAM -DWINLINK_SECURE_LOGIN wlauth.c -lcrypto -o wlauth ; ./wlauth
 *
 * There will be some warnings, don't worry about it ...
 *
 * 25Jun2016, Maiko (VE4KLM), Added B2F directive and config.h !
 *
 * 12Oct2019, Maiko, B2F changed to WINLINK_SECURE_LOGIN in just
 * a few of the modules, B2F itself is now a permanent define !
 *
 */

#include "config.h"

/*
 * 12Oct2019, Changed from B2F to WINLINK_SECURE_LOGIN
 * see config.h.default and lzhuf.c for more detail
 */
#ifdef	WINLINK_SECURE_LOGIN

/* this is important, especially if openssl dev not installed */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/md5.h>

static unsigned char salt[64] = {

	77, 197, 101, 206, 190, 249,
	93, 200, 51, 243, 93, 237,
	71, 94, 239, 138, 68, 108,
	70, 185, 225, 137, 217, 16,
	51, 122, 193, 48, 194, 195,
	198, 175, 172, 169, 70, 84,
	61, 62, 104, 186, 114, 52,
	61, 168, 66, 129, 192, 208,
	187, 249, 232, 193, 41, 113,
	41, 45, 240, 16, 29, 228, 
	208, 228, 61, 20
};

/*
 * Calculate the MD5 hash of the supplied buffer
 *
 * NOTE : not thread safe, but I'm not concerned in this case.
 *
 */

static unsigned char *MD5ByteHash (unsigned char *iBuf)
{
	static unsigned char hash[MD5_DIGEST_LENGTH];

	MD5 (iBuf, strlen ((char*)iBuf), hash);

	return hash;
}

/*
 *
 * Calculate the challenge password response as follows :
 *
 * - concatenate challenge phrase, password, and secret value (salt)
 * - generate an MD5 hash of the result
 * - convert the first 4 bytes of the hash to an integer, return it
 *
 */

static int ChallengedPassword (char *challenge, char *password)
{
	unsigned char *tmpCP, *orgtmpCP, *retHash;

	int retVal, i;

	orgtmpCP = (unsigned char*) malloc (strlen(challenge)+strlen(password)+65);

	tmpCP = orgtmpCP;

	tmpCP += sprintf ((char*)tmpCP, "%s%s", challenge, password);

	memcpy (tmpCP, salt, 64); tmpCP += 64; *tmpCP = 0;	/* delimiter */
	
	retHash = MD5ByteHash (orgtmpCP);

	/*
	 * Create a positive integer return value from the hash bytes
     */

	retVal = (int) (retHash[3] & 0x3f);

	/* Trim the sign bit from what will be the high byte */

	for (i = 2; i > -1; i--)
       	retVal = (retVal << 8) | retHash[i];

	return retVal;
}

/*
 * This is the only function you need to call from JNOS - in particular from
 * within the source file, forward.c, during processing of the incoming SID.
 *
 * NOTE : this is not thread safe (response should be dynamically allocated
 * if you want that), but I figure for JNOS we only talk to one CMS at any
 * particular time, so it's fine for now.
 *
 */

char *J2ChallengeResponse (char *strChallengePhrase, char *strPassword)
{
	/*
	 * 09Jun2020, Maiko (VE4KLM), can't have a single static buffer
	 * anymore, replace the statis response buffer with a malloc().
	 */
	char *response = malloc (11);

	int resp = ChallengedPassword (strChallengePhrase, strPassword);

	sprintf (response, "%010d", resp);

	return (response+2);
}

#endif	/* end of WINLINK_SECURE_LOGIN */

#ifdef	TESTPROGRAM

/*
 * Test program
 */

int main ()
{
	printf ("WinlinkAuth (C) Test ");

	char *password = "ABC123XYZ";

	char *challenge = "45623893";

	char *response = J2ChallengeResponse (challenge, password);

	printf ("Password: %s Challenge: %s Response: %s\n",
		password, challenge, response);
}

#endif

