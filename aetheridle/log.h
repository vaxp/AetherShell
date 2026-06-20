#ifndef _AETHERIDLE_LOG_H
#define _AETHERIDLE_LOG_H

#include <stdarg.h>
#include <string.h>
#include <errno.h>

enum log_importance {
    LOG_SILENT = 0,
    LOG_ERROR = 1,
    LOG_INFO = 2,
    LOG_DEBUG = 3,
    LOG_IMPORTANCE_LAST,
};

void aetheridle_log_init(enum log_importance verbosity);

#ifdef __GNUC__
#define _ATTRIB_PRINTF(start, end) __attribute__((format(printf, start, end)))
#else
#define _ATTRIB_PRINTF(start, end)
#endif

void _aetheridle_log(enum log_importance verbosity, const char *format, ...)
    _ATTRIB_PRINTF(2, 3);

#define aetheridle_log(verb, fmt, ...) \
    _aetheridle_log(verb, "[Line %d] " fmt, __LINE__, ##__VA_ARGS__)

#define aetheridle_log_errno(verb, fmt, ...) \
    aetheridle_log(verb, fmt ": %s", ##__VA_ARGS__, strerror(errno))

#endif
