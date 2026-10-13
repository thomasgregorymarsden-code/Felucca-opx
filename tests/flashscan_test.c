/* SPDX-License-Identifier: GPL-3.0-only */
/* FLASH_SCAN (firmware/src/editor_flashscan.c, OPX fork) against a simulated 1 MiB NOR:
 *   erased sectors report status 0, 0 bytes used and the CRC of 4096 x 0xFF;
 *   a sector with data reports status 1, the exact count of non-0xFF bytes and its zlib CRC-32;
 *   sectors outside 0x93..0xFF, or with no flash, report status 2 and are never read;
 *   a malformed request (count 0, count > FS_MAX, wrong length) gets 127 alone;
 *   the scan never writes: the NOR is unchanged afterwards.
 * Built with tests/run_tests.sh (cc only; no build/ needed). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t nor[0x100000], nor_copy[0x100000];
static uint32_t reads_outside;                                    /* reads below 0x93000: must stay 0 */
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off < 0x93000u)
        reads_outside++;
    if (off + n > sizeof nor)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static uint32_t st_crc32_add(uint32_t c, const void *p, uint32_t n)   /* as storage.c */
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
static uint8_t ed_smp_buf[512];
static uint8_t out[1024];
static uint32_t on;
static void ed_b(uint32_t v) { out[on++] = (uint8_t)(v & 0x7Fu); }

#include "../firmware/src/editor_flashscan.c"

static uint32_t ref_crc(const uint8_t *p, uint32_t n)             /* bitwise zlib CRC-32, independent of the table */
{
    uint32_t c = 0xFFFFFFFFu, i, k;
    for (i = 0; i < n; i++) {
        c ^= p[i];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
static uint32_t scan(uint32_t first, uint32_t count)
{
    uint8_t a[3] = {(uint8_t)(first & 127u), (uint8_t)(first >> 7), (uint8_t)count};
    on = 0;
    return (uint32_t)ed_flashscan_handle(ED_FLASH_SCAN, a, 3);
}
static uint32_t rec_crc(const uint8_t *r) { uint32_t c = 0, k; for (k = 0; k < 5; k++) c |= (uint32_t)r[3 + k] << (7 * k); return c; }
static uint32_t rec_used(const uint8_t *r) { return r[1] | (uint32_t)r[2] << 7; }

static int check(const char *what, int ok)
{
    printf("flashscan: %-70s %s\n", what, ok ? "ok" : "FAIL");
    return !ok;
}

int main(void)
{
    int bad = 0;
    uint32_t i, used;
    uint8_t ff[4096];
    const uint8_t *r;
    memset(nor, 0xFF, sizeof nor);
    memset(ff, 0xFF, sizeof ff);
    for (i = 0; i < 4096; i += 3)                                 /* sector 0x95: every third byte 0x00 */
        nor[0x95000u + i] = 0x00;
    nor[0xFFFFFu] = 0x12;                                         /* the last byte of the part */
    for (i = 0; i < 0x93000u; i++)                                /* the app area: data the scan must not read */
        nor[i] = (uint8_t)i;
    memcpy(nor_copy, nor, sizeof nor);

    bad += check("a request is handled", scan(0x93, 4) == 1);
    bad += check("reply header: first sector, count", on == 3u + 4u * 8u && out[0] == (0x93 & 127) &&
        out[1] == (0x93 >> 7) && out[2] == 4);
    r = out + 3;
    bad += check("erased sector: status 0, 0 used, the CRC of 4096 x 0xFF",
        r[0] == 0 && rec_used(r) == 0 && rec_crc(r) == ref_crc(ff, 4096));
    r = out + 3 + 2 * 8;
    used = 0;
    for (i = 0; i < 4096; i++)
        used += nor[0x95000u + i] != 0xFF;
    bad += check("sector with data: status 1, the exact count of non-0xFF bytes",
        r[0] == 1 && rec_used(r) == used && used == 1366u);
    bad += check("sector with data: its zlib CRC-32", rec_crc(r) == ref_crc(nor + 0x95000u, 4096));

    scan(0xFF, 1);
    r = out + 3;
    bad += check("the last sector of the part: one byte used", r[0] == 1 && rec_used(r) == 1);
    scan(0xFE, 4);
    bad += check("past the end of the part: status 2, no CRC", on == 3u + 4u * 8u && out[3 + 8] == 1 &&
        out[3 + 16] == 2 && rec_used(out + 3 + 16) == 0 && rec_crc(out + 3 + 16) == 0);
    scan(0x10, 2);
    bad += check("below 0x93000 (the app): status 2, never read", out[3] == 2 && out[3 + 8] == 2 && !reads_outside);
    flash_ok = 0;
    scan(0x95, 1);
    bad += check("no flash: status 2", out[3] == 2);
    flash_ok = 1;

    on = 0;
    ed_flashscan_handle(ED_FLASH_SCAN, (const uint8_t[]){0x13, 1, 0}, 3);
    bad += check("count 0: 127 alone", on == 1 && out[0] == 127);
    on = 0;
    ed_flashscan_handle(ED_FLASH_SCAN, (const uint8_t[]){0x13, 1, FS_MAX + 1u}, 3);
    bad += check("count above FS_MAX: 127 alone", on == 1 && out[0] == 127);
    on = 0;
    ed_flashscan_handle(ED_FLASH_SCAN, (const uint8_t[]){0x13, 1}, 2);
    bad += check("short request: 127 alone", on == 1 && out[0] == 127);
    bad += check("another command is not taken", ed_flashscan_handle(78, (const uint8_t[]){0}, 1) == 0);

    for (i = 0x93; i <= 0xFF; i += FS_MAX)                        /* the whole range, as the page scans it */
        scan(i, 0xFFu - i + 1u < FS_MAX ? 0xFFu - i + 1u : FS_MAX);
    bad += check("a full scan writes nothing", !memcmp(nor, nor_copy, sizeof nor));

    printf(bad ? "flashscan: FAILED\n" : "flashscan: all ok\n");
    return bad != 0;
}
