
/*
 * New way to store passwords for JNOS related operations, this is came
 * about from the need to keep the passwords for Winlink Secure Login safe
 * from prying eyes. Then I got to thinking perhaps it's time to start doing
 * the same for general JNOS passwords (ie, ftpusers, popusers, etc) in the
 * near future (but not yet). That is why I introduced the 'type' field, 0
 * for Winlink Secure Login, and 1 for general JNOS passwords and so on.
 *
 * Designed and Coded during October of 2015 by Maiko Langelaar (VE4KLM)
 *
 * Compile & Run :
 *
 *    cc -DNOJ2STRLWR j2pwmgr.c j2pwrtns.c -o j2pwmgr -lcrypt; ./j2pwmgr
 *
 * Add -DTESTPROGRAM to the cc arguments to compile and run test program.
 *
 * Based on ideas and code found in code forums and crypt man pages, including
 * a post from 'Wayne C. Morris, Dec 16, 2004' to 'thecodingforums', under the
 * thread 're-right-way-of-using-setkey-3-and-encrypt-3.436192', very useful.
 *
 */

#ifdef	JUST_SOME_COMMENTS

   http://www.thecodingforums.com/threads/
     re-right-way-of-using-setkey-3-and-encrypt-3.436192/

   Wayne C. Morris, Dec 16, 2004

#endif

#define _XOPEN_SOURCE

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

/* 24Jun2016, Maiko, forgot a few compiler warnings, added rip function  */
extern char *j2userpasswd (int, char*);
extern char *strdup (const char*);
extern char *strlwr (char*);

#ifdef	ENCRYPT_PASSWORD_BLOCK
extern void j2setkey ();
#endif

#ifdef	TESTPROGRAM
static int debug = 1;
#else
static int debug = 0;
#endif

#ifndef NULLCHAR
#define NULLCHAR (char*)0
#endif

/* replace terminating end of line marker(s) with null */
void rip (register char *s)
{
    register char *cp;

    if((cp = strpbrk(s,"\r\n")) != NULLCHAR)    /* n5knx: was "\n" */
        *cp = '\0';
}

void display( char *msg )
{
	int i;

	for (i=0; i<64; i++, msg++)
		if (debug)
			printf( "%1d", (*msg ? 1 : 0) );

	if (debug)
		printf("\n");
}

/*
 * Function to convert an 8 character string to the 64 byte
 * block representing all the bits for that string. This block
 * will then be passed to the encrypt() function.
 *
 * 23Oct2015, Coded by Maiko Langelaar / VE4KLM
 *
 * based off ideas from man pages and coding forums and so on ...
 *
 */

static void j2conv_data2block (char *block, char *data)
{
	int cnt, bits;

	if (debug)
		printf ("converting [%s] to block format\n", data);

	for (cnt = 0; cnt < 8; cnt ++, data++)
	{
		if (debug)
			printf ("%02x : ", *data);

		for (bits = 0; bits < 8; bits++, block++)
		{
			/* looked it up was using 2, no, use 1, x << y = x * 2^y */
			*block = (*data & (1 << bits)) ? 1 : 0;

			if (debug)
				printf ("%d", *block);
		}

		if (debug)
			printf ("\n");
	}
}

/* 29Oct2015, Maiko (VE4KLM), delete a user */
void j2deluser (int type, char *username)
{
	char userpath[100];

	sprintf (userpath, "users/%s.dat", username);

	if (unlink (userpath)) perror (NULL);
}

/* 24Oct2015, Maiko (VE4KLM), create user file to hold their type:password */
void j2adduser (int type, char *username, char *passwdblock)
{
	char userpath[100];

	int cnt;

	strlwr (username);	/* 30Oct2015, Maiko (VE4KLM), force lower */

	/* 02Nov2015, Maiko, okay incorporate account type in filename */
	sprintf (userpath, "users/%s.%d.dat", username, type);

	FILE *fp = fopen (userpath, "w+");

	if (fp)
	{
		fprintf (fp, "%d:%s:", type, username);

		for (cnt = 0; cnt < 64; cnt++, passwdblock++)
			fprintf (fp, "%d", (*passwdblock ? 1 : 0));

		fclose (fp);
	}
	else perror (NULL);
}


#ifdef	TESTPROGRAM

