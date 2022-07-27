/*****************************************************************
	BASE64.C
	A simple utility for BASE64 encoding of ASCII strings.
	Usage: base64 <ascii_string>
	This utilitity is used to produce the base64 string needed to
	insert into an html document to authorize a remote access using the
	"Basic Authorization Scheme".

	5/6/1996	Selcuk Ozturk
			seost2+@pitt.edu
******************************************************************/



#include <stdio.h>
#include <string.h>

char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
void
main(int argc, char *argv[])
{
	char a,b,c,d,*cp;
	int i,tlen,glen,rem;

	if(argc < 2) {
		puts("usage: 'base64 username:password'");
		return;
	}
	cp = argv[1];
	tlen = strlen(cp);
	glen = tlen/3;

	for(i = 0; i < glen; i++) {
		a = (cp[0] >> 2);
		b = (cp[0] << 4) & 0x30;
		b |= (cp[1] >> 4);
		c = (cp[1] << 2) & 0x3c;
		c |= (cp[2] >> 6);
		d = cp[2] & 0x3f;
		cp +=3;
		printf("%c%c%c%c",table[a],table[b],table[c],table[d]);
	}
	rem = tlen - 3 * glen;
	if(rem == 0)
		return;
	if(rem == 1) {
		a = (cp[0] >> 2);
		b = (cp[0] << 4) & 0x30;
		printf("%c%c==",table[a],table[b]);
	} else {
		a = (cp[0] >> 2);
		b = (cp[0] << 4) & 0x30 ;
		b |= (cp[1] >> 4);
		c = (cp[1] << 2) & 0x3c;
		printf("%c%c%c=",table[a],table[b],table[c]);
	}
//	printf("\n%d %d %d %d",a & 0xff, b & 0xff, c & 0xff, d & 0xff);
	return;
}
