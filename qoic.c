#include <assert.h>
#include <png.h>
#include <pngconf.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

typedef struct { // raw image data
  uint32_t width;
  uint32_t height;
  uint8_t channels;
  uint8_t colorspace;
  uint8_t** data;
} image;

typedef struct {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
} pixel;

pixel array[64] = {0};

void write_raw(image img, char* outfile) {
  FILE* output = fopen(outfile, "w");
  if(!output) {
    fprintf(stderr, "ERROR: Couldn't open file \"%s\"\n", outfile);
  }

  for(int i = 0; i < img.height; i++) {
    fwrite(img.data[i], 1, img.width * img.channels, output);
  }

  fclose(output);
}

void write_png(image img, char* outfile) {
  FILE* output = fopen(outfile, "w");
  if(!output) {
    fprintf(stderr, "ERROR: Couldn't open file \"%s\"\n", outfile);
  }

  png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
  if(!png_ptr) {
    fprintf(stderr, "ERROR: Couldn't create png_ptr\n");
    fclose(output);
    return;
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);
  if(!info_ptr) {
    fprintf(stderr, "ERROR: Couldn't create info_ptr\n");
    fclose(output);
    png_destroy_write_struct(&png_ptr,  NULL);
    return;
  }

  if(setjmp(png_jmpbuf(png_ptr))) {
    fprintf(stderr, "ERROR: An error occurred\n");
    fclose(output);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return;
  }
  png_init_io(png_ptr, output);

  png_set_rows(png_ptr, info_ptr, (unsigned char**) img.data);

  png_set_IHDR(png_ptr, info_ptr, img.width, img.height, 8,
               img.channels == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
               PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE
  );

  png_write_info(png_ptr, info_ptr);

  png_write_png(png_ptr, info_ptr, 0, NULL);

  png_destroy_write_struct(&png_ptr, &info_ptr);
}

bool pixel_equal(pixel a, pixel b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

bool pixel_diff(int8_t dr, int8_t dg, int8_t db) {
  return -2 <= dr && dr <= 1
      && -2 <= dg && dg <= 1
      && -2 <= db && db <= 1;
}

bool pixel_diff_luma(int8_t dr_dg, int8_t dg, int8_t db_dg) {
  return - 8 <= dr_dg && dr_dg <=  7
      && -32 <=    dg &&    dg <= 31
      && - 8 <= db_dg && db_dg <=  7;
}

uint8_t hash(pixel p) {
  return (p.r * 3 + p.g * 5 + p.b * 7 + p.a * 11) % 64;
}

void write_qoi(image img, char* outfile) {
  memset(array, 0, sizeof(pixel) * 64);

  FILE* output = fopen(outfile, "w");
  if(!output) {
    fprintf(stderr, "ERROR: Couldn't open file \"%s\"\n", outfile);
  }

  uint8_t width_parts[4];
  for(int i = 0; i < 4; i++) {
    width_parts[i] = img.width >> 8 * (3 - i);
  }
  uint8_t height_parts[4];
  for(int i = 0; i < 4; i++) {
    height_parts[i] = img.height >> 8 * (3 - i);
  }

  fwrite("qoif", 1, 4, output);
  fwrite(width_parts, 1, 4, output);
  fwrite(height_parts, 1, 4, output);
  fwrite(&img.channels, 1, 1, output);
  fwrite(&img.colorspace, 1, 1, output);

  pixel pp = {.a = 255};
  pixel cp;

  uint8_t pixel_hash;
  int8_t dr, dg, db, dr_dg, db_dg;
  for(int i = 0; i < img.height; i++) {
    for(int j = 0; j < img.width; j++) {
      cp.r = img.data[i][j * img.channels];
      cp.g = img.data[i][j * img.channels + 1];
      cp.b = img.data[i][j * img.channels + 2];
      if(img.channels == 4) cp.a = img.data[i][j * img.channels + 3];

      if(pixel_equal(cp, pp)) { // qoi_run
        bool b = false;
        uint8_t k = 1;

        for(; k < 62; k++) {
          if((j + k) >= img.width) break;
          cp.r = img.data[i][(j + k) * img.channels];
          cp.g = img.data[i][(j + k) * img.channels + 1];
          cp.b = img.data[i][(j + k) * img.channels + 2];
          if(img.channels == 4) cp.a = img.data[i][(j + k) * img.channels + 3];
          if(!pixel_equal(cp, pp)) {j++; b = true; break;}
        }

        j += k - 1;

        uint8_t run = (0b11 << 6) | (k - 1);

        fwrite(&run, 1, 1, output);

        if(!b) continue;
      }

      if(pixel_equal(array[pixel_hash = hash(cp)], cp)) { // qoi_index
        fwrite(&pixel_hash, 1, 1, output);

      } else if(cp.a != pp.a) { // qoi_rgba
        uint8_t rgba = -1;
        fwrite(&rgba, 1, 1, output);
        fwrite(&cp, 1, 4, output);

      } else if(pixel_diff(dr = cp.r - pp.r, // qoi_diff
                           dg = cp.g - pp.g,
                           db = cp.b - pp.b)) {
        uint8_t diff = (0b01 << 6) | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2);
        fwrite(&diff, 1, 1, output);

      } else if(pixel_diff_luma(dr_dg = dr - dg, // qoi_diff_luma
                                dg,
                                db_dg = db - dg)) {
        uint8_t luma[2] = {(0b10 << 6) | (dg + 32), (dr_dg + 8) << 4 | (db_dg + 8)};
        fwrite(luma, 1, 2, output);

      } else { // qoi_rgb
        uint8_t rgb = -2;
        fwrite(&rgb, 1, 1, output);
        fwrite(&cp, 1, 3, output);
      }

      array[pixel_hash] = cp;
      pp = cp;
    }
  }

  fwrite("\0\0\0\0\0\0\0\1", 1, 8, output); // qoi end tag
}

