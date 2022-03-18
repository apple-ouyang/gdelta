#ifndef GETOPT_H
#define GETOPT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * getopt --
 *      Parse argc/argv argument vector.
 */
int getopt(int nargc, char *const nargv[], const char *ostr);

char *optarg;
int opterr, optind, optopt, optreset;

#ifdef __cplusplus
}
#endif

#endif
