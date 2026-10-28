/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX fork: the editor's OPX patches (EDITOR_PROTOCOL.md "OPX patches", cmds 121, 122; INFO `4F 01 ...`). A patch
 * travels as its packed record (opx_patch.c, OPX_PK_BYTES = 434 bytes, tools/opx_patch_spec.py), pack7 (as BACKUP's
 * data: a byte of high bits, then up to 7 bytes). Targets: 0 a track's own patch (0..3), 1 a factory patch
 * (0..OPX_NFACTORY - 1, read only), 2 a user preset's patch (0..UP_SLOTS - 1: an OPX user preset; opx_store.c).
 *   OPX_GET  target, index            -> target, index, rc, [the record, pack7]
 *   OPX_PUT  target, index, pack7     -> target, index, rc
 * rc: 0 ok, 1 a bad request (target, index, length, a record this firmware does not take: its version, a field out of
 * its range), 2 none (GET of a user preset without an OPX patch; PUT to a slot that is not an OPX preset, or a flash
 * error), 3 the transport would not stop (a user preset's PUT writes flash), 4 no room (16 OPX user presets). A PUT to
 * a track makes the patch the track's own (SLOT OWN), as an edit on the device does; a factory patch unchanged shows
 * as its SLOT F n. */
enum { ED_OPX_GET = 121, ED_OPX_PUT };
enum { ED_OPX_TRACK, ED_OPX_FACTORY, ED_OPX_USER };
static uint8_t ed_opx_pk[OPX_PK_BYTES] __attribute__((section(".pool")));

static void ed_opx_pack(const uint8_t *p, uint32_t n)      /* pack7: a mask byte, then up to 7 bytes */
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++)
            m |= (uint32_t)(p[i] >> 7) << i;
        ed_b(m);
        for (i = 0; i < k; i++)
            ed_b(p[i]);
        p += k;
        n -= k;
    }
}
static int ed_opx_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t rc = 1;
    if (cmd != ED_OPX_GET && cmd != ED_OPX_PUT)
        return 0;
    if (cmd == ED_OPX_GET) {
        if (n == 2u && a[0] == ED_OPX_TRACK && a[1] < NTRK) {
            memcpy(ed_opx_pk, opx_pk[a[1]], OPX_PK_BYTES);
            rc = 0;
        } else if (n == 2u && a[0] == ED_OPX_FACTORY && a[1] < OPX_NFACTORY) {
            opx_encode(&OPX_FACTORY[a[1]], ed_opx_pk);
            rc = 0;
        } else if (n == 2u && a[0] == ED_OPX_USER && a[1] < UP_SLOTS) {
            const uint8_t *pk = upx_find(a[1]);
            if (pk)
                memcpy(ed_opx_pk, pk, OPX_PK_BYTES);
            rc = pk ? 0u : 2u;
        }
        ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
        if (!rc)
            ed_opx_pack(ed_opx_pk, OPX_PK_BYTES);
        return 1;
    }
    if (n > 2u && ed_unpack7(a + 2, n - 2u, ed_opx_pk, OPX_PK_BYTES) == OPX_PK_BYTES && opx_pk_ok(ed_opx_pk)) {
        if (a[0] == ED_OPX_TRACK && a[1] < NTRK) {
            uint32_t f = 0;
            uint8_t fp[OPX_PK_BYTES];
            for (; f < OPX_NFACTORY; f++) {                /* (a factory patch unchanged: SLOT F n, as FM6_PUT) */
                opx_encode(&OPX_FACTORY[f], fp);
                if (!memcmp(fp, ed_opx_pk, OPX_PK_BYTES))
                    break;
            }
            opx_defaults(fp);                              /* (the INIT patch unchanged: SLOT INIT; else OWN) */
            if (f == OPX_NFACTORY)
                f = memcmp(fp, ed_opx_pk, OPX_PK_BYTES) ? OPX_OWN : OPX_INIT;
            opx_set_pk(a[1], ed_opx_pk);
            opx_slot[a[1]] = (uint8_t)f;                   /* (OWN, or F n: opx_poll keeps it) */
            trk[a[1]].p[P_E7] = (int16_t)f;
            ui.force = 1;
            rc = 0;
        } else if (a[0] == ED_OPX_USER && a[1] < UP_SLOTS) {
            if (!up_used(a[1]) || up_rec(a[1])->engine != ENGI_OPX)
                rc = 2;
            else if (ed_flash_stop())
                rc = 3;
            else {
                int r = upx_put(a[1], ed_opx_pk);
                rc = r == 4 ? 4u : r == 2 ? 2u : 0u;       /* (3, no flash: nothing kept, as no flash keeps none) */
            }
        }
    }
    ed_b(n ? a[0] : 127u); ed_b(n > 1u ? a[1] : 127u); ed_b(rc);
    return 1;
}
