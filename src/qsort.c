/*
 * 05Apr2006, Maiko (VE4KLM), Time to replace the use of the system qsort()
 * call with an ANSI-C version that I found on the internet. This version I
 * found is quicker and uses ALOT less stack then the system version. They
 * have replaced the recursive nature of the function with explicit stack
 * operations. By the looks of the benchmark numbers, the improvement is
 * quite something. We'll have to see how well it works (so far so good).
 *
 * The websites I visited to get me this information and source code :
 *
 * 1) http://oopweb.com/Algorithms/Documents/Sman/Volume/Quicksort.html
 *
 * 2) http://epaperpress.com/sortsearch/txt/qsort.txt
 * 
 * Sorry, the source code has no credits, so I have no idea who deserves it.
 *
 * The only modification I have made is to put J2 prefixes in front of
 * the MAXSTACK and exchange definitions, and rename qsort to j2qsort,
 * to avoid any conflicts with system calls. Users should call the new
 * j2qsort() function to replace existing calls to the system qsort().
 *
 */

/* qsort() */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#define J2MAXSTACK (sizeof(size_t) * CHAR_BIT)

#define j2exchange(a, b, size) \
{ \
    size_t s; \
    int *ai, *bi; \
    char *ac, *bc; \
    ai = (int *)a; \
    bi = (int *)b; \
    for (s = sizeof(int); s <= size; s += sizeof(int)) { \
        int t = *ai; \
        *ai++ = *bi; \
        *bi++ = t; \
    } \
    ac = (char *)ai; \
    bc = (char *)bi; \
    for (s = s - sizeof(int) + 1; s <= size; s++) { \
        char t = *ac; \
        *ac++ = *bc; \
        *bc++ = t; \
    } \
}

void j2qsort(void *base, size_t nmemb, size_t size,
        int (*compar)(const void *, const void *)) {
    void *lbStack[J2MAXSTACK], *ubStack[J2MAXSTACK];
    int sp;
    unsigned int offset;

    /********************
     *  ANSI-C qsort()  *
     ********************/

    lbStack[0] = (char *)base;
    ubStack[0] = (char *)base + (nmemb-1)*size;
    for (sp = 0; sp >= 0; sp--) {
        char *lb, *ub, *m;
        char *P, *i, *j;

        lb = (char *)lbStack[sp];
        ub = (char *)ubStack[sp];

        while (lb < ub) {

            /* select pivot and exchange with 1st element */
            offset = (ub - lb) >> 1;
            P = lb + offset - offset % size;
            j2exchange (lb, P, size);

            /* partition into two segments */
            i = lb + size;
            j = ub;
            while (1) {
                while (i < j && compar(lb, i) > 0) i += size;
                while (j >= i && compar(j, lb) > 0) j -= size;
                if (i >= j) break;
                j2exchange (i, j, size);
                j -= size;
                i += size;
            }

            /* pivot belongs in A[j] */
            j2exchange (lb, j, size);
            m = j;

            /* keep processing smallest segment, and stack largest */
            if (m - lb <= ub - m) {
                if (m + size < ub) {
                    lbStack[sp] = m + size;
                    ubStack[sp++] = ub;
                }
                ub = m - size;
            } else {
                if (m - size > lb) {
                    lbStack[sp] = lb; 
                    ubStack[sp++] = m - size;
                }
                lb = m + size;
            }
        }
    }
}

