/*
 * No copyright. This trivial file is offered as-is, without any warranty.
 */

/* fmallloc.h -- memory allocation functions declaration */


#if !defined(FMALLOC_H)
#define FMALLOC_H

#include <stdlib.h>

#if defined(__GNUC__)
#define ATTRIBUTE_MALLOC __attribute__ ((__malloc__))
#else
#define ATTRIBUTE_MALLOC
#endif

#ifdef	__cplusplus
extern "C" {
#endif

void *fmalloc(size_t) ATTRIBUTE_MALLOC;
void *frealloc(void *, size_t) ATTRIBUTE_MALLOC;
void *fcalloc(size_t, size_t) ATTRIBUTE_MALLOC;
void ffree(void *);

#ifdef	__cplusplus
}
#endif

#endif /* FMALLOC_H */
