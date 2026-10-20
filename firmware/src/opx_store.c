/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX (OPX fork): the OPX patches in flash, beside the projects, the autosave and the user presets (storage.c
 * OBJ_OPX0 + i, i < OPX_EXT_OBJS; where they live: opx_map.h, docs/OPX_STORAGE.md). Since 0.2:
 *   object 0  projects 1 and 2's patches (a pair: opx_pair_t)      object 1  projects 3 and 4's
 *   object 2  OPX user presets, 8 entries (upx_obj_t)               object 3  8 more
 *   object 4  the autosave's patches (opx_ext_t, as up to 0.1.0)
 * 0.0.1 .. 0.1.0 had one object a project (opx_ext_t, tags OPX_EXT_TAG0 + i in the same sectors): see "the former
 * layout" below for how they move into the pairs.
 *
 * A project record has no room for four 434-byte patches (and its format is upstream's), so they go in an object of
 * their own, written after the project and tied to it: `link` is the CRC-32 of the project record as stored. A load
 * takes the patches only when the link matches the record it loaded; anything else (no entry, a torn object, an entry
 * left from an older project in that slot, a project restored from a backup or written by firmware without OPX)
 * is ignored, and each OPX track plays its SLOT's factory patch (OWN without its patch: F1's). So a stale entry
 * can never put the wrong sound into a project.
 *
 * Written only when a track plays OPX: a project without one leaves its entry as it was (ignored at the next load: its
 * link no longer matches). A rename rewrites the project record, so the entry is re-linked. A save of one project of
 * a pair keeps the other's entry (the object is read, the one entry changed, the object written: A/B as any other). */
#define OPX_EXT_MAGIC 0x3158504Fu                   /* "OPX1": the autosave's object (and each project's up to 0.1.0) */
#define OPX_PAIR_MAGIC 0x3250504Fu                  /* "OPP2" */
#define OPX_UPX_MAGIC 0x3155504Fu                   /* "OPU1" */
#define OPX_EXT_AUTOSAVE (OPX_EXT_OBJS - 1u)        /* (the project.c callers' index: 0..3 the projects, this one) */
#define OPX_OBJ_AUTO (OBJ_OPX0 + 4u)
#define UPX_N 8u                                    /* user preset entries an object; UPX_OBJS objects */
#define UPX_OBJS 2u
typedef struct {
    uint32_t magic, link;
    uint8_t ver, ntrk, rsv[2];
    uint8_t pk[NTRK][OPX_PK_BYTES];
} opx_ext_t;
typedef struct {                                     /* one project's entry of a pair */
    uint32_t link;
    uint8_t used, rsv[3];
    uint8_t pk[NTRK][OPX_PK_BYTES];
} opx_pent_t;
typedef struct {
    uint32_t magic;
    uint8_t ver, n, rsv[2];                          /* OPX_FORMAT_VERSION, 2 */
    opx_pent_t e[2];
} opx_pair_t;
typedef struct {                                     /* a user preset's patch: slot (0xFF: none), the record's tag */
    uint32_t tag;
    uint8_t slot, rsv[3];
    uint8_t pk[OPX_PK_BYTES], pad[2];
} upx_ent_t;
typedef struct {
    uint32_t magic;
    uint8_t ver, n, rsv[2];                          /* OPX_FORMAT_VERSION, UPX_N */
    upx_ent_t e[UPX_N];
} upx_obj_t;
_Static_assert(OPX_EXT_OBJS == 5u, "OPX objects: 2 pairs, 2 user preset objects, the autosave");
#if FELUCCA_FLASH
_Static_assert(sizeof(opx_ext_t) <= ST_PAYLOAD_MAX && sizeof(opx_pair_t) <= ST_PAYLOAD_MAX &&
               sizeof(upx_obj_t) <= ST_PAYLOAD_MAX, "OPX objects fit one storage object each");
#endif
static union {                                       /* the one buffer (main loop only) */
    opx_ext_t ext;
    opx_pair_t pair;
    upx_obj_t up;
} opx_sb __attribute__((section(".pool")));

