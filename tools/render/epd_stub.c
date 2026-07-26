// Host-side PBM dump of the framebuffer. The drawing primitives
// themselves are the component's own epd_fb.c, compiled directly — see
// the Makefile — so renders are pixel-exact by construction.
#include <stdio.h>
#include <stdlib.h>

#include "epd_fb.h"

void epd_stub_dump(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        exit(1);
    }
    fprintf(f, "P4\n%d %d\n", EPD_WIDTH, EPD_HEIGHT);
    for (size_t i = 0; i < sizeof(epd_fb); i++) {
        fputc(~epd_fb[i] & 0xFF, f);  // PBM 1=black, fb 0=black
    }
    fclose(f);
}
