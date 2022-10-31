/*******************************************************************************

License: 
This software and/or related materials was developed at the National Institute
of Standards and Technology (NIST) by employees of the Federal Government
in the course of their official duties. Pursuant to title 17 Section 105
of the United States Code, this software is not subject to copyright
protection and is in the public domain. 

This software and/or related materials have been determined to be not subject
to the EAR (see Part 734.3 of the EAR for exact details) because it is
a publicly available technology and software, and is freely distributed
to any interested party with no licensing requirements.  Therefore, it is 
permissible to distribute this software as a free download from the internet.

Disclaimer: 
This software and/or related materials was developed to promote biometric
standards and biometric technology testing for the Federal Government
in accordance with the USA PATRIOT Act and the Enhanced Border Security
and Visa Entry Reform Act. Specific hardware and software products identified
in this software were used in order to perform the software development.
In no case does such identification imply recommendation or endorsement
by the National Institute of Standards and Technology, nor does it imply that
the products and equipment identified are necessarily the best available
for the purpose.

This software and/or related materials are provided "AS-IS" without warranty
of any kind including NO WARRANTY OF PERFORMANCE, MERCHANTABILITY,
NO WARRANTY OF NON-INFRINGEMENT OF ANY 3RD PARTY INTELLECTUAL PROPERTY
or FITNESS FOR A PARTICULAR PURPOSE or for any purpose whatsoever, for the
licensed product, however used. In no event shall NIST be liable for any
damages and/or costs, including but not limited to incidental or consequential
damages of any kind, including economic damage or injury to property and lost
profits, regardless of whether NIST shall be advised, have reason to know,
or in fact shall know of the possibility.

By using this software, you agree to bear all risk relating to quality,
use and performance of the software and/or related materials.  You agree
to hold the Government harmless from any claim arising from your use
of the software.

*******************************************************************************/

/***********************************************************************
      LIBRARY: FING - NIST Fingerprint Systems Utilities

      FILE:           BZ_IO.C
      ALGORITHM:      Allan S. Bozorth (FBI)
      MODIFICATIONS:  Michael D. Garris (NIST)
                      Stan Janet (NIST)
      DATE:           09/21/2004
      UPDATED:        01/11/2012 by Kenneth Ko
      UPDATED:        03/08/2012 by Kenneth Ko
      UPDATED:        07/10/2014 by Kenneth Ko

      Contains routines responsible for supporting command line
      processing, file and data input to, and output from the
      Bozorth3 fingerprint matching algorithm.

***********************************************************************

      ROUTINES:
#cat: parse_line_range - parses strings of the form #-# into the upper
#cat:            and lower bounds of a range corresponding to lines in
#cat:            an input file list
#cat: set_progname - stores the program name for the current invocation
#cat: set_probe_filename - stores the name of the current probe file
#cat:            being processed
#cat: set_gallery_filename - stores the name of the current gallery file
#cat:            being processed
#cat: get_progname - retrieves the program name for the current invocation
#cat: get_probe_filename - retrieves the name of the current probe file
#cat:            being processed
#cat: get_gallery_filename - retrieves the name of the current gallery
#cat:            file being processed
#cat: get_next_file - gets the next probe (or gallery) filename to be
#cat:            processed, either from the command line or from a
#cat:            file list
#cat: get_score_filename - returns the filename to which the output line
#cat:            should be written
#cat: get_score_line - formats output lines based on command line options
#cat:            specified
#cat: bz_load -  loads the contents of the specified XYT file into
#cat:            structured memory
#cat: fd_readable - when multiple bozorth processes are being run
#cat:            concurrently and one of the processes determines a
#cat:            has been found, the other processes poll a file
#cat:            descriptor using this function to see if they
#cat:            should exit as well

***********************************************************************/

#include <string.h>
#include <ctype.h>
#include "bozorth3/bozorth3.h"
#include "bz_io.hpp"

