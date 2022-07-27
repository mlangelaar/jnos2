
/*
 * 24Oct2008, Maiko (VE4KLM), I need functions to encode/decode base64
 * for the new 'raw ip' over digital modes - using multipsk interface,
 * and other stuff that I may be doing with the digital modes, most of
 * which work with strictly ascii / printable characters only.
 * 
 * This is a test program using the base64 routines I got from :
 *
 *   http://www.koders.com/c/.......
 *
 * Compile using -> cc testb64.c base64.c -o testb64
 */

#include "base64.h"

main (int argc, char **argv)
{
	int inlen, outlen, cnt;

	char *in, *out;

	in = "testing";

	inlen = strlen (in);

	outlen = base64_encode_alloc (in, inlen, &out);

	if (!out && !outlen && inlen)
	{
		printf ("input too long\n");
		exit(1);
	}
	if (!out)
	{
		printf ("no memory\n");
		exit(1);
	}

	printf ("[%.*s]\n", outlen, out);

	if (!base64_decode_alloc (out, outlen, &in, &inlen) || !out)
		exit (1);

	printf ("[%.*s]\n", inlen, in);
}

