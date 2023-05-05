/* Simple utility for generating hashed password; concatenates challenge
 * (in hex) and password, without spaces or newlines, and feeds to MD5
 * Not needed in normal operation since JNOS etelnet does this for you.
 * Originally by KA9Q .. mod by N5KNX for Jnos 1.11x4.
 * Note: under Linux/gcc, compile via:
 *	echo "#define MD5AUTHENTICATE" > global.h
 *	gcc -o epass -O epass.c md5.c
 */
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "md5.h"

#define direct

#ifndef direct
#include "files.h"
#endif

void rip(char *s);
int main(void);

int
main()
{
        int32 challenge=0;
        char chall[80];
	char pass[80];
	MD5_CTX md;
	int i;

	printf("Enter challenge (in hex): ");
	fflush(stdout);
	fgets(chall,sizeof(chall),stdin);
	rip(chall);

	printf("Enter password: ");
	fflush(stdout);
	fgets(pass,sizeof(pass),stdin);
	rip(pass);

        for(i=0; i<strlen(chall); i++) {
            challenge <<= 4;
            challenge += (chall[i]>'9') ? (tolower(chall[i])-'a'+10) : chall[i]-'0';

        }

#ifdef direct
	MD5Init(&md);
	MD5Update(&md,(unsigned char *)&challenge,sizeof(challenge));
	MD5Update(&md,(unsigned char *)pass,strlen(pass));
	MD5Final(&md);

	for(i=0;i<16;i++)
		printf("%02x",md.digest[i] & 0xff);
	printf("\n");
#else
        printf("%s\n", md5sum(challenge, pass));    /* md5sum() in files.c */
#endif
	return 0;
}
void
rip(buf)
char *buf;
{
	char *cp;

	if((cp = strchr(buf,'\r')) != NULL)
		*cp = '\0';
	if((cp = strchr(buf,'\n')) != NULL)
		*cp = '\0';
}