static int opx_in_use(void)
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++)
        if (trk[tr].eng_req == ENGI_OPX)
            return 1;
    return 0;
}
/* track tr's patch by its SLOT: F n that factory patch; OWN without its patch F1's sound, SLOT still OWN */
static void opx_track_factory(uint32_t tr)
{
    int32_t s = trk[tr].p[P_E7];
    opx_load_factory(tr, s >= 0 && s < (int32_t)OPX_NFACTORY ? (uint32_t)s : 0u);
    if (trk[tr].eng_req == ENGI_OPX && s == (int32_t)OPX_OWN)
        opx_slot[tr] = (uint8_t)OPX_OWN;
}
/* track tr plays pk (a stored patch), SLOT as the sound has it (OWN, or F n with its edits) */
static void opx_track_stored(uint32_t tr, const uint8_t *pk)
{
    int32_t s = trk[tr].p[P_E7];
    opx_set_pk(tr, pk);
    opx_slot[tr] = (uint8_t)(s >= 0 && s <= (int32_t)OPX_OWN ? s : (int32_t)OPX_OWN);   /* (opx_poll: it stays) */
}
/* after any restore (a project, the autosave, a backup): each track's patch by its SLOT, before an entry (if one
 * matches) puts the project's own back */
static void opx_after_restore(void)
{
    uint32_t tr;
    for (tr = 0; tr < NTRK; tr++)
        opx_track_factory(tr);
}

#if FELUCCA_FLASH
/* ---- the former layout (0.0.1 .. 0.1.0): object i (0..3) project i's opx_ext_t, tag OPX_EXT_TAG0 + i, in the
 * sectors object i has now. Each moves into its pair before anything overwrites it: object j (0..3) is written only
 * once the former object j has no copy or its project is in pair j / 2 (opx_free). The former objects are only read */
static uint32_t opx_old_off(uint32_t j) { return OPX_EXT_LO + j * 2u * ST_SECTOR; }
/* the former object j's current copy: -1 none, else its copy (0 A, 1 B), its payload in st_buf */
static int opx_old_cur(uint32_t j)
{
    st_hdr_t h;
    int c = st_current_at(opx_old_off(j), opx_old_off(j) + ST_SECTOR, OPX_EXT_TAG0 + j, &h);
    const opx_ext_t *e = (const opx_ext_t *)st_buf;
    if (c < 0 || h.len != sizeof(opx_ext_t) || e->magic != OPX_EXT_MAGIC || e->ver != OPX_FORMAT_VERSION ||
        e->ntrk != NTRK)
        return -1;
    return c;
}
static int opx_pair_valid(uint32_t p)
{
    st_hdr_t h;
    return st_current(OBJ_OPX0 + p, &h) >= 0 && h.len == sizeof(opx_pair_t) &&
           ((const opx_pair_t *)st_buf)->magic == OPX_PAIR_MAGIC && ((const opx_pair_t *)st_buf)->n == 2u;
}
/* pair p -> opx_sb.pair: the stored one; none (or not valid): empty, with the former layout's projects 2p, 2p + 1
 * where they still are. 1 = the stored pair */
static void opx_pair_empty(void)
{
    memset(&opx_sb.pair, 0, sizeof opx_sb.pair);
    opx_sb.pair.magic = OPX_PAIR_MAGIC;
    opx_sb.pair.ver = OPX_FORMAT_VERSION;
    opx_sb.pair.n = 2;
}
static int opx_pair_get(uint32_t p)
{
    opx_pair_t *b = &opx_sb.pair;
    uint32_t k;
    if (opx_pair_valid(p)) {
        memcpy(b, st_buf, sizeof *b);
        return 1;
    }
    opx_pair_empty();
    for (k = 0; k < 2u; k++)
        if (opx_old_cur(2u * p + k) >= 0) {
            const opx_ext_t *e = (const opx_ext_t *)st_buf;
            b->e[k].link = e->link;
            b->e[k].used = 1;
            memcpy(b->e[k].pk, e->pk, sizeof b->e[k].pk);
        }
    return 0;
}
/* object j (0..3) may be written over: 0. Its sectors may hold the former object j, the only copy of project j's
 * patches while pair j / 2 does not exist yet: then that pair is written first (into the copy the former object
 * does not use now; its own sectors made free first, the same way) */
static int opx_free(uint32_t j)
{
    uint32_t p = j / 2u;
    int c;
    if (j >= 4u || opx_old_cur(j) < 0 || opx_pair_valid(p))
        return 0;
    if (p != j && opx_free(p))
        return -1;
    if (opx_pair_valid(p))
        return 0;
    c = opx_old_cur(p);
    opx_pair_get(p);
    return st_save_to(OBJ_OPX0 + p, &opx_sb.pair, sizeof opx_sb.pair, c == 0 ? 1 : 0) ? -1 : 0;
}
/* boot (after opx_ext_migrate): the former layout's projects into their pairs, once. A device without them writes
 * nothing. The number of pairs written */
