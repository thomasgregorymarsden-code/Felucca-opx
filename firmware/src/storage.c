/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Persistent storage on the SPI NOR.
 *
 * Every object has an A/B sector pair. A save goes to the copy that is not
 * the current one: erase the sector, program the payload pages (from offset
 * 256), then the 32-byte header at offset 0 LAST. The header is the commit
 * record (magic, seq, length, payload CRC, header CRC); on load the valid
 * copy with the newest seq wins (including wrap), so a write torn at any point leaves the
 * previous copy in charge.
 *
 * Flash access goes through three hooks (also used by the host test):
 *   st_read(off, dst, n)   st_erase(off)   st_prog(off, src, n)
 */
#include "opx_map.h"                          /* OPX fork: FELUCCA_OPX, where its objects live */
#define ST_MAGIC 0x554C4546u                   /* "FELU" */
#define ST_SECTOR 4096u
#define ST_PAYLOAD_OFF 256u
#define ST_PAYLOAD_MAX (ST_SECTOR - ST_PAYLOAD_OFF)

/* flash map (FL_DATA 0x97000..0xDFFFF, FL_GLOB 0xFC000.., FL_AUTO 0xE5000..0xE6FFF, FL_VBANK 0xE7000): settings 0xFC000, projects
 * 0x97000..0x9EFFF, user sample slots 0xA0000..0xDBFFF (eng_sample.c), user preset banks 0xDC000..0xDFFFF (upreset.c),
 * the user presets' FM6 patches (up_fm6.c, since 1.0.3): copy A 0x9F000, copy B 0xFE000; the autosave (project.c,
 * 1.2): A 0xE5000, B 0xE6000, above the OTA staging (0xE0000..0xE4FFF), in the margin the stock firmware's own update
 * stages into (only ever while the stock firmware runs: a later Felucca finds no valid copy and starts as without one).
 * Those two sectors held the FM6 patch bank of 1.0..1.0.2 (OBJ_FM6BANK, retired): both objects use the same pair,
 * told apart by the commit record's type. The bank is only read, once, to move its patches into the user presets
 * (up_fm6.c upf_boot); the first write of the new object goes to the sector that does not hold the bank's newest
 * copy (st_save_to), so a power cut never loses both.
 * The FM6 voice bank (1.4.1, fm6_vbank.c): 0xE7000, the sector above the autosave, with a commit record of the same
 * kind (type OBJ_FM6VB, not an st_load object); a new bank is first staged in the autosave's older copy (fm6_vbank.c). */
/* OPX fork: OBJ_OPX0 + i (i < OPX_EXT_OBJS: project i's extension, OPX_EXT_OBJS - 1 the autosave's), at
 * OPX_EXT_LO (opx_map.h). On flash each is known by a fixed tag (OPX_EXT_TAG0 + i, st_tag), not by its place in this
 * enum, so objects added here later (upstream's or ours) do not change what the flash means. */
enum { OBJ_SETTINGS, OBJ_PROJECT0, OBJ_UPRESET0 = OBJ_PROJECT0 + 4, OBJ_FM6BANK = OBJ_UPRESET0 + 2, OBJ_UPFM6,
       OBJ_AUTOSAVE,
#if FELUCCA_OPX
       OBJ_OPX0, OBJ_OPX_END = OBJ_OPX0 + OPX_EXT_OBJS, OBJ_COUNT = OBJ_OPX_END
#else
       OBJ_COUNT
#endif
};
#define OBJ_FM6VB 32u                          /* the voice bank's commit records (fm6_vbank.c; >= OBJ_COUNT: st_load
                                                * never takes one, no object here takes its sectors) */

_Static_assert(OBJ_COUNT <= OBJ_FM6VB, "storage objects: below the voice bank's type");

typedef struct {
    uint32_t magic;
    uint16_t type, slot;
    uint32_t seq, len, crc, rsv[2];
    uint32_t hcrc;
} st_hdr_t;
_Static_assert(sizeof(st_hdr_t) == 32u, "storage commit record layout");

static int st_read(uint32_t off, void *dst, uint32_t n);
static int st_erase(uint32_t off);
static int st_prog(uint32_t off, const void *src, uint32_t n);

