#include "osc.h"
#include <string.h>

/* Round n up to the next 4-byte boundary */
static int pad4(int n) { return (n + 3) & ~3; }

/* Read a big-endian uint32 without alignment requirements */
static uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

bool osc_parse(const uint8_t *buf, int len, osc_msg_t *out) {
    if (len < 8 || buf[0] != '/') return false;

    /* Address string */
    int addr_raw = strnlen((const char *)buf, len) + 1;
    int addr_end = pad4(addr_raw);
    if (addr_end >= len) return false;

    /* Type-tag string — must start with ',' */
    const char *tags = (const char *)buf + addr_end;
    if (tags[0] != ',') return false;
    int tags_raw = strnlen(tags, len - addr_end) + 1;
    int tags_end = addr_end + pad4(tags_raw);
    if (tags_end > len) return false;

    out->address = (const char *)buf;
    out->types   = tags + 1;             /* skip ',' */
    out->args    = buf + tags_end;
    out->buf_end = len;
    return true;
}

/* Compute byte offset into args[] for argument at index idx */
static int arg_offset(const osc_msg_t *msg, int idx) {
    int off = 0;
    for (int i = 0; i < idx; i++) {
        switch (msg->types[i]) {
            case 'i': case 'f': case 'r': off += 4;   break;
            case 's': case 'S': {
                const char *s = (const char *)msg->args + off;
                off += pad4((int)strlen(s) + 1);
                break;
            }
            default: return -1;
        }
    }
    return off;
}

float osc_float(const osc_msg_t *msg, int idx) {
    if (!msg->types[idx] || msg->types[idx] != 'f') return 0.0f;
    int off = arg_offset(msg, idx);
    if (off < 0) return 0.0f;
    uint32_t raw = read_u32(msg->args + off);
    float val;
    memcpy(&val, &raw, 4);
    return val;
}

int32_t osc_int(const osc_msg_t *msg, int idx) {
    if (!msg->types[idx] || msg->types[idx] != 'i') return 0;
    int off = arg_offset(msg, idx);
    if (off < 0) return 0;
    return (int32_t)read_u32(msg->args + off);
}

const char *osc_string(const osc_msg_t *msg, int idx) {
    char t = msg->types[idx];
    if (!t || (t != 's' && t != 'S')) return NULL;
    int off = arg_offset(msg, idx);
    if (off < 0) return NULL;
    return (const char *)msg->args + off;
}
