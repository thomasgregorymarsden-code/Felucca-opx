/* SPDX-License-Identifier: GPL-3.0-only */
/* OPX fork: FLASH_SCAN, a read-only survey of the flash above the app (0x93000..0xFFFFF), to find sectors no one uses
 * before the OPX project extensions take any (web/opx/flashscan.html shows the map). Nothing here erases or writes.
 *   request  F0 7D 46 4C 78  first_lo first_hi  count  F7
 *            first: the absolute 4 KiB sector number (0x93..0xFF; offset = first x 4096), two 7-bit bytes, LSB first;
 *            count: 1..FS_MAX sectors
 *   reply    F0 7D 46 4C 78  first_lo first_hi  count  then per sector:
 *            status (0 erased: every byte 0xFF, 1 holds data, 2 not read: outside the range or no flash),
 *            used_lo used_hi (bytes that are not 0xFF, 0..4096, two 7-bit bytes),
 *            crc (the sector's zlib CRC-32, five 7-bit bytes, LSB first)  F7
 * The command number is far above the upstream protocol's (1..79) so a later Felucca command does not collide with it.
 * Each sector is read in st_read's 256-byte IRQ-off windows; FS_MAX keeps one request near 16 KiB of reads, so the
 * main loop is not held for long. */
enum { ED_FLASH_SCAN = 120 };
#define FS_LO 0x93u                                 /* first sector the plain XIP window covers (fl_plain_window_init) */
#define FS_HI 0xFFu                                 /* last sector of the 1 MiB part */
#define FS_MAX 4u

static void ed_fs_sector(uint32_t s)
{
    uint32_t off, k, used = 0, crc = 0xFFFFFFFFu, st = 0;
    uint8_t *b = ed_smp_buf;                        /* (a sample transfer never runs at the same time) */
    if (s < FS_LO || s > FS_HI || !flash_ok) {
        st = 2;
    } else {
        for (off = s * 4096u; off < (s + 1u) * 4096u; off += 256u) {
            if (st_read(off, b, 256u)) {
                st = 2;
                break;
            }
            for (k = 0; k < 256u; k++)
                used += b[k] != 0xFFu;
            crc = st_crc32_add(crc, b, 256u);
        }
        if (st != 2)
            st = used ? 1u : 0u;
    }
    if (st == 2)
        used = 0, crc = 0;
    else
        crc = ~crc;
    ed_b(st);
    ed_b(used);
    ed_b(used >> 7);
    for (k = 0; k < 5u; k++)
        ed_b(crc >> (7u * k));
}

static int ed_flashscan_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint32_t first, count, i;
    if (cmd != ED_FLASH_SCAN)
        return 0;
    if (n != 3u || !a[2] || a[2] > FS_MAX) {
        ed_b(127);                                  /* malformed: 127 alone */
        return 1;
    }
    first = a[0] | (uint32_t)a[1] << 7;
    count = a[2];
    ed_b(first);
    ed_b(first >> 7);
    ed_b(count);
    for (i = 0; i < count; i++)
        ed_fs_sector(first + i);
    return 1;
}
