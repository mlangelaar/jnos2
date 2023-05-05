
/*
 * 11Mar2009, Maiko, A new JNOS version of tmpnam(), essentially the same code
 * as the original GLIBC version, but stripped down, and independant of GLIBC,
 * because I'm tired of that gcc link time warning about how dangerous it is,
 * and how we should be using mkstemp instead. I'll stick with tmpnam(), but
 * now I want full control of it, so it is now part of the JNOS source !
 *
 * This new module was built using source from the following GLIBC files :
 *
 *   stdio-common / tmpnam.c, tempname.c, and tempname.h
 *
 * The GNU Library GPL requires me to insert the following legalities :
 *
 */

/* Copyright (C) 1991, 1993, 1996, 1997, 1998 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the GNU C Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

#define	__need_size_t

/* March 11, 2002       Manuel Novoa III
 *
 * Modify code to remove dependency on libgcc long long arith support funcs.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
//#include <fcntl.h>
#include <unistd.h>
//#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>

/* Return nonzero if DIR is an existent directory.  */
static int direxists (const char *dir)
{
    struct stat buf;
    return stat(dir, &buf) == 0 && S_ISDIR (buf.st_mode);
}

/* Path search algorithm, for tmpnam, tmpfile, etc.  If DIR is
   non-null and exists, uses it; otherwise uses the first of $TMPDIR,
   P_tmpdir, /tmp that exists.  Copies into TMPL a template suitable
   for use with mk[s]temp.  Will fail (-1) if DIR is non-null and
   doesn't exist, none of the searched dirs exists, or there's not
   enough space in TMPL. */

static int j2path_search (char *tmpl, size_t tmpl_len, const char *dir, 
	const char *pfx, int try_tmpdir)
{
    size_t dlen, plen;

    if (!pfx || !pfx[0])
    {
	pfx = "jnos";	/* 01Sep2016, Maiko, Used to be 'file' */
	plen = 4;
    }
    else
    {
	plen = strlen (pfx);
	if (plen > 5)
	    plen = 5;
    }

    if (dir == NULL)
    {
	if (direxists (P_tmpdir))
	    dir = P_tmpdir;
	else if (strcmp (P_tmpdir, "/tmp") != 0 && direxists ("/tmp"))
	    dir = "/tmp";
	else
	    return -1;
    }

    dlen = strlen (dir);
    while (dlen > 1 && dir[dlen - 1] == '/')
	dlen--;			/* remove trailing slashes */

    /* check we have room for "${dir}/${pfx}XXXXXX\0" */
    if (tmpl_len < dlen + 1 + plen + 6 + 1)
	return -1;

    sprintf (tmpl, "%.*s/%.*sXXXXXX", (int) dlen, dir, (int) plen, pfx);
    return 0;
}

/* These are the characters used in temporary filenames.  */
static const char letters[] =
"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/* Generate a temporary file name based on TMPL.  TMPL must match the
   rules for mk[s]temp (i.e. end in "XXXXXX").  The name constructed
   does not exist at the time of the call to __gen_tempname.  TMPL is
   overwritten with the result.  

   This one simply verifies that the name does not exist at call time.

   We use a clever algorithm to get hard-to-predict names. */

static int j2gen_tempname (char *tmpl)
{
    char *XXXXXX;
    struct timeval tv;
    uint32_t high, low, rh;
    unsigned int k;
    int len, i, count;
    static uint64_t value; /* Do not initialize this, 
			      or lock it for multi-threaded
			      apps -- the messier the better */

    len = strlen (tmpl);
    if (len < 6 || strcmp (&tmpl[len - 6], "XXXXXX"))
	return -1;

    /* This is where the Xs start.  */
    XXXXXX = &tmpl[len - 6];

    /* Get some more or less random data.  */
    gettimeofday (&tv, NULL);
    value += ((uint64_t) tv.tv_usec << 16) ^ tv.tv_sec ^ getpid ();

    for (count = 0; count < TMP_MAX; value += 7777, ++count)
    {
	low = value & UINT32_MAX;
	high = value >> 32;

	for (i = 0 ; i < 6 ; i++) {
	    rh = high % 62;
	    high /= 62;
#define L ((UINT32_MAX % 62 + 1) % 62)
	    k = (low % 62) + (L * rh);
#undef L
#define H ((UINT32_MAX / 62) + ((UINT32_MAX % 62 + 1) / 62))
	    low = (low / 62) + (H * rh) + (k / 62);
#undef H
	    k %= 62;
	    XXXXXX[i] = letters[k];
	}

	{
	    struct stat st;

	    if (stat (tmpl, &st) < 0)
	    {
			if (errno == ENOENT)
			    return 0;
			else
			    return -1;
	    }
	}

	if (errno != EEXIST)
	    /* Any other error will apply also to other names we might
	       try, and there are 2^32 or so of them, so give up now. */
	    return -1;
    }

    /* We got out of the loop because we ran out of combinations to try.  */
    return -1;
}

static char tmpnam_buffer[L_tmpnam];

/* Generate a unique filename in P_tmpdir.
   This function is *not* thread safe when S == NULL!  
*/
char *j2tmpnam (char *s)
{
    /* By using two buffers we manage to be thread safe in the case
       where S != NULL.  */
    char tmpbuf[L_tmpnam];

    /* In the following call we use the buffer pointed to by S if
       non-NULL although we don't know the size.  But we limit the size
       to L_tmpnam characters in any case.  */
    if (j2path_search (s ? : tmpbuf, L_tmpnam, NULL, NULL, 0))
	return NULL;

    if (j2gen_tempname (s ? : tmpbuf))
	return NULL;

    if (s == NULL)
	return (char *) memcpy (tmpnam_buffer, tmpbuf, L_tmpnam);

    return s;
}
