#ifndef GETOPT_H
#define GETOPT_H

/*
 * getopt --
 *      Parse argc/argv argument vector.
 */
int getopt(int nargc, char *const nargv[], const char *ostr);

char *optarg;
int opterr, optind, optopt, optreset;

#endif
