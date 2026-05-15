#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char    *address;  /* e.g. "/Mx/fader/4203"   */
    const char    *types;    /* type chars after ',', e.g. "f" */
    const uint8_t *args;     /* raw argument bytes (big-endian) */
    int            buf_end;  /* byte index of end of buffer, for bounds checks */
} osc_msg_t;

bool        osc_parse(const uint8_t *buf, int len, osc_msg_t *out);
float       osc_float(const osc_msg_t *msg, int idx);
const char *osc_string(const osc_msg_t *msg, int idx);
int32_t     osc_int(const osc_msg_t *msg, int idx);

/* Bundle support */
typedef struct { const uint8_t *_buf; int _len; int _off; } osc_bundle_iter_t;
bool osc_is_bundle(const uint8_t *buf, int len);
bool osc_bundle_init(const uint8_t *buf, int len, osc_bundle_iter_t *it);
bool osc_bundle_next(osc_bundle_iter_t *it, const uint8_t **out_buf, int *out_len);
