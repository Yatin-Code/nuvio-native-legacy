#ifndef NUVIOSRC_MINIPOOL_FALLBACK_H
#define NUVIOSRC_MINIPOOL_FALLBACK_H

typedef void (*MpoolOptionalLookupStart)(void *context);
typedef int (*MpoolLocalThumbnailStart)(void *context);

/* Optional shared lookup startup never controls whether local work begins. */
static inline int minipool_start_local_fallback(void *context,
                                                MpoolOptionalLookupStart start_lookup,
                                                MpoolLocalThumbnailStart start_local) {
  if (start_lookup) start_lookup(context);
  return start_local ? start_local(context) : 0;
}

#endif