image read_raw(char* infile, uint32_t width, uint32_t height, uint8_t channels) {
  image r = {0};

  FILE* file = fopen(infile, "r");
  if(!file) {
    fprintf(stderr, "ERROR: No such file \"%s\"\n", infile);
    return r;
  }

  r.data = malloc(sizeof(uint8_t*) * height);
  for(int i = 0; i < height; i++) {
    r.data[i] = malloc(width * channels);
    fread(r.data[i], 1, width * channels, file);
  }

  return r;
}

image read_png(char* infile) {
  int number = 4;
  uint8_t* header = malloc(number);

  FILE* file = fopen(infile, "rb");
  if(!file) {
    fprintf(stderr, "ERROR: No such file: \"%s\"\n", infile);
    return (image) {0};
  }

  if(fread(header, 1, number, file) != number) {
    fprintf(stderr, "ERROR: Couldn't read from file \"%s\"\n", infile);
    free(header);
    fclose(file);
    return (image) {0};
  }

  int is_png = !png_sig_cmp(header, 0, number);
  if(!is_png) {
    fprintf(stderr, "ERROR: File \"%s\" is not a png image\n", infile);
    free(header);
    fclose(file);
    return (image) {0};
  }

  free(header);

  png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if(!png_ptr) {
    fprintf(stderr, "ERROR: Couldn't create png_ptr\n");
    fclose(file);
    return (image) {0};
  }

  png_infop info_ptr = png_create_info_struct(png_ptr);

  if(!info_ptr) {
    fprintf(stderr, "ERROR: Couldn't create info_ptr\n");
    png_destroy_read_struct(&png_ptr, NULL, NULL);
    fclose(file);
    return (image) {0};
  }

  if(setjmp(png_jmpbuf(png_ptr))) {
    fprintf(stderr, "ERROR: An error occurred\n");
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    fclose(file);
    return (image) {0};
  }

  png_init_io(png_ptr, file);
  png_set_sig_bytes(png_ptr, number);

  png_read_png(png_ptr, info_ptr, 0, NULL);
  fclose(file);
  
  image r = {0};

  uint8_t b = png_get_bit_depth(png_ptr, info_ptr);
  if(b != 8) {
    fprintf(stderr, "ERROR: File \"%s\" is has unsupported color-depth (got %d, expected 8)\n",
        infile, b
    );
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    return (image) {0};
  }

  r.width = png_get_image_width(png_ptr, info_ptr);
  r.height = png_get_image_height(png_ptr, info_ptr);
  uint8_t c = png_get_color_type(png_ptr, info_ptr);
  if(c == 2) c = 3; // 2 == rgb
  if(c == 6) c = 4; // 6 == rgba
  r.channels = c;

  uint8_t** rows = png_get_rows(png_ptr, info_ptr);
  r.data = malloc(sizeof(uint8_t*) * r.height);
  for(int i = 0; i < r.height; i++) {
    r.data[i] = malloc(r.width * r.channels);
    memcpy(r.data[i], rows[i], r.width * r.channels);
  }

  png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

  return r;
}

