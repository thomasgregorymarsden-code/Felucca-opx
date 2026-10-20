# OPX storage: where the OPX engine keeps its data, and how to move it

The OPX engine needs more data than Felucca's records can hold: four OPX patches of up to 434 bytes per project,
and one per OPX user preset. It keeps them in five storage objects of its own, each with an A and a B copy, in Felucca's
own A/B storage (`storage.c`): erase, write the payload, then write the 32-byte commit record last, with CRCs on both.
A save interrupted at any point leaves the previous copy in use.

## Where it is now

| | |
|---|---|
| Region | `0xD2000`–`0xDBFFF`, 10 sectors of 4 KB: 5 objects, each with an A and a B copy |
| Taken from | the upper half of user sample slot 3. Slot 3 keeps `0xC8000`–`0xD1FFF` (40 KB); slots 1 and 2 keep 80 KB |
| Defined in | `firmware/src/opx_map.h` only (`OPX_EXT_LO`, `OPX_EXT_OBJS`) |
| Flash survey | 2026-10-13 (`web/opx/flashscan.html`): all 10 sectors erased. The unmapped block `0xEA000`–`0xFBFFF` held stock data in 17 of 18 sectors, so it was not used |

## What the objects hold (since 0.2)

| Object | Sectors | Holds | Commit record type |
|---|---|---|---|
| 0 | 1–2 | projects 1 and 2's patches (`opx_pair_t`: per project the record's CRC as `link`, its 4 patches) | `0x4F58` |
| 1 | 3–4 | projects 3 and 4's | `0x4F59` |
| 2 | 5–6 | 8 OPX user presets' patches (`upx_obj_t`: per entry the slot, a tag of its record, the patch) | `0x4F5A` |
| 3 | 7–8 | 8 more | `0x4F5B` |
| 4 | 9–10 | the autosave's patches (`opx_ext_t`, as in 0.1.0) | `0x4F54` |

A project's entry counts only while its `link` equals the CRC of the project record it is loaded with; a user preset's
entry only while its record is an OPX sound with the same tag (`upf_tag`, the record without its name, as FM6's user
preset patches use). A stale entry is never applied: the track plays its SLOT's factory patch. Saving one project of a
pair reads the pair, changes its entry and writes the pair. With 16 OPX user presets saved, a 17th is refused before
its record is written.

**From 0.0.1–0.1.0:** objects 0–3 held one project each (`opx_ext_t`, types `0x4F50`–`0x4F53`, in the same sectors).
Their contents move into the two pairs once, at the first boot of 0.2 (`opx_ext_convert`, `opx_store.c`), and lazily
before any write if that boot could not: object j is never written while 0.1.0's object j there is still the only copy
of its project's patches (`opx_free`). Pair 0 goes into the copy of object 0 that 0.1.0's project 1 does not use now; pair 1
only once pair 0 holds project 2 (whose 0.1.0 object it overwrites). A power cut at any erase or program loses nothing
(`tests/persistence_test.c`). New types for the new contents mean a 0.1.0 object is never read as a 0.2 one; installing
0.1.0 again after the move finds no objects of its types for projects 1–4 (their OPX tracks play the SLOT's factory
patch).

**Backup:** the editor's full backup (`editor_backup.c`) lists objects 0–3 as ids 10–13, as stored, and the tracks'
current patches as id 14 (restored after the live music, id 0). Archives without them (Felucca, Felucca-OPX 0.1.0) still
restore; firmware without them skips them (rc 1 at begin).

## Precautions so the region can be moved later

These are for the case where upstream Felucca takes the OPX engine but wants its data somewhere else.

1. **One place in the source.** `opx_map.h` is the only file that names the region. Everything else is derived from it:
   - `storage.c`'s sector map;
   - which sample slot gives up room, and how much (`opx_smp_slot_size`);
   - SLICE's slice record, which sits in the last sector of what the slot keeps;
   - the editor's upload limit and erase range for that slot (`SMP_SLOT_SIZE`);
   - the size the editor reports per slot (`SMP_INFO`).

   If the region moves out of slot 3, slot 3 gets its 80 KB back without any other edit. Compile-time checks reject a region that would overlap the projects, the FM6 patches or the preset banks, leave a sample slot under 8 KB, or span two slots.
2. **Fixed on-flash identity.** Each object's commit record carries a fixed type tag (table above, `OPX_OBJ_TAG`). The tag is not the object's position in `storage.c`'s enum, so adding upstream storage objects later does not change what is already on a device's flash. The upstream objects' commit records are unchanged.
3. **Built-in migration.** At boot, before anything else uses the flash map, `opx_ext_migrate()` checks each extension:
   - If the extension has no valid copy at the current location, the firmware looks at each former location listed in `OPX_EXT_LEGACY`.
   - It copies the first valid copy it finds (both CRCs checked), keeping its sequence number.
   - The former location is only read, never written; whatever takes it over owns it.
   - A device that already has valid data at the new location is left alone, and a corrupt former copy is not copied.

   Today the list is empty, so this does nothing.
4. **Can be built without it.** `FELUCCA_OPX=0` builds with no OPX objects and an 80 KB slot 3, and the upstream storage test passes both ways.
5. **Tests.** `tests/opx_store_test.c` simulates a move. It checks that data saved at one location and found only at a former one is copied, that the former location is not written, that a second boot copies nothing, and that the next save continues the sequence.

## How to move it (for a maintainer)

1. In `opx_map.h`, set `OPX_EXT_LO` to the new location.
2. Add the old value to `OPX_EXT_LEGACY`, newest first, e.g. `#define OPX_EXT_LEGACY 0xD2000u`.
3. Build and run `tests/run_tests.sh`. The compile-time checks catch overlaps.

The one thing a move cannot protect against is the old location being overwritten before the new firmware's first boot. For example, a user might install a firmware without OPX that gives slot 3 its full 80 KB back, then upload a long sample into slot 3. That loses OPX patches only; the device still works. The editor's backup archive holds them (ids 10–13), so users can export them first; an archive does not depend on where the data lives in flash.

## Engine number

The same concern applies to the engine number, which projects and user presets store. OPX is **engine 15**, and engine 14 is reserved: never selectable, silent, no presets. That is the same treatment as DIGITAL's retired number 1.

The reason is that upstream keeps the MIDI LEARN map in the favourites row of engine 14 (`favorites.factory[14]`, `midi_learn.c`), and a compile-time check stops any engine from taking 14. Using 15 leaves that map where it is, so a user's learned CCs survive installing this fork. It also leaves 14 free for upstream's own next engine, if upstream ever moves the map. If upstream later wants OPX under another number, stored sounds would need a conversion at load, like DIGITAL's sounds becoming FM6 (`fm4_convert.c`).

## What this costs users of this fork

- User sample slot 3 holds 40 KB instead of 80 KB, about 3.7 s at the editor's 22.05 kHz.
- After installing this fork, a sample already in slot 3 that is longer than that is rejected as invalid and must be uploaded again, shorter or into slot 1 or 2. This is deliberate: the region would overwrite its tail.
- SLICE slices already saved for a slot 3 sample are looked for in the new last sector of the slot, so they are lost once. The sample itself is kept if it fits.
- A project, user preset or autosave with an OPX track stores engine 15, which Felucca and the official firmware do not have: they treat that record as empty (`proj_engines_ok`, `up_valid`) and leave it in flash. Records without an OPX track are byte-for-byte Felucca 1.5.1's.