static uint32_t opx_ext_convert(void)
{
    uint32_t n = (uint32_t)opx_pair_valid(0) + (uint32_t)opx_pair_valid(1);
    if (!flash_ok)
        return 0;
    opx_free(2);                                     /* (2 and 3: pair 1, after pair 0 where object 1 lies) */
    opx_free(3);
    return (uint32_t)opx_pair_valid(0) + (uint32_t)opx_pair_valid(1) - n;
}

/* ---- the projects and the autosave ---- */
/* the tracks' patches -> project i's entry (OPX_EXT_AUTOSAVE: the autosave's object), tied to the project record
 * whose CRC is link: 0 written (or nothing to write) */
static int opx_ext_save(uint32_t i, uint32_t link)
{
    if (!flash_ok || !opx_in_use())
        return 0;
    if (i == OPX_EXT_AUTOSAVE) {
        opx_ext_t *e = &opx_sb.ext;
        memset(e, 0, sizeof *e);
        e->magic = OPX_EXT_MAGIC;
        e->link = link;
        e->ver = OPX_FORMAT_VERSION;
        e->ntrk = NTRK;
        memcpy(e->pk, opx_pk, sizeof e->pk);
        return st_save(OPX_OBJ_AUTO, e, sizeof *e);
    }
    if (i >= 4u || opx_free(i / 2u))
        return -1;
    opx_pair_get(i / 2u);
    opx_sb.pair.e[i & 1u].link = link;
    opx_sb.pair.e[i & 1u].used = 1;
    memcpy(opx_sb.pair.e[i & 1u].pk, opx_pk, sizeof opx_sb.pair.e[0].pk);
    return st_save(OBJ_OPX0 + i / 2u, &opx_sb.pair, sizeof opx_sb.pair);
}
/* project i's patches (as tracks 1..4) when its entry is the one of the project record with CRC link: the patches,
 * 0 none */
static const uint8_t (*opx_ext_read(uint32_t i, uint32_t link))[OPX_PK_BYTES]
{
    if (!flash_ok)
        return 0;
    if (i == OPX_EXT_AUTOSAVE) {
        opx_ext_t *e = &opx_sb.ext;
        if (st_load(OPX_OBJ_AUTO, e, sizeof *e) == (int)sizeof *e && e->magic == OPX_EXT_MAGIC &&
            e->ver == OPX_FORMAT_VERSION && e->ntrk == NTRK && e->link == link)
            return e->pk;
        return 0;
    }
    if (i >= 4u)
        return 0;
    opx_pair_get(i / 2u);
    return opx_sb.pair.e[i & 1u].used && opx_sb.pair.e[i & 1u].link == link ? opx_sb.pair.e[i & 1u].pk : 0;
}
/* the project record with CRC link was just restored: its OPX patches, when its entry matches (1) */
static int opx_ext_load(uint32_t i, uint32_t link)
{
    const uint8_t (*pk)[OPX_PK_BYTES] = opx_ext_read(i, link);
    uint32_t tr;
    if (!pk)
        return 0;
    for (tr = 0; tr < NTRK; tr++)
        if (opx_pk_ok(pk[tr]))
            opx_track_stored(tr, pk[tr]);
    return 1;
}
/* project slot i's record changed CRC old -> new (a rename): its entry follows */
static void opx_ext_relink(uint32_t i, uint32_t old, uint32_t now)
{
    if (old == now || i >= 4u || !opx_ext_read(i, old) || opx_free(i / 2u))
        return;
    opx_pair_get(i / 2u);                            /* (opx_free may have used the buffer) */
    opx_sb.pair.e[i & 1u].link = now;
    st_save(OBJ_OPX0 + i / 2u, &opx_sb.pair, sizeof opx_sb.pair);
}

/* ---- the user presets: an OPX user preset carries its track's patch, as FM6's do (up_fm6.c). An entry belongs to
 * user preset slot k while that record is an OPX sound and still has the tag upf_tag gave it (the record without its
 * name: a rename keeps the patch; an erase, another sound saved there, an UP_PUT from an editor that sends no patch
 * leave the record without one, and the entry free). The record is written first, its entry second (up_store): a
 * power cut between them leaves the record without a patch (its SLOT's factory one), never another record's. 16
 * entries: a 17th OPX user preset is refused before anything is written (upx_room) */