int main ()
{
	/* EXAMPLES FROM THE WEB */

#ifdef	ENCRYPT_PASSWORD_BLOCK
	/* bit pattern for key */
	char key[64] =
	{
	1,0,0,1,0,1,0,0, 1,0,1,0,1,0,1,1, 1,1,0,0,0,1,0,1, 1,1,1,1,0,0,0,1,
	0,0,0,1,0,0,1,0, 1,1,1,0,0,0,1,0, 1,0,1,1,0,1,0,0, 1,0,1,0,1,0,1,1
	};
#endif
	/* bit pattern for messages */
	char txt[64] =
	{
	0,0,1,1,0,1,0,0, 0,1,0,0,1,0,1,1, 0,1,0,1,0,1,1,0, 0,0,1,0,1,0,0,0,
	1,0,1,0,1,1,0,0, 1,0,1,0,0,0,1,0, 1,0,1,0,1,0,0,0, 1,0,1,1,1,0,1,1
	};
	char data[10], user[10];


#ifdef	ENCRYPT_PASSWORD_BLOCK
    setkey (key);
#endif

    strcpy (user, "VE4KLM");

	/*
	 * only first 8 characters of a password are used
	 * 30OCT2015, Maiko (VE4KLM), put padding on the end, easier to remove
	 */
    sprintf (data, "%-8.8s", "PASSWD");

	j2conv_data2block (txt, data);

    display (txt);
#ifdef	ENCRYPT_PASSWORD_BLOCK
    encrypt(txt, 0);   /* encode */
#endif
    display (txt);

    j2adduser (0, user, txt);

#ifdef	ENCRYPT_PASSWORD_BLOCK
    encrypt(txt, 1);   /* decode */
#endif
    display (txt);

    j2conv_block2data (txt);

	if (debug)
		printf ("\n---- read password from file ----\n");

	j2userpasswd (0, user);

    return 0;
}

#else

int usage ()
{
	printf ("\n JNOS 2.0 password manager, Sep 2019, by Maiko Langelaar (VE4KLM)\n"); 
	printf ("\n  Usage: j2pwmgr -a <user> [option]\n\n   -a  create user\n   -d  delete user\n\n  Option:\n\n   -w  mark it as a Winlink user, the default is a JNOS user.\n\n");

	return 0;
}

int main (int argc, char **argv)
{
	char *cmd, *usr, *opt = (char*)0;

	char cleartxtpasswd[20], data[10];

#ifdef	MOVED_INTO_RTNS
	char key[64] =
	{
		1,0,0,1,0,1,0,0, 1,0,1,0,1,0,1,1, 1,1,0,0,0,1,0,1, 1,1,1,1,0,0,0,1,
		0,0,0,1,0,0,1,0, 1,1,1,0,0,0,1,0, 1,0,1,1,0,1,0,0, 1,0,1,0,1,0,1,1
	};
#endif

    char txt[64];

#ifdef	MOVED_INTO_RTNS
	setkey (key);
#endif

	if (argc < 3)
		return (usage ());

	argv++; cmd = strdup (*argv);

	if (*cmd != '-')
		return (usage ());

	argv++; usr = strdup (*argv);

	if (argc == 4)
	{
		argv++; opt = strdup (*argv);

		if (*opt != '-')
			return (usage ());
	}

	cmd++; if (opt) opt++;

#ifdef	ENCRYPT_PASSWORD_BLOCK
	j2setkey ();	/* 02Nov2015, Maiko (VE4KLM), setkey now in my libs */
#endif
	if (*cmd == 'a')
	{
		printf ("enter your password\n");

		/*
		 * 24Jun2016, Maiko, deprecated, supposedly dangerous function,
		 * so yeah okay whatever, replace with something that requires
		 * more code, typical C scare mongers ...
		 *
			gets (cleartxtpasswd);
		 */

		fgets (cleartxtpasswd, sizeof(cleartxtpasswd), stdin);

		rip (cleartxtpasswd);

	/*
	 * only first 8 characters of a password are used
	 * 30OCT2015, Maiko (VE4KLM), put padding on the end, easier to remove
	 */
		sprintf (data, "%-8.8s", cleartxtpasswd);

		if (debug)
			printf ("[%s] ", data);

		j2conv_data2block (txt, data);

    	display (txt);

#ifdef	ENCRYPT_PASSWORD_BLOCK
    	encrypt(txt, 0);   /* encode */
#endif

    	display (txt);

		j2adduser ((opt && (*opt == 'w')) ? 1 : 0, usr, txt);
	}
	else if (*cmd == 'd')
		j2deluser (0, usr);
/*
 * 30Oct2015, Maiko (VE4KLM), probably should leave this commented out
 * in any production environment, the j2userpasswd() is called from the
 * forward.c file when a Winlink Secure Login session is requested ...
 *
 * This was originally to help me debug all of this stuff, that's all.
 *
 */
	else if (*cmd == 'l')
		printf ("passwd %s\n",
			j2userpasswd ((opt && (*opt == 'w')) ? 1 : 0, usr));
/*
		printf ("passwd %s\n", j2userpasswd (0, usr));
*/

	return 0;
}

#endif

