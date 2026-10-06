/* Tiny helper for robots: read "key=value,key=value" parameter strings. */
#ifndef RR_PARAMS_H
#define RR_PARAMS_H
#include <stdlib.h>
#include <string.h>

/* Returns the value of key in params, or def if absent or not a number. */
static inline float rr_param(const char* params, const char* key, float def) {
    size_t klen = strlen(key);
    const char* p = params;
    while (p && *p) {
        while (*p == ',' || *p == ' ') ++p;
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            char* end = NULL;
            float v = strtof(p + klen + 1, &end);
            return end != p + klen + 1 ? v : def;
        }
        p = strchr(p, ',');
    }
    return def;
}

static inline float rr_clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

#endif
