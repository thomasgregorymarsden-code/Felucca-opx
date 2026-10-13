/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX fork: where the OPX engine's data lives in flash. This header is the ONLY place the location is written down;
 * everything else (storage.c's sector map, the user sample slot that gives the room up, SLICE's record in that slot,
 * the editor's sample upload limits, the tests) is derived from these two numbers.
 *
 * Today: the upper half of user sample slot 3, 0xD2000..0xDBFFF (10 sectors; flash survey of 2026-10-13, all erased,
 * web/opx/flashscan.html). Slot 3 keeps 0xC8000..0xD1FFF (40 KiB). See docs/OPX_STORAGE.md.
 *
 * To move the data (e.g. when merging into upstream Felucca):
 *   1. change OPX_EXT_LO (and OPX_EXT_HI if the size changes): the slot that gave the room up gets it back by itself;
 *   2. add the OLD OPX_EXT_LO to OPX_EXT_LEGACY: the first boot of the new firmware finds no valid copy at the new
 *      place, looks there, and copies what it finds (commit record and payload CRCs checked) to the new place;
 *   3. nothing else: the on-flash identity of each object is a fixed tag (storage.c st_tag), not an enum position.
 * FELUCCA_OPX=0 builds without any of it: slot 3 is 80 KiB again and no OPX object exists. */
#ifndef OPX_MAP_H
#define OPX_MAP_H
#include <stdint.h>

#ifndef FELUCCA_OPX
#define FELUCCA_OPX 1
#endif

#define OPX_EXT_OBJS 5u                      /* one extension per project (4) and one for the autosave */
#define OPX_EXT_LO 0x000D2000u
#define OPX_EXT_HI (OPX_EXT_LO + OPX_EXT_OBJS * 2u * 0x1000u)   /* exclusive; A/B per object */
#define OPX_EXT_TAG0 0x4F50u                  /* "PO" little-endian: commit record type of extension 0 (+ index) */

/* former OPX_EXT_LO values, newest first (a comma-separated list; empty: nothing to migrate from) */
#ifndef OPX_EXT_LEGACY
#define OPX_EXT_LEGACY
#endif

/* the user sample slots (eng_sample.c) the region may sit in */
#define OPX_SMP_BASE 0xA0000u
#define OPX_SMP_SIZE 0x14000u
#define OPX_SMP_SLOTS 3u

_Static_assert((OPX_EXT_LO & 0xFFFu) == 0u, "OPX region: sector aligned");
_Static_assert(OPX_EXT_LO >= 0x97000u && OPX_EXT_HI <= 0xE0000u, "OPX region: inside Felucca's data store (FL_DATA)");
_Static_assert(OPX_EXT_HI <= 0x97000u || OPX_EXT_LO >= 0x9F000u, "OPX region: overlaps the projects");
_Static_assert(OPX_EXT_HI <= 0x9F000u || OPX_EXT_LO >= 0xA0000u, "OPX region: overlaps FM6 patches copy A");
_Static_assert(OPX_EXT_HI <= 0xDC000u || OPX_EXT_LO >= 0xE0000u, "OPX region: overlaps the user preset banks");

/* bytes of user sample slot k left to samples: a slot the region starts in ends where the region starts; the region
 * must not cut a slot from below (a slot starts at its header) */
static inline uint32_t opx_smp_slot_size(uint32_t k)
{
#if FELUCCA_OPX
    uint32_t lo = OPX_SMP_BASE + k * OPX_SMP_SIZE, hi = lo + OPX_SMP_SIZE;
    if (OPX_EXT_LO > lo && OPX_EXT_LO < hi)
        return OPX_EXT_LO - lo;
#else
    (void)k;
#endif
    return OPX_SMP_SIZE;
}
#if FELUCCA_OPX
/* no slot may lose its start (header) to the region, and the region may not span past the end of a slot into the next */
_Static_assert(OPX_EXT_LO < OPX_SMP_BASE || OPX_EXT_LO >= OPX_SMP_BASE + OPX_SMP_SLOTS * OPX_SMP_SIZE ||
               (OPX_EXT_LO - OPX_SMP_BASE) % OPX_SMP_SIZE >= 0x2000u, "OPX region: leaves a sample slot < 8 KiB");
_Static_assert(OPX_EXT_HI <= OPX_SMP_BASE || OPX_EXT_LO >= OPX_SMP_BASE + OPX_SMP_SLOTS * OPX_SMP_SIZE ||
               (OPX_EXT_HI - 1u - OPX_SMP_BASE) / OPX_SMP_SIZE == (OPX_EXT_LO - OPX_SMP_BASE) / OPX_SMP_SIZE,
               "OPX region: spans two sample slots");
_Static_assert(OPX_EXT_LO < OPX_SMP_BASE || OPX_EXT_LO >= OPX_SMP_BASE + OPX_SMP_SLOTS * OPX_SMP_SIZE ||
               OPX_EXT_HI == OPX_SMP_BASE + ((OPX_EXT_LO - OPX_SMP_BASE) / OPX_SMP_SIZE + 1u) * OPX_SMP_SIZE,
               "OPX region: must reach the end of its sample slot (the slot keeps one contiguous part)");
#endif
#endif