static uint32_t st_crc32_add(uint32_t c, const void *p, uint32_t n)   /* zlib CRC-32, 4 bits per step; running:
                                                                         * from 0xFFFFFFFF, the CRC is ~c */
{
    static const uint32_t T[16] = {
        0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu, 0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
        0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu, 0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu};
    const uint8_t *b = p;
    while (n--) {
        c ^= *b++;
        c = (c >> 4) ^ T[c & 15u];
        c = (c >> 4) ^ T[c & 15u];
    }
    return c;
}
static uint32_t st_crc32(const void *p, uint32_t n) { return ~st_crc32_add(0xFFFFFFFFu, p, n); }

/* the commit record's type for obj: its enum value, except the OPX objects' fixed tags */
static uint32_t st_tag(uint32_t obj)
{
#if FELUCCA_OPX
    if (obj >= OBJ_OPX0 && obj < OBJ_OPX_END)
        return OPX_EXT_TAG0 + (obj - OBJ_OPX0);
#endif
    return obj;
}

static uint32_t st_sector(uint32_t obj, uint32_t copy)  /* flash offset of copy A (0) / B (1) */
{
#if FELUCCA_OPX
    if (obj >= OBJ_OPX0 && obj < OBJ_OPX_END)
        return OPX_EXT_LO + (obj - OBJ_OPX0) * 2u * ST_SECTOR + copy * ST_SECTOR;
#endif
    if (obj == OBJ_SETTINGS)
        return 0xFC000u + copy * ST_SECTOR;
    if (obj == OBJ_FM6BANK || obj == OBJ_UPFM6)          /* (the same pair: see the flash map) */
        return copy ? 0xFE000u : 0x9F000u;
    if (obj == OBJ_AUTOSAVE)
        return 0xE5000u + copy * ST_SECTOR;
    if (obj >= OBJ_UPRESET0)
        return 0xDC000u + (obj - OBJ_UPRESET0) * 2u * ST_SECTOR + copy * ST_SECTOR;
    return 0x97000u + (obj - OBJ_PROJECT0) * 2u * ST_SECTOR + copy * ST_SECTOR;
}

static uint8_t st_buf[ST_PAYLOAD_MAX] __attribute__((aligned(4), section(".pool")));   /* (main loop only) */

/* the commit record at flash offset off: that of type tag, copy `copy`, valid: 0 (the place is the caller's: st_head
 * for an object where it is now, opx_ext_migrate for one where it was) */
static int st_head_at(uint32_t off, uint32_t tag, uint32_t copy, st_hdr_t *h)
{
    if (st_read(off, h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != tag || h->slot != copy || h->len > ST_PAYLOAD_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}
static int st_head(uint32_t obj, uint32_t copy, st_hdr_t *h)   /* commit record valid: 0 */
{
    if (obj >= OBJ_COUNT || copy > 1u)
        return -1;
    return st_head_at(st_sector(obj, copy), st_tag(obj), copy, h);
}

static int st_body_at(uint32_t off, const st_hdr_t *h)   /* payload of the sector at off -> st_buf, CRC ok: 0 */
{
    if (st_read(off + ST_PAYLOAD_OFF, st_buf, h->len) || st_crc32(st_buf, h->len) != h->crc)
        return -1;
    return 0;
}
static int st_body(uint32_t obj, uint32_t copy, const st_hdr_t *h) { return st_body_at(st_sector(obj, copy), h); }

/* the current copy of the A/B pair at offsets a_off / b_off (commit records of type tag): the newest valid sequence
 * (A on a tie), -1 when neither is valid. Headers first, so only the winner's payload is read (it is left in st_buf);
 * *h gets its header. */
static int st_current_at(uint32_t a_off, uint32_t b_off, uint32_t tag, st_hdr_t *h)
{
    st_hdr_t a, b;
    int va = st_head_at(a_off, tag, 0, &a) == 0, vb = st_head_at(b_off, tag, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u))) {
        if (st_body_at(b_off, &b) == 0) {
            *h = b;
            return 1;
        }
        vb = 0;
    }
    if (va && st_body_at(a_off, &a) == 0) {
        *h = a;
        return 0;
    }
    if (vb && st_body_at(b_off, &b) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}
static int st_current(uint32_t obj, st_hdr_t *h)
{
    if (obj >= OBJ_COUNT)
        return -1;
    return st_current_at(st_sector(obj, 0), st_sector(obj, 1), st_tag(obj), h);
}

/* load the whole object into dst; returns its length, or -1 if it does not fit */
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t i;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || st_current(obj, &h) < 0 || h.len > max)
        return -1;
    for (i = 0; i < h.len; i++)
        ((uint8_t *)dst)[i] = st_buf[i];
    return (int)h.len;
}

