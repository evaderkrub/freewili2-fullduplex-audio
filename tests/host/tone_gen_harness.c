#include <stdio.h>
#include <string.h>
#include "audio/tone_gen.h"
int main(int argc, char** argv) {
    const char* what = argc > 1 ? argv[1] : "peak";
    int16_t buf[65]; float ph = 0.0f;
    tone_gen_fill(buf, 65, 1000.0f, 16000.0f, &ph);  // 64 + 1 to check wrap
    if (!strcmp(what, "peak")) {
        int pk = 0; for (int i=0;i<64;i++){ int a = buf[i]<0?-buf[i]:buf[i]; if(a>pk)pk=a; }
        printf("%d\n", pk);
    } else if (!strcmp(what, "zc")) {
        int zc = 0; for (int i=1;i<64;i++) if ((buf[i-1]<0)!=(buf[i]<0)) zc++;
        printf("%d\n", zc);
    } else { // wrap: sample 64 should match sample 0 within int16 rounding
        int d = buf[64] - buf[0]; if (d<0) d=-d;
        printf("%s\n", d <= 2 ? "OK" : "BAD");
    }
    return 0;
}
