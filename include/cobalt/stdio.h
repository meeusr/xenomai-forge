#ifndef __KERNEL__

#include_next <stdio.h>

#ifndef STDIO_H
#define STDIO_H

#include <stdarg.h>
#include <cobalt/wrappers.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

COBALT_DECL(int, vfprintf(FILE *stream, const char *fmt, va_list args));

COBALT_DECL(int, vprintf(const char *fmt, va_list args));

COBALT_DECL(int, fprintf(FILE *stream, const char *fmt, ...));

COBALT_DECL(int, printf(const char *fmt, ...));

COBALT_DECL(int, puts(const char *s));

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* STDIO_H */

#endif /* !__KERNEL__ */
