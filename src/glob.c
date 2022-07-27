#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#include <sys/stat.h>

#define GLOB_INTERNAL
#include "dirutil.h"

extern void j_free __ARGS((void *));

#ifdef __cplusplus
extern "C" {
#endif
extern uid_t geteuid __ARGS((void));
extern gid_t getegid __ARGS((void));
#ifdef __cplusplus
}
#endif

/*
 * Second approximation of glob routine for JNOS/Linux
 */

static int ff1(char *, char *, char *, struct ffblk *, int);

static char *
__ff_cat(char *pfx, char *sfx)
{
    static char buf[1024];

    buf[0] = '\0';
    if (pfx && *pfx)
	strcat(buf, pfx);
    if (pfx && *pfx && sfx && *sfx && (*pfx != '/' || pfx[1] != '\0'))
	strcat(buf, "/");
    if (sfx && *sfx)
	strcat(buf, sfx);
    return buf;
}

int
findnext(struct ffblk *ff)
{
    struct stat sb;
    char *path;
    int m;

    /* special-case literal: if there's no pattern, just close the ff */
    if (!ff->ff_pat || !ff->ff_dir)
	return -1;
    for (;;)
    {
	while ((ff->ff_cur = readdir(ff->ff_dir)) &&
		!wildmat(ff->ff_cur->d_name, ff->ff_pat, 0))
	    ;
	if (!ff->ff_cur)
	{
	    closedir(ff->ff_dir);
	    ff->ff_dir = 0;
	    j_free(ff->ff_pat);
	    j_free(ff->ff_pfx);
	    return -1;
	}
	path = j2strdup(__ff_cat(ff->ff_pfx, ff->ff_cur->d_name));
	if ((*ff->ff_cur->d_name == '.' && !(ff->ff_sattr & FA_HIDDEN)) ||
	    stat(path, &sb) == -1 ||
	    (!(ff->ff_sattr & FA_DIREC) && S_ISDIR(sb.st_mode)) ||
	    (!S_ISREG(sb.st_mode) && !(ff->ff_sattr & FA_SYSTEM) &&
	     !(ff->ff_sattr & FA_HIDDEN)))
	{
	    j_free(path);
	    continue;
	}
	j_free(path);
        m = 0007;
	if (geteuid() == sb.st_uid)
	    m |= 0700;
	if (getegid() == sb.st_gid)
	    m |= 0070;

	if (!(sb.st_mode & m) && !(ff->ff_sattr & FA_SYSTEM))
	    continue;
	ff->ff_ftime = *localtime(&sb.st_mtime);
	ff->ff_fsize = sb.st_size;
	if (S_ISDIR(sb.st_mode))
	    ff->ff_attrib = FA_DIREC;
	else if (!S_ISREG(sb.st_mode) || !(sb.st_mode & m))
	    ff->ff_attrib = FA_SYSTEM | FA_HIDDEN;
	else
	    ff->ff_attrib = FA_NORMAL;
	if (*ff->ff_cur->d_name == '.')
	    ff->ff_attrib |= FA_HIDDEN;
	if (!(sb.st_mode & m & 0222))
	    ff->ff_attrib |= FA_RDONLY;
	strcpy(ff->ff_name, ff->ff_cur->d_name);
	return 0;
    }
}

static int
ff1(char *prefix, char *dir, char *pat, struct ffblk *ff, int attr)
{
    char *pp, *cp, *ep, *xp, *bp;
    struct stat sb;
    int m;

    ff->ff_pfx = j2strdup(__ff_cat(prefix, dir));
    ff->ff_pat = 0;
    ff->ff_dir = 0;
    ff->ff_cur = 0;
    ff->ff_name[0] = '\0';
    ff->ff_sattr = attr;
    cp = ep = pp = j2strdup(pat);
    xp = 0;
    bp = "";
    for (; *cp; cp = ep)
    {
	while (*ep && *ep != '/') /* extract next component */
	{
	    if (*ep == '?' || *ep == '*' || *ep == '[')
		xp = ep;	/* record presence of wildcard characters */
	    ep++;
	}
	if (*ep)
	    *ep++ = '\0';
	if (xp)			/* if we got a wildcard, abort */
	    break;
	if (!*cp || strcmp(cp, ".") == 0) /* prune null components */
	    continue;
	xp = ff->ff_pfx;	/* append component to prefix */
	ff->ff_pfx = j2strdup(__ff_cat(xp, cp));
	j_free(xp);
	xp = 0;
	bp = cp;
    }
    if (!xp)			/* no wildcards; just return it */
    {
	strcpy(ff->ff_name, bp);
	if ((*bp == '.' && !(attr & FA_HIDDEN)) ||
	    stat(ff->ff_pfx, &sb) == -1 ||
	    (!(attr & FA_DIREC) && S_ISDIR(sb.st_mode)) ||
	    (!S_ISREG(sb.st_mode) && !(attr & FA_SYSTEM) &&
	     !(attr & FA_HIDDEN)))
	{
	    j_free(pp);
	    j_free(ff->ff_pfx);
	    return -1;
	}
	/* unreadable files are system files */
	/* don't check for root:  if you run nos as root you're dead anyway */
	m = 0007;
	if (geteuid() == sb.st_uid)
	    m |= 0700;
	if (getegid() == sb.st_gid) /* WARNING: ignores group vec */
	    m |= 0070;

	if (!(attr & FA_SYSTEM) && !(sb.st_mode & m))
	{
	    j_free(ff->ff_pfx);
	    j_free(pp);
	    return -1;
	}
	ff->ff_ftime = *localtime(&sb.st_mtime);
	ff->ff_fsize = sb.st_size;
	if (S_ISDIR(sb.st_mode))
	    ff->ff_attrib = FA_DIREC;
	else if (!S_ISREG(sb.st_mode) || !(sb.st_mode & m))
	    ff->ff_attrib = FA_SYSTEM | FA_HIDDEN;
	else
	    ff->ff_attrib = FA_NORMAL;
	if (*bp == '.')
	    ff->ff_attrib |= FA_HIDDEN;
	if (!(sb.st_mode & m & 0222))
	    ff->ff_attrib |= FA_RDONLY;
	j_free(pp);
	j_free(ff->ff_pfx);
	ff->ff_pfx = 0;
	return 0;
    }
    if (*ep)			/* no subdirs this version */
    {
	j_free(ff->ff_pfx);
	j_free(pp);
	return -1;
    }
    ff->ff_pat = j2strdup(cp);	/* the wildcarded component */
    j_free(pp);
    if (!(ff->ff_dir = opendir(*ff->ff_pfx? ff->ff_pfx: ".")))
    {
	j_free(ff->ff_pat);
	j_free(ff->ff_pfx);
	return -1;
    }
    return findnext(ff);
}

int
findfirst(char *pat, struct ffblk *ff, int attr)
{
    return ff1((*pat == '/'? "/": ""), "", (*pat == '/'? pat + 1: pat),
		 ff, attr);
}

void
findlast(struct ffblk *ff)
{
    if (!ff->ff_dir)
      return;
    closedir(ff->ff_dir);
    j_free(ff->ff_pat);
    j_free(ff->ff_pfx);
}