static int upx_live(const upx_ent_t *e)
{
    return e->slot < UP_SLOTS && up_used(e->slot) && up_rec(e->slot)->engine == ENGI_OPX &&
           e->tag == upf_tag(up_rec(e->slot));
}
static void upx_empty(void)
{
    upx_obj_t *u = &opx_sb.up;
    uint32_t n;
    memset(u, 0, sizeof *u);
    u->magic = OPX_UPX_MAGIC;
    u->ver = OPX_FORMAT_VERSION;
    u->n = UPX_N;
    for (n = 0; n < UPX_N; n++)
        u->e[n].slot = 0xFFu;
}
/* user preset object j -> opx_sb.up (empty when none): 1 the stored one */
static int upx_get(uint32_t j)
{
    upx_obj_t *u = &opx_sb.up;
    if (st_load(OBJ_OPX0 + 2u + j, u, sizeof *u) == (int)sizeof *u && u->magic == OPX_UPX_MAGIC &&
        u->ver == OPX_FORMAT_VERSION && u->n == UPX_N)
        return 1;
    upx_empty();
    return 0;
}
/* where slot k's patch goes: object j, entry n (its own entry, else a free one): 0, -1 none (all 16 taken) */
static int upx_place(uint32_t k, uint32_t *j, uint32_t *n)
{
    uint32_t pass;
    for (pass = 0; pass < 2u; pass++)                /* (slot k's own entry first, live or not; then a free one) */
        for (*j = 0; *j < UPX_OBJS; (*j)++) {
            if (!flash_ok)
                return -1;
            upx_get(*j);
            for (*n = 0; *n < UPX_N; (*n)++) {
                const upx_ent_t *e = &opx_sb.up.e[*n];
                if (pass ? !upx_live(e) : e->slot == k)
                    return 0;
            }
        }
    return -1;
}
/* slot k (UP_SLOTS: a new record) can take an OPX patch: 1 (no flash: 1, the record is RAM only) */
static int upx_room(uint32_t k)
{
    uint32_t j, n;
    return !flash_ok || upx_place(k, &j, &n) == 0;
}
/* up_store: slot k (its record just written from track tr) gets that track's patch: 0 ok, 2 flash error, 3 no flash,
 * 4 no room */
static int upx_store(uint32_t k, uint32_t tr)
{
    uint32_t j, n, m;
    upx_ent_t *e;
    if (!flash_ok)
        return 3;
    if (upx_place(k, &j, &n))
        return 4;
    if (opx_free(2u + j))
        return 2;
    upx_get(j);                                      /* (opx_free may have used the buffer) */
    for (m = 0; m < UPX_N; m++)                      /* (no second entry for k: an older one of k elsewhere stays */
        if (m != n && opx_sb.up.e[m].slot == k)      /* dead, its tag the old record's) */
            opx_sb.up.e[m].slot = 0xFFu;
    e = &opx_sb.up.e[n];
    e->slot = (uint8_t)k;
    e->tag = upf_tag(up_rec(k));
    memcpy(e->pk, opx_pk[tr % NTRK], OPX_PK_BYTES);
    return st_save(OBJ_OPX0 + 2u + j, &opx_sb.up, sizeof opx_sb.up) ? 2 : 0;
}
/* slot k's patch: its stored bytes (in opx_sb), 0 none */
static const uint8_t *upx_find(uint32_t k)
{
    uint32_t j, n;
    if (!flash_ok)
        return 0;
    for (j = 0; j < UPX_OBJS; j++) {
        upx_get(j);
        for (n = 0; n < UPX_N; n++)
            if (opx_sb.up.e[n].slot == k && upx_live(&opx_sb.up.e[n]) && opx_pk_ok(opx_sb.up.e[n].pk))
                return opx_sb.up.e[n].pk;
    }
    return 0;
}
#else
static uint32_t opx_ext_convert(void) { return 0; }
static int upx_room(uint32_t k) { (void)k; return 1; }
static int upx_store(uint32_t k, uint32_t tr) { (void)k; (void)tr; return 3; }
static const uint8_t *upx_find(uint32_t k) { (void)k; return 0; }
#endif

/* up_load: track t (its values just loaded from slot k) gets the preset's own patch; without one its SLOT's factory
 * patch (as up to 0.1.0, where no OPX user preset kept its patch) */
static void upx_track_load(track_t *t, uint32_t k)
{
    uint32_t tr = (uint32_t)(t - trk);
    const uint8_t *pk;
    if (tr >= NTRK || t->eng_req != ENGI_OPX)
        return;
    if ((pk = upx_find(k)) != 0)
        opx_track_stored(tr, pk);
    else
        opx_track_factory(tr);
}
