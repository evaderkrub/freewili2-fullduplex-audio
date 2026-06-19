// tests/host/vu_meter_harness.c — drive vu_meter.c from argv; print tokens.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio/vu_meter.h"

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    const char *cmd = argv[1];

    if (!strcmp(cmd, "sample")) {
        uint32_t frame = (uint32_t)strtoul(argv[2], NULL, 0);
        int slot = atoi(argv[3]);
        printf("%d\n", (int)vu_sample(frame, slot));
        return 0;
    }
    if (!strcmp(cmd, "peak")) {
        int slot = atoi(argv[2]);
        uint32_t frames[64]; uint32_t n = 0;
        for (int i = 3; i < argc && n < 64; i++)
            frames[n++] = (uint32_t)strtoul(argv[i], NULL, 0);
        printf("%u\n", (unsigned)vu_peak(frames, n, slot));
        return 0;
    }
    if (!strcmp(cmd, "bar")) {
        uint16_t peak = (uint16_t)strtoul(argv[2], NULL, 0);
        int max_px = atoi(argv[3]);
        printf("%d\n", vu_bar_px(peak, max_px));
        return 0;
    }
    if (!strcmp(cmd, "color")) {
        uint16_t peak = (uint16_t)strtoul(argv[2], NULL, 0);
        printf("%04x\n", (unsigned)vu_color_be(peak));
        return 0;
    }
    return 1;
}