void shift_read(uint8_t* read_buf, int amount, FILE* file) {
  for(int i = 0; i < (8 - amount); i++) {
    read_buf[i] = read_buf[i + amount];
  }
  fread(&read_buf[8 - amount], 1, amount, file);
}

void shift_read1(uint8_t* read_buf, FILE* file) {
  for(int i = 0; i < 7; i++) {
    read_buf[i] = read_buf[i + 1];
  }
  fread(&read_buf[7], 1, 1, file);
}

int qoi_end(uint8_t* read_buf) {
  for(int i = 0; i < 7; i++) {
    if(read_buf[i]) return 0;
  }
  if(read_buf[7] != 1) return 0;

  return 1;
}

image read_qoi(char* infile) {
  memset(array, 0, sizeof(pixel) * 64);

  image img = {0};

  FILE* file = fopen(infile, "r");
  if(!file) {
    fprintf(stderr, "ERROR: No such file \"%s\"\n", infile);
    return img;
  }

  char magic_number[5];
  fread(magic_number, 1, 4, file);
  if(strcmp("qoif", magic_number) != 0) {
    fclose(file);
    return img;
  }

  uint8_t width_parts[4];
  fread(width_parts, 1, 4, file);
  for(int i = 0; i < 4; i++) {
    img.width += width_parts[i] << (8 * (3 - i));
  }

  uint8_t height_parts[4];
  fread(height_parts, 1, 4, file);
  for(int i = 0; i < 4; i++) {
    img.height += height_parts[i] << (8 * (3 - i));
  }

  fread(&img.channels, 1, 1, file);
  fread(&img.colorspace, 1, 1, file);

  img.data = malloc(sizeof(uint8_t*) * img.height);
  for(int i = 0; i < img.height; i++) {
    img.data[i] = malloc(img.width * img.channels + 1);
  }

  uint8_t read_buf[8] = {0};
  fread(read_buf, 1, 8, file);

  pixel pp = {.a = 255};
  pixel cp;

  int8_t dr, dg, db, dr_dg, db_dg;
  for(int i = 0; i < img.height; i++) {
    for(int j = 0; j < img.width * img.channels; j++) {
      if(qoi_end(read_buf)) goto end;

      if(read_buf[0] == (uint8_t) -1) { // qoi_rgba
        img.data[i][j] = (cp.r = read_buf[1]);
        img.data[i][j + 1] = (cp.g = read_buf[2]);
        img.data[i][j + 2] = (cp.b = read_buf[3]);
        img.data[i][j + 3] = (cp.a = read_buf[4]);

        shift_read(read_buf, 4, file);
        j += 3;

      } else if(read_buf[0] == (uint8_t) -2) { // qoi_rgb
        img.data[i][j] = (cp.r = read_buf[1]);
        img.data[i][j + 1] = (cp.g = read_buf[2]);
        img.data[i][j + 2] = (cp.b = read_buf[3]);
        if(img.channels == 4) img.data[i][j + 3] = cp.a;

        shift_read(read_buf, 3, file);
        j += img.channels - 1;

      } else if((read_buf[0] >> 6) == 0b00) { // qoi_index
        cp = array[read_buf[0]];

        img.data[i][j] = cp.r;
        img.data[i][j + 1] = cp.g;
        img.data[i][j + 2] = cp.b;
        if(img.channels == 4) img.data[i][j + 3] = cp.a;

        j += img.channels - 1;

      } else if((read_buf[0] >> 6) == 0b01) { // qoi_diff
        dr = ((read_buf[0] >> 4) & 0b11) - 2;
        dg = ((read_buf[0] >> 2) & 0b11) - 2;
        db = (read_buf[0] & 0b11) - 2;

        img.data[i][j] = (cp.r += dr);
        img.data[i][j + 1] = (cp.g += dg);
        img.data[i][j + 2] = (cp.b += db);
        if(img.channels == 4) img.data[i][j + 3] = cp.a;

        j += img.channels - 1;

      } else if((read_buf[0] >> 6) == 0b10) { // qoi_diff_luma
        dg = (read_buf[0] & 0b111111) - 32;
        db_dg = (read_buf[1] & 0b1111) - 8;
        dr_dg = (read_buf[1] >> 4) - 8;
        dr = dr_dg + dg;
        db = db_dg + dg;

        img.data[i][j] = (cp.r += dr);
        img.data[i][j + 1] = (cp.g += dg);
        img.data[i][j + 2] = (cp.b += db);
        if(img.channels == 4) img.data[i][j + 3] = cp.a;

        shift_read1(read_buf, file);
        j += img.channels - 1;

      } else if((read_buf[0] >> 6) == 0b11) { // qoi_run
        uint8_t run = (read_buf[0] & 0b111111) + 1;
        for(int k = 0; k < run; k++) {
          if(j + k * img.channels == img.width * img.channels) {
            if(++i == img.height) break;
            j = 0;
            run -= k;
            k = 0;
          }
          img.data[i][j + k * img.channels] = cp.r;
          img.data[i][j + k * img.channels + 1] = cp.g;
          img.data[i][j + k * img.channels + 2] = cp.b;
          if(img.channels == 4) img.data[i][j + (k * 4) + 3] = cp.a;
        }
        j += run * img.channels - 1;
      }
      array[hash(cp)] = cp;
      pp = cp;
      shift_read1(read_buf, file);
    }
  }
  end:
  fclose(file);

  return img;
}