/***********************************************************************/
int parse_line_range(const char *sb, int *begin, int *end) {
    int ib, ie;
    char *se;


    if (!isdigit(*sb))
        return -1;
    ib = atoi(sb);

    se = strchr(sb, '-');
    if (se != (char *) NULL) {
        se++;
        if (!isdigit(*se))
            return -2;
        ie = atoi(se);
    } else {
        ie = ib;
    }

    if (ib <= 0) {
        if (ie <= 0) {
            return -3;
        } else {
            return -4;
        }
    }

    if (ie <= 0) {
        return -5;
    }

    if (ib > ie)
        return -6;

    *begin = ib;
    *end = ie;

    return 0;
}

/***********************************************************************/

/* Used by the following set* and get* routines */
static char program_buffer[1024];

/***********************************************************************/
char *get_progname(void) {
    return program_buffer;
}

/***********************************************************************/
char *get_next_file(
        char *fixed_file,
        FILE *list_fp,
        FILE *mates_fp,
        int *done_now,
        int *done_afterwards,
        char *line,
        int argc,
        char **argv,
        int *optind,

        int *lineno,
        int begin,
        int end
) {
    char *p;
    FILE *fp;


    if (fixed_file != (char *) NULL) {
        return fixed_file;
    }


    fp = list_fp;
    if (fp == (FILE *) NULL)
        fp = mates_fp;
    if (fp != (FILE *) NULL) {
        while (1) {
            if (fgets(line, MAX_LINE_LENGTH, fp) == (char *) NULL) {
                *done_now = 1;
                return (char *) NULL;
            }
            ++*lineno;

            if (begin <= 0)         /* no line number range was specified */
                break;
            if (*lineno > end) {
                *done_now = 1;
                return (char *) NULL;
            }
            if (*lineno >= begin) {
                break;
            }
            /* Otherwise ( *lineno < begin ) so read another line */
        }

        p = strchr(line, '\n');
        if (p == (char *) NULL) {
            *done_now = 1;
            return (char *) NULL;
        }
        *p = '\0';

        p = line;
        return p;
    }


    p = argv[*optind];
    ++*optind;
    if (*optind >= argc)
        *done_afterwards = 1;
    return p;
}

/***********************************************************************/
/* returns CNULL on error */
char *get_score_filename(const char *outdir, const char *listfile) {
    const char *basename;
    int baselen;
    int dirlen;
    int extlen;
    char *outfile;

    basename = strrchr(listfile, '/');
    if (basename == CNULL) {
        basename = listfile;
    } else {
        ++basename;
    }
    baselen = strlen(basename);
    if (baselen == 0) {
        fprintf(errorfp, "%s: ERROR: couldn't find basename of %s\n", get_progname(), listfile);
        return (CNULL);
    }
    dirlen = strlen(outdir);
    if (dirlen == 0) {
        fprintf(errorfp, "%s: ERROR: illegal output directory %s\n", get_progname(), outdir);
        return (CNULL);
    }

    extlen = strlen(SCOREFILE_EXTENSION);
    outfile = malloc_or_return_error(dirlen + baselen + extlen + 2, "output filename");
    if (outfile == CNULL)
        return (CNULL);

    sprintf(outfile, "%s/%s%s", outdir, basename, SCOREFILE_EXTENSION);

    return outfile;
}

/***********************************************************************/
char *get_score_line(
        const char *probe_file,
        const char *gallery_file,
        int n,
        int static_flag,
        const char *fmt
) {
    int nchars;
    char *bufptr;
    static char linebuf[1024];

    nchars = 0;
    bufptr = &linebuf[0];
    while (*fmt) {
        if (nchars++ > 0)
            *bufptr++ = ' ';
        switch (*fmt++) {
            case 's':
                sprintf(bufptr, "%d", n);
                break;
            case 'p':
                sprintf(bufptr, "%s", probe_file);
                break;
            case 'g':
                sprintf(bufptr, "%s", gallery_file);
                break;
            default:
                return (char *) NULL;
        }
        bufptr = strchr(bufptr, '\0');
    }
    *bufptr++ = '\n';
    *bufptr = '\0';

    if (static_flag) {
        return &linebuf[0];
    } else {
        size_t len = strlen(linebuf) + 1;
        char *buf = malloc(len);    /* Caller must free() */
        if (buf == NULL)
            return buf;
        strncpy(buf, linebuf, len);
        return buf;
    }
}