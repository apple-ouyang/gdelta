#ifndef MSVC_COMPAT_H
#define MSVC_COMPAT_H


#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#include <stdio.h>

#define open            _open
#define read            _read
#define write           _write
#define close           _close
#define stat            _stat
#define fstat           _fstat
#define mkdir           _mkdir
#define snprintf        _snprintf
#if _MSC_VER <= 1200 /* Versions below VC++ 6 */
#define vsnprintf       _vsnprintf
#endif
#define O_RDONLY        _O_RDONLY
#define O_BINARY        _O_BINARY
#define O_CREAT         _O_CREAT
#define O_WRONLY        _O_WRONLY
#define O_TRUNC         _O_TRUNC
#define S_IREAD         _S_IREAD
#define S_IWRITE        _S_IWRITE
#define S_IFDIR         _S_IFDIR

// ctime
#include <Windows.h>
#define BILLION (1E9)
#define CLOCK_MONOTONIC -1

#ifdef __cplusplus
extern "C" {
#endif

struct timespec { long tv_sec; long tv_nsec; };

static BOOL g_first_time = 1;
static LARGE_INTEGER g_counts_per_sec;

int clock_gettime(int dummy, struct timespec *ct);


#ifdef __cplusplus
}
#endif

#endif