void usage() {
  printf("./qoic [options] [raw,png,qoi] infile [raw,png,qoi] outfile\n\n");
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
  bool ws = false, hs = false, cs = false, ss = false;

  image img = {0};

  int i;
  for(i = 1; i < argc; i++) {
    if(argv[i][0] != '-') break;
    if(strcmp("-?", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
      usage();
      return 0;

    } else if(strcmp("-w", argv[i]) == 0) {
      if(++i >= argc) goto fail;
      int ret = sscanf(argv[i], "%d", &width);
      if(ret != 1) goto fail;
      ws = true;

    } else if(strcmp("-h", argv[i]) == 0) {
      if(++i >= argc) goto fail;
      int ret = sscanf(argv[i], "%d", &height);
      if(ret != 1) goto fail;
      hs = true;

    } else if(strcmp("-c", argv[i]) == 0) {
      if(++i >= argc) goto fail;
      int ret = sscanf(argv[i], "%d", &channels);
      if(ret != 1) goto fail;
      cs = true;

    } else if(strcmp("-s", argv[i]) == 0) {
      if(++i >= argc) goto fail;
      int ret = sscanf(argv[i], "%d", &colorspace);
      if(ret != 1) goto fail;
      ss = true;
    }
  }

  if(i + 3 >= argc 
     || (channels != 3 && channels != 4)
     || (colorspace != 0 && colorspace != 1)) goto fail;


  if(strcmp("raw", argv[i]) == 0) {
    if(width == 0 || height == 0) goto fail;
    img = read_raw(argv[i + 1], width, height, channels);

  } else if(strcmp("png", argv[i]) == 0) {
    img = read_png(argv[i + 1]);

  } else if(strcmp("qoi", argv[i]) == 0) {
    img = read_qoi(argv[i + 1]);

  } else {
    goto fail;
  }

  if(!img.data) goto fail;

  img.width = ws ? width : img.width;
  img.height = hs ? height : img.height;
  img.channels = cs ? channels : (img.channels ? img.channels : 3);
  img.colorspace = ss ? colorspace : img.colorspace;

  if(strcmp("raw", argv[i + 2]) == 0) {
    write_raw(img, argv[i + 3]);

  } else if(strcmp("png", argv[i + 2]) == 0) {
    write_png(img, argv[i + 3]);

  } else if(strcmp("qoi", argv[i + 2]) == 0) {
    write_qoi(img, argv[i + 3]);

  } else {
    for(int i = 0; i < img.height; i++) {
      free(img.data[i]);
    }
    free(img.data);
    goto fail;
  }

  for(int i = 0; i < img.height; i++) {
    free(img.data[i]);
  }
  free(img.data);
  return 0;

  fail:
  usage();
  return 1;
}
