/*
 * Take an incoming message and queue it for JNOS's SMTP.
 * Usage is:  u2j NOS-address < file-with-RFC822-formatted-message
 * This works well with aliases and .forward files, perhaps for gatewaying
 * mailing lists into Jnos.
 *
 * You must edit *queue, below, to the value of your JNOS mqueue path.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <fcntl.h>

const char *queue = "/home/jpd/jnos/spool/mqueue/";  /* ADJUST TO SUIT! */

int
main(int argc, char * const * const argv)
{
    char fname[128], from[1024], line[1024];
    const char *to, *host;
    struct timeval tv;
    int h, q, fm, c;
    FILE *fp;
    long seq;

    host = strchr((to = argv[1]), '@') + 1;
    strcpy(fname, queue);
    strcat(fname, "sequence.lck");
    c = 0;
    while ((h = open(fname, O_WRONLY|O_CREAT|O_EXCL)) == -1)
    {
	if (errno != EEXIST || ++c == 5)
	{
	    perror(fname);
	    fprintf(stderr, "can't lock sequence file, aborting...\n");
	    return 1;
	}
	if ((fp = fopen(fname, "r")))
	{
	    fscanf(fp, "%ld", &seq);
	    fclose(fp);
	    if (kill(seq, 0) == -1 && errno == ESRCH)
	    {
		unlink(fname);
		continue;
	    }
	}
	tv.tv_sec = 0;
	tv.tv_usec = 500000;
	select(0, 0, 0, 0, &tv);
    }
    sprintf(fname, "%10d\n", getpid());
    write(h, fname, strlen(fname));
    close(h);
    strcpy(fname, queue);
    strcat(fname, "sequence.seq");
    if (!(fp = fopen(fname, "r+")))
    {
	perror(fname);
	fprintf(stderr, "can't open sequence file, aborting...\n");
	return 1;
    }
    fscanf(fp, "%ld", &seq);
    rewind(fp);
    fprintf(fp, "%ld\n", ++seq);
    fclose(fp);
    strcpy(fname, queue);
    strcat(fname, "sequence.lck");
    unlink(fname);
    sprintf(fname, "%s/%ld.lck", queue, seq);
    if (!(fp = fopen(fname, "w")))
    {
	perror(fname);
	fprintf(stderr, "can't create job lock file, aborting...\n");
	return 2;
    }
    fclose(fp);
    sprintf(fname, "%s/%ld.txt", queue, seq);
    if (!(fp = fopen(fname, "w")))
    {
	perror(fname);
	fprintf(stderr, "can't create job text file, aborting...\n");
	sprintf(fname, "%s/%ld.lck", queue, seq);
	unlink(fname);
	return 3;
    }
    h = 1;
    while (fgets(line, sizeof line, stdin))
    {
	if (h == 1)
	{
	    h = 2;
	    if (strncmp(line, "From ", 5) == 0)
	    {
		for (q = 5; line[q] && line[q] != ' '; q++)
		    ;
		line[q] = '\0';
		strcpy(from, line + 5);
		continue;
	    }
	}
	fputs(line, fp);
	if (!h)
	    continue;
	for (q = 0; line[q] && line[q] != ' ' && line[q] != ':'; q++)
	    ;
	if (line[q] != ':')
	{
	    h = 0;
	    continue;
	}
	fm = 0;
	if (strncasecmp(line, "from:", 5) == 0)
	{
	    for (q = 5; line[q] == ' ' || line[q] == '\t'; q++)
		;
	    strcpy(from, line + q);
	    for (q = 0; from[q] && from[q] != '\n'; q++)
		;
	    from[q] = '\0';
	    fm = 1;
	}
	for (q = 0; line[q] && line[q] != '\n'; q++)
	    ;
	/* handle long line */
	while (line[q] != '\n')
	{
	    if (!fgets(line, sizeof line, stdin))
		break;
	    fputs(line, fp);
	    if (fm)
	    {
		strcat(from, line);
		for (q = 0; from[q] && from[q] != '\n'; q++)
		    ;
		from[q] = '\0';
	    }
	    for (q = 0; line[q] && line[q] != '\n'; q++)
		;
	}
	while ((c = getchar()) == ' ' || c == '\t')
	{
	    /* RFC822 continuation */
	    ungetc(c, stdin);
	    if (!fgets(line, sizeof line, stdin))
		break;
	    fputs(line, fp);
	    for (q = 0; line[q] && line[q] != '\n'; q++)
		;
	    while (line[q] != '\n')
	    {
		if (!fgets(line, sizeof line, stdin))
		    break;
		fputs(line, fp);
		for (q = 0; line[q] && line[q] != '\n'; q++)
		    ;
	    }
	}
	if (c == EOF)
	    break;
	ungetc(c, stdin);
    }
    fclose(fp);
    sprintf(fname, "%s/%ld.wrk", queue, seq);
    if (!(fp = fopen(fname, "w")))
    {
	perror(fname);
	fprintf(stderr, "can't create job work file, aborting...\n");
	sprintf(fname, "%s/%ld.txt", queue, seq);
	unlink(fname);
	sprintf(fname, "%s/%ld.lck", queue, seq);
	unlink(fname);
	return 4;
    }
    fprintf(fp, "%s\n", host);
    fprintf(fp, "%s\n", from);
    fprintf(fp, "%s\n", argv[1]);
    fclose(fp);
    sprintf(fname, "%s/%ld.lck", queue, seq);
    unlink(fname);
    return 0;
}
