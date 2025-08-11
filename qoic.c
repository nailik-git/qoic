#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct pixel {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

struct pixel array[64] = {0};

_Bool peq(struct pixel a, struct pixel b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

_Bool pdiff(int8_t dr, int8_t dg, int8_t db) {
  return -2 <= dr && dr <= 1
      && -2 <= dg && dg <= 1
      && -2 <= db && db <= 1;
}

_Bool pldiff(int8_t dr_dg, int8_t dg, int8_t db_dg) {
  return - 8 <= dr_dg && dr_dg <=  7
      && -32 <=    dg &&    dg <= 31
      && - 8 <= db_dg && db_dg <=  7;
}

uint8_t hash(struct pixel p) {
  return (p.r * 3 + p.g * 5 + p.b * 7 + p.a * 11) % 64;
}

void encode(char* infile, char* outfile, uint32_t width, uint32_t height, uint8_t channels, uint8_t colorspace) {
  FILE* output = fopen(outfile, "w");

  uint8_t width_parts[4];
  for(int i = 0; i < 4; i++) {
    width_parts[i] = width >> 8 * (3 - i);
  }
  uint8_t height_parts[4];
  for(int i = 0; i < 4; i++) {
    height_parts[i] = height >> 8 * (3 - i);
  }

  fwrite("qoif", 1, 4, output);
  fwrite(width_parts, 1, 4, output);
  fwrite(height_parts, 1, 4, output);
  fwrite(&channels, 1, 1, output);
  fwrite(&colorspace, 1, 1, output);

  struct pixel pp = {.a = 255};
  struct pixel cp;

  FILE* file = fopen(infile, "r");

  uint8_t ph;
  int8_t dr, dg, db, dr_dg, db_dg;
  while(!feof(file)) {
    fread(&cp, sizeof(uint8_t), channels, file);
    if(peq(cp, pp)) { // qoi_run
      _Bool b = 0;
      uint8_t i = 1;

      for(; i < 62; i++) {
        fread(&cp, sizeof(uint8_t), channels, file);
        if(!peq(cp, pp)) {b = 1; break;}
        if(feof(file)) break;
      }

      uint8_t run = (0b11 << 6) | (i - 1);

      fwrite(&run, 1, 1, output);

      if(!b) continue;
    }
    if(peq(array[ph = hash(cp)], cp)) { // qoi_index
      fwrite(&ph, 1, 1, output);
    } else if(cp.a != pp.a) { // qoi_rgba
      uint8_t rgba = -1;
      fwrite(&rgba, 1, 1, output);
      fwrite(&cp, 1, 4, output);
    } else if(pdiff(dr = cp.r - pp.r, dg = cp.g - pp.g, db = cp.b - pp.b)) { // qoi_diff
      uint8_t diff = (0b01 << 6) | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2);
      fwrite(&diff, 1, 1, output);
    } else if(pldiff(dr_dg = dr - dg, dg, db_dg = db - dg)) { // qoi_diff_luma
      uint8_t luma[2] = {(0b10 << 6) | (dg + 32), (dr_dg + 8) << 4 | (db_dg + 8)};
      fwrite(luma, 1, 2, output);
    } else { // qoi_rgb
      uint8_t rgb = -2;
      fwrite(&rgb, 1, 1, output);
      fwrite(&cp, 1, 3, output);
    }
    array[ph] = cp;
    pp = cp;
  }

  fwrite("\0\0\0\0\0\0\0\1", 1, 8, output);
}

void usage() {
  printf("./qoic [options] infile outfile\n\n");
  printf("options:\n");
  printf("  -c 3/4       set channels (3 = rgb / 4 = rgba)\n");
  printf("                            (optional, default=3)\n");
  printf("  -h height    set height (needed if infile is raw)\n");
  printf("  -s 0/1       set colorspace (0 = sRGB linear alpha)\n");
  printf("                               1 = all channels linear)");
  printf("                              (optional, default=0)\n");
  printf("  -w width     set width (needed if infile is raw)\n");
  printf("  -?/--help    show this message\n");
}

int main(int argc, char** argv) {
  uint32_t width = 0, height = 0;
  uint32_t channels = 3, colorspace = 0;
  int i;
  for(i = 1; i < argc; i++) {
    if(argv[i][0] != '-') break;
    if(strcmp("-?", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
      usage();
      return 0;
    }
    if(strcmp("-w", argv[i]) == 0) {
      if(++i >= argc) goto fail;
      int ret = sscanf(argv[i], "%d", &width);
      if(ret != 1) goto fail;
    }
    if(strcmp("-h", argv[i]) == 0) {
      if(++i >= argc) goto fail;
      int ret = sscanf(argv[i], "%d", &height);
      if(ret != 1) goto fail;
    }
    if(strcmp("-c", argv[i]) == 0) {
      if(++i >= argc) goto fail;
      int ret = sscanf(argv[i], "%d", &channels);
      if(ret != 1) goto fail;
    }
    if(strcmp("-s", argv[i]) == 0) {
      if(++i >= argc) goto fail;
      int ret = sscanf(argv[i], "%d", &colorspace);
      if(ret != 1) goto fail;
    }
  }

  if(i + 1 >= argc || 
    width == 0 || height == 0 ||
   !(channels == 3 || channels == 4) ||
   !(colorspace == 0 || colorspace == 1)) goto fail;

  encode(argv[i], argv[i + 1], width, height, channels, colorspace);
  return 0;

  fail:
  printf("%d, %d, %d, %d\n\n", width, height, channels, colorspace);
  usage();
  return 1;
}
