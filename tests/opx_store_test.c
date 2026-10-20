/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX fork: the OPX extension objects in storage.c and the room they take (opx_map.h), against a simulated NOR.
 *   the map: 5 A/B objects inside [OPX_EXT_LO, OPX_EXT_HI), apart from every upstream object; the region is the top of
 *   user sample slot 3, which keeps the rest (opx_smp_slot_size), and FELUCCA_OPX=0 gives the slot back;
 *   identity: the commit record's type is the fixed tag OPX_EXT_TAG0 + i; the upstream objects keep type = enum value;
 *   isolation: a save of an OPX object writes its own two sectors and nothing else;
 *   relocation (what a maintainer's move of OPX_EXT_LO does): objects found only at a former place are copied to the
 *   new place with their sequence, the former place is only read, a second boot copies nothing, a valid copy at the
 *   new place is never replaced, a corrupt former copy is not copied (the others still are), and the next save
 *   goes on from the migrated sequence; with no former places (OPX_EXT_LEGACY empty) a boot writes nothing.
 * Built twice by tests/run_tests.sh: FELUCCA_OPX=1 (all of the above) and FELUCCA_OPX=0 (no OPX object, slot 3 whole). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define __attribute__(x)

static uint8_t nor[0x100000];
static uint32_t writes;                              /* erases + programs */
static uint32_t w_lo = 0xFFFFFFFFu, w_hi;            /* the span written since the last reset */
static void w_note(uint32_t off, uint32_t n)
{
    writes++;
    if (off < w_lo) w_lo = off;
    if (off + n > w_hi) w_hi = off + n;
}
static void w_reset(void) { writes = 0; w_lo = 0xFFFFFFFFu; w_hi = 0; }
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { w_note(off, 4096); memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    w_note(off, n);
    if ((off & 0xFFu) + n > 256u) { printf("page wrap at %#x\n", off); exit(1); }
    for (i = 0; i < n; i++) nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"

static int check(const char *what, int ok)
{
    printf("opx store: %-86s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}

int main(void)
{
    int bad = 0;
    memset(nor, 0xFF, sizeof nor);

    /* the slots: the region is the top of slot 3 (or nothing) */
    bad += check("slots 1 and 2 keep 80 KiB", opx_smp_slot_size(0) == 0x14000u && opx_smp_slot_size(1) == 0x14000u);
#if FELUCCA_OPX
    bad += check("slot 3 ends where the region starts (40 KiB today), the region ends where slot 3 did",
                 opx_smp_slot_size(2) == OPX_EXT_LO - (OPX_SMP_BASE + 2u * OPX_SMP_SIZE) && opx_smp_slot_size(2) == 0xA000u &&
                     OPX_EXT_HI == OPX_SMP_BASE + 3u * OPX_SMP_SIZE);
    {
        uint32_t o, c, i, ok = 1;
        for (i = 0; i < OPX_EXT_OBJS; i++)
            for (c = 0; c < 2u; c++) {
                uint32_t s = st_sector(OBJ_OPX0 + i, c);
                ok &= s >= OPX_EXT_LO && s + 4096u <= OPX_EXT_HI && s == OPX_EXT_LO + (2u * i + c) * 4096u;
            }
        for (o = 0; o < OBJ_OPX0; o++)
            for (c = 0; c < 2u; c++)
                ok &= st_sector(o, c) + 4096u <= OPX_EXT_LO || st_sector(o, c) >= OPX_EXT_HI;
        bad += check("5 A/B objects fill the region, sector after sector; no upstream object inside it", ok);
        ok = 1;
        for (o = 0; o < OBJ_OPX0; o++)
            ok &= st_tag(o) == o;
        for (i = 0; i < OPX_EXT_OBJS; i++)              /* (0.2: new types for the new contents of 0..3, the
                                                         * autosave's kept; never 0.1.0's types of 0..3) */
            ok &= st_tag(OBJ_OPX0 + i) == OPX_OBJ_TAG(i) && st_tag(OBJ_OPX0 + i) >= OBJ_COUNT &&
                  (i == 4u ? st_tag(OBJ_OPX0 + i) == OPX_EXT_TAG0 + 4u : st_tag(OBJ_OPX0 + i) >= OPX_EXT_TAG0 + 5u);
        bad += check("commit record types: upstream objects unchanged, OPX objects fixed tags", ok);
    }

    /* save every OPX object: own sectors only, the tag on flash, loads back */
    {
        static uint8_t pay[OPX_EXT_OBJS][1800], got[1800];
        uint32_t i, k, ok = 1;
        st_hdr_t h;
        for (i = 0; i < OPX_EXT_OBJS; i++) {
            for (k = 0; k < sizeof pay[i]; k++)
                pay[i][k] = (uint8_t)(k * 7u + i * 31u + 1u);
            w_reset();
            ok &= st_save(OBJ_OPX0 + i, pay[i], sizeof pay[i]) == 0;
            ok &= w_lo == st_sector(OBJ_OPX0 + i, 0) && w_hi <= st_sector(OBJ_OPX0 + i, 0) + 4096u;
        }
        bad += check("each save writes copy A of its own object only", ok);
        ok = 1;
        for (i = 0; i < OPX_EXT_OBJS; i++) {
            memcpy(&h, nor + st_sector(OBJ_OPX0 + i, 0), sizeof h);
            ok &= h.type == OPX_OBJ_TAG(i) && h.magic == ST_MAGIC;
            ok &= st_load(OBJ_OPX0 + i, got, sizeof got) == (int)sizeof got && !memcmp(got, pay[i], sizeof got);
        }
        bad += check("on flash: magic and the fixed tag; every object loads back", ok);

        /* the boot of this firmware (no former places): nothing written */
        w_reset();
        bad += check("boot with OPX_EXT_LEGACY empty: no migration, no flash write", opx_ext_migrate() == 0 && !writes);

        /* a maintainer moves the region: what lives at OPX_EXT_LO today was at OLD before */
        {
            const uint32_t OLD = 0x93000u + 0x1000u * 0u;     /* (any sector-aligned place outside the region) */
            const uint32_t span = OPX_EXT_HI - OPX_EXT_LO;
            static uint8_t snap_old[0x10000];
            uint32_t legacy[2] = {0xC0000u, OLD};             /* newest first; nothing at 0xC0000 */
            memcpy(nor + OLD, nor + OPX_EXT_LO, span);
            memset(nor + OPX_EXT_LO, 0xFF, span);
            /* object 2: a corrupt payload in both former copies; object 3: a valid copy already at the new place */
            nor[OLD + 2u * 8192u + ST_PAYLOAD_OFF + 5u] ^= 0x40u;
            {
                uint8_t mine[1800];
                memset(mine, 0x5A, sizeof mine);
                ok = st_save(OBJ_OPX0 + 3u, mine, sizeof mine) == 0;
            }
            memcpy(snap_old, nor + OLD, span);
            w_reset();
            k = opx_ext_migrate_from(legacy, 2);
            ok &= k == OPX_EXT_OBJS - 2u;
            ok &= !memcmp(snap_old, nor + OLD, span);        /* the former place: only read */
            ok &= w_lo >= OPX_EXT_LO && w_hi <= OPX_EXT_HI;
            bad += check("relocation: 3 objects copied; the corrupt one and the one already there are not", ok);
            ok = 1;
            for (i = 0; i < OPX_EXT_OBJS; i++) {
                int n = st_load(OBJ_OPX0 + i, got, sizeof got);
                if (i == 2u) ok &= n < 0;
                else if (i == 3u) ok &= n == (int)sizeof got && got[0] == 0x5A && got[1799] == 0x5A;
                else ok &= n == (int)sizeof got && !memcmp(got, pay[i], sizeof got);
            }
            bad += check("relocation: the copied objects load their old payload; the new one wins over the old", ok);
            memcpy(&h, nor + st_sector(OBJ_OPX0, 0), sizeof h);
            ok = h.seq == 1u;
            w_reset();
            ok &= opx_ext_migrate_from(legacy, 2) == 0 && !writes;
            bad += check("relocation: the sequence is kept; a second boot copies nothing", ok);
            memset(got, 0x77, sizeof got);
            ok = st_save(OBJ_OPX0, got, sizeof got) == 0;
            memcpy(&h, nor + st_sector(OBJ_OPX0, 1), sizeof h);
            ok &= h.seq == 2u && st_load(OBJ_OPX0, got, sizeof got) == (int)sizeof got && got[0] == 0x77;
            bad += check("relocation: the next save goes to copy B with the next sequence", ok);
            /* a torn copy at the new place (valid record, bad payload) does not hide the former copy */
            memset(nor + OPX_EXT_LO + 4u * 8192u, 0xFF, 8192u);
            {
                uint8_t junk[64];
                memset(junk, 0x11, sizeof junk);
                ok = st_save(OBJ_OPX0 + 4u, junk, sizeof junk) == 0;
                nor[st_sector(OBJ_OPX0 + 4u, 0) + ST_PAYLOAD_OFF] ^= 1u;
            }
            ok &= opx_ext_migrate_from(legacy, 2) == 1u && st_load(OBJ_OPX0 + 4u, got, sizeof got) == (int)sizeof got &&
                  !memcmp(got, pay[4], sizeof got);
            bad += check("relocation: a torn copy at the new place is replaced by the former valid copy", ok);
        }
    }
#else
    bad += check("FELUCCA_OPX=0: slot 3 keeps 80 KiB", opx_smp_slot_size(2) == 0x14000u);
    bad += check("FELUCCA_OPX=0: no OPX object (the autosave is the last)", OBJ_COUNT == OBJ_AUTOSAVE + 1);
#endif
    printf(bad ? "opx store: FAILED\n" : "opx store: all ok\n");
    return bad != 0;
}
