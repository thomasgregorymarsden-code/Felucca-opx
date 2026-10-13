# OPX storage: where the OPX engine keeps its data, and how to move it

The OPX engine needs more per-project data than a Felucca project record can hold: four OPX patches of up to 434 bytes each. So each project gets a second storage object, its **extension**. There are five extensions: one for each of the four projects, and one for the autosave.

## Where it is now

| | |
|---|---|
| Region | `0xD2000`–`0xDBFFF`, 10 sectors of 4 KB: 5 objects, each with an A and a B copy |
| Taken from | the upper half of user sample slot 3. Slot 3 keeps `0xC8000`–`0xD1FFF` (40 KB); slots 1 and 2 keep 80 KB |
| Defined in | `firmware/src/opx_map.h` only (`OPX_EXT_LO`, `OPX_EXT_OBJS`) |
| Flash survey | 2026-10-13 (`web/opx/flashscan.html`): all 10 sectors erased. The unmapped block `0xEA000`–`0xFBFFF` held stock data in 17 of 18 sectors, so it was not used |

Each extension uses Felucca's own A/B storage (`storage.c`): erase, write the payload, then write the 32-byte commit record last, with CRCs on both. A save interrupted at any point leaves the previous copy in use.

## Precautions so the region can be moved later

These are for the case where upstream Felucca takes the OPX engine but wants its data somewhere else.

1. **One place in the source.** `opx_map.h` is the only file that names the region. Everything else is derived from it:
   - `storage.c`'s sector map;
   - which sample slot gives up room, and how much (`opx_smp_slot_size`);
   - SLICE's slice record, which sits in the last sector of what the slot keeps;
   - the editor's upload limit and erase range for that slot (`SMP_SLOT_SIZE`);
   - the size the editor reports per slot (`SMP_INFO`).

   If the region moves out of slot 3, slot 3 gets its 80 KB back without any other edit. Compile-time checks reject a region that would overlap the projects, the FM6 patches or the preset banks, leave a sample slot under 8 KB, or span two slots.
2. **Fixed on-flash identity.** Each extension's commit record carries a fixed type tag (`0x4F50 + i`). The tag is not the object's position in `storage.c`'s enum, so adding upstream storage objects later does not change what is already on a device's flash. The upstream objects' commit records are unchanged.
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

The one thing a move cannot protect against is the old location being overwritten before the new firmware's first boot. For example, a user might install a firmware without OPX that gives slot 3 its full 80 KB back, then upload a long sample into slot 3. That loses OPX patches only; the device still works. A later milestone adds the extensions to the editor's backup archive, so users can export them first; an archive does not depend on where the data lives in flash.

## What this costs users of this fork

- User sample slot 3 holds 40 KB instead of 80 KB, about 3.7 s at the editor's 22.05 kHz.
- After installing this fork, a sample already in slot 3 that is longer than that is rejected as invalid and must be uploaded again, shorter or into slot 1 or 2. This is deliberate: the region would overwrite its tail.
- SLICE slices already saved for a slot 3 sample are looked for in the new last sector of the slot, so they are lost once. The sample itself is kept if it fits.