/* write st_buf[0..len) as copy `to` of obj with sequence seq: erase, payload, commit record last, read back */
static int st_commit(uint32_t obj, uint32_t to, uint32_t seq, uint32_t len)
{
    uint32_t base = st_sector(obj, to), off;
    int rc;
    st_hdr_t h;
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u) {
        uint32_t n = len - off > 256u ? 256u : len - off;
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, st_buf + off, n)) != 0)
            return rc;
    }
    h.magic = ST_MAGIC;
    h.type = (uint16_t)st_tag(obj);
    h.slot = (uint16_t)to;
    h.seq = seq;
    h.len = len;
    h.crc = st_crc32(st_buf, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)       /* the commit record, last */
        return rc;
    {   /* read back: a write-protected or failing part must not report SAVED */
        st_hdr_t chk;
        if (st_head(obj, to, &chk) || memcmp(&chk, &h, sizeof h) || st_body(obj, to, &chk))
            return -7;
    }
    return 0;
}

/* save into copy `to` (0 A, 1 B; -1: the one that is not the current copy, as st_save) */
static int st_save_to(uint32_t obj, const void *src, uint32_t len, int to)
{
    uint32_t seq, off;
    int cur;
    st_hdr_t h;
    if (obj >= OBJ_COUNT || len > ST_PAYLOAD_MAX || to > 1)
        return -1;
    cur = st_current(obj, &h);
    seq = cur < 0 ? 0u : h.seq;
    if (to < 0)
        to = cur == 0 ? 1 : 0;                        /* write the other copy */
    for (off = 0; off < len; off++)
        st_buf[off] = ((const uint8_t *)src)[off];    /* the driver wants RAM sources */
    return st_commit(obj, (uint32_t)to, seq + 1u, len);
}

static int st_save(uint32_t obj, const void *src, uint32_t len) { return st_save_to(obj, src, len, -1); }

#if FELUCCA_OPX
/* OPX fork, boot (before anything else touches the flash map): an OPX object with no valid copy where it lives now
 * (OPX_EXT_LO) is looked for at each former place (OPX_EXT_LEGACY, newest first) and, when a valid copy is found
 * there, written to the new place as copy A with the same sequence number. The old place is only read: it belongs to
 * whatever took it over. A move is therefore one edit of opx_map.h; a firmware that never moved does nothing here.
 * Returns the number of objects copied. */
static uint32_t opx_ext_migrate_from(const uint32_t *legacy, uint32_t nlegacy)
{
    uint32_t i, j, moved = 0;
    st_hdr_t h;
    for (i = 0; i < OPX_EXT_OBJS; i++) {
        uint32_t obj = OBJ_OPX0 + i;
        if (st_current(obj, &h) >= 0)
            continue;                                 /* a valid copy here: nothing to look for */
        for (j = 0; j < nlegacy; j++) {
            uint32_t lo = legacy[j] + i * 2u * ST_SECTOR;
            if (legacy[j] == OPX_EXT_LO || st_current_at(lo, lo + ST_SECTOR, st_tag(obj), &h) < 0)
                continue;
            if (st_commit(obj, 0, h.seq, h.len) == 0) /* (st_buf holds the old copy's payload) */
                moved++;
            break;
        }
    }
    return moved;
}
static uint32_t opx_ext_migrate(void)
{
    static const uint32_t legacy[] = {0u, OPX_EXT_LEGACY};   /* ([0]: a placeholder, so an empty list compiles) */
    return opx_ext_migrate_from(legacy + 1, (uint32_t)(sizeof legacy / sizeof legacy[0]) - 1u);
}
#endif
