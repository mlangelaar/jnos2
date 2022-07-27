/* This is a junky piece of C designed to take a uuencoded file
   as the first parameter and decode it into the file given as
   the second parameter. there is no error checking or other
   warning if it encounters a problem. The purpose of this
   program is to act as a simple bootstrap to enable the initial
   transfer of a file to allow more advanced binary transfers.
  
   In particular, note that the first line of the file to be
   decoded should be the 'begin' line produced by a uuencode.
  
   This code (for what it is worth) is Copyright 1990, by
   David R. Evans, NQ0I.
  
*/
  
#define byte unsigned char
  
#include <stdio.h>
#include <string.h>
  
main(argc, argv)
int argc;
char** argv;
{ char line[100];
    FILE *infile, *outfile;
  
/* open the files */
    infile = fopen(argv[1], "r");
    outfile = fopen(argv[2], "w");
  
/* skip the first (begin) line */
    fgets(line, 100, infile);
  
/* decode the remainder of the file */
    while (fgets(line, 100, infile), strncmp(line, "end", 3))
    { int n_to_write, n_to_read, n_reads, n, m;
        byte b[4], out[3];
  
        n_to_write = (int)line[0] - 32;
        n_to_read = ((n_to_write + 2) / 3) * 4;
        if(strlen(line) - 2 < n_to_read)    /* EOF apparently */
            n_to_read = 0;
        n_reads = n_to_read / 4;
        for (n = 0; n < n_reads; n++)
        { for (m = 0; m < 4; m++)
                b[m] = line[1 + 4 * n + m] - 32;
  
/* now do the actual decode */
            out[0] = (b[0] << 2) | ((b[1] & 0x30) >> 4);
            out[1] = ((b[1] & 0x0f) << 4) | ((b[2] & 0x3c) >> 2);
            out[2] = ((b[2] & 0x03) << 6) | (b[3]);
            if(n_to_write > 3)
                for (m = 0; m < 3; m++)
                    fputc(out[m], outfile);
            else
                for (m = 0; m < n_to_write; m++)
                    fputc(out[m], outfile);
            n_to_write -= m;
        }
    }
    fflush(outfile);
}
  
