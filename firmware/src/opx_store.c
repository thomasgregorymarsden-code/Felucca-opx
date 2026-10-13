/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX (OPX fork): the tracks' OPX patches saved beside a project, in its extension object (storage.c OBJ_OPX0 + i:
 * i = the project slot 0..3, OPX_EXT_AUTOSAVE the autosave's; where they live: opx_map.h, docs/OPX_STORAGE.md).
 *
 * A project record has no room for four 434-byte patches (and its format is upstream's), so they go in an object of
 * their own, written after the project and tied to it: `link` is the CRC-32 of the project record as stored. A load
 * takes the patches only when the link matches the record it loaded; anything else (no extension, a torn one, one
 * left from an older project in that slot, a project restored from a backup or written by firmware without OPX)
 * is ignored, and each OPX track plays its SLOT's factory patch (OWN without its patch: F1's). So a stale extension
 * can never put the wrong sound into a project.
 *
 * Written only when a track plays OPX: a project without one leaves the extension as it was (ignored at the next
 * load: its link no longer matches). A rename rewrites the project record, so the extension is re-linked. */
#define OPX_EXT_MAGIC 0x3158504Fu                   /* "OPX1" */
#define OPX_EXT_AUTOSAVE (OPX_EXT_OBJS - 1u)
typedef struct {
    uint32_t magic, link;
    uint8_t ver, ntrk, rsv[2];
    uint8_t pk[NTRK][OPX_PK_BYTES];
} opx_ext_t;
#if FELUCCA_FLASH
_Static_assert(sizeof(opx_ext_t) <= ST_PAYLOAD_MAX, "OPX extension fits one storage object");
#endif
static opx_ext_t opx_ext_buf __attribute__((section(".pool")));   /* (main loop only) */

static int opx_in_use(void)
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++)
        if (trk[tr].eng_req == ENGI_OPX)
            return 1;
    return 0;
}
/* after any restore (a project, the autosave, a backup): each track's patch by its SLOT, before an extension (if
 * one matches) puts the project's own back */
static void opx_after_restore(void)
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++) {
        int32_t s = trk[tr].p[P_E7];
        opx_load_factory(tr, s >= 0 && s < (int32_t)OPX_NFACTORY ? (uint32_t)s : 0u);
        if (trk[tr].eng_req == ENGI_OPX && s == (int32_t)OPX_OWN)
            opx_slot[tr] = (uint8_t)OPX_OWN;           /* (OWN, its patch lost: F1's sound, still OWN) */
    }
}

#if FELUCCA_FLASH
/* the tracks' patches -> extension i, tied to the project record whose CRC is link: 0 written (or nothing to write) */
static int opx_ext_save(uint32_t i, uint32_t link)
{
    opx_ext_t *e = &opx_ext_buf;
    if (!flash_ok || !opx_in_use())
        return 0;
    memset(e, 0, sizeof *e);
    e->magic = OPX_EXT_MAGIC;
    e->link = link;
    e->ver = OPX_FORMAT_VERSION;
    e->ntrk = NTRK;
    memcpy(e->pk, opx_pk, sizeof e->pk);
    return st_save(OBJ_OPX0 + i, e, sizeof *e);
}
/* extension i -> buf when it is the one of the project record with CRC link: 1 */
static int opx_ext_read(uint32_t i, uint32_t link)
{
    opx_ext_t *e = &opx_ext_buf;
    return flash_ok && st_load(OBJ_OPX0 + i, e, sizeof *e) == (int)sizeof *e && e->magic == OPX_EXT_MAGIC &&
           e->ver == OPX_FORMAT_VERSION && e->ntrk == NTRK && e->link == link;
}
/* the project record with CRC link was just restored: its OPX patches, when its extension matches (1) */
static int opx_ext_load(uint32_t i, uint32_t link)
{
    uint32_t tr;
    if (!opx_ext_read(i, link))
        return 0;
    for (tr = 0; tr < NTRK; tr++) {
        int32_t s = trk[tr].p[P_E7];
        if (!opx_pk_ok(opx_ext_buf.pk[tr]))
            continue;
        opx_set_pk(tr, opx_ext_buf.pk[tr]);
        opx_slot[tr] = (uint8_t)(s >= 0 && s <= (int32_t)OPX_OWN ? s : (int32_t)OPX_OWN);   /* (opx_poll: it stays) */
    }
    return 1;
}
/* project slot i's record changed CRC old -> new (a rename): its extension follows */
static void opx_ext_relink(uint32_t i, uint32_t old, uint32_t now)
{
    if (old != now && opx_ext_read(i, old)) {
        opx_ext_buf.link = now;
        st_save(OBJ_OPX0 + i, &opx_ext_buf, sizeof opx_ext_buf);
    }
}
#endif
