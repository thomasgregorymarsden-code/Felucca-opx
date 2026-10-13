/* SPDX-License-Identifier: GPL-3.0-only */
/* FLASH_SCAN (cmd 120, OPX fork) through the real editor protocol path (editor_test.c is the base: sysex_byte ->
 * ed_service -> ed_handle -> the wire): the frame's shape, and that the request does not disturb the transport.
 * The base's flash reads fail, so every in-range sector reports status 2 here; the scan's own logic is
 * tests/flashscan_test.c. */
#define EDITOR_TEST_NO_MAIN 1
#include "editor_test.c"

int main(void)
{
    int bad = 0;
    uint32_t n;
    reset();
    n = request(ED_FLASH_SCAN, (const uint8_t[]){0x93 & 127, 0x93 >> 7, 2}, 3);
    bad += check("FLASH_SCAN reply: F0 7D 46 4C 78, first, count, 2 x 8 bytes, F7",
        n == 5u + 3u + 2u * 8u + 1u && host_wire[0] == 0xF0 && host_wire[1] == ED_HDR0 && host_wire[4] == 120 &&
        host_wire[5] == (0x93 & 127) && host_wire[6] == (0x93 >> 7) && host_wire[7] == 2 && host_wire[n - 1] == 0xF7);
    bad += check("FLASH_SCAN: a failed read reports status 2", host_wire[8] == 2 && host_wire[16] == 2);
    n = request(ED_FLASH_SCAN, (const uint8_t[]){0x13, 1, 0}, 3);
    bad += check("FLASH_SCAN malformed: 127 alone", n == 5u + 1u + 1u && host_wire[5] == 127);
    n = request(ED_PING, 0, 0);
    bad += check("the protocol answers as before afterwards (PING)", n == 5u + 1u + 1u && host_wire[4] == ED_PING);
    printf(bad ? "flashscan protocol: FAILED\n" : "flashscan protocol: all ok\n");
    return bad != 0;
}
