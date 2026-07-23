/*
 * Since git has it's own cache.h which we include,
 * lets test on CGIT_CACHE_H to avoid confusion
 */

#ifndef CGIT_CACHE_H
#define CGIT_CACHE_H

#include <stdint.h>
#include <stddef.h>

typedef void (*cache_fill_fn)(void);


/* Print cached content to stdout, generate the content if necessary.
 *
 * Parameters
 *   size    max number of cache files
 *   path    directory used to store cache files
 *   key     the key used to lookup cache files
 *   ttl     max cache time in seconds for this key
 *   fn      content generator function for this key
 *
 * Return value
 *   0 indicates success, everything else is an error
 */
extern int cache_process(size_t size, const char *path, const char *key, int ttl,
			 cache_fill_fn fn);


/* List info about all cache entries on stdout */
extern int cache_ls(const char *path);

/* Print a message to stdout */
__attribute__((format (printf,1,2)))
extern void cache_log(const char *format, ...);

extern uint64_t hash64_str2(const void *d, size_t len, uint64_t h);
extern uint64_t hash64_str(const void *d, size_t len);
extern uint64_t hash64_str_clipped(const void *d, size_t len, size_t value_count);

#endif /* CGIT_CACHE_H */
