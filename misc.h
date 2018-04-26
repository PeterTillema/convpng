#ifndef MISC_H
#define MISC_H

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "libs/libimagequant.h"
#include "format.h"

void *safe_malloc(size_t n);
void *safe_realloc(void *a, size_t n);
void *safe_calloc(size_t n, size_t m);
int create_icon(void);
char *str_dup(const char *s);
char *str_dupcat(const char *s, const char *c);
char *str_dupcatdir(const char *s, const char *c);
uint16_t rgb1555(const uint8_t r8, const uint8_t g8, const uint8_t b8);
uint16_t rgb565(const uint8_t r8, const uint8_t g8, const uint8_t b8);
void encodePNG(const char* filename, const unsigned char* image, unsigned int width, unsigned int height);
void build_image_palette(const liq_palette *pal, const unsigned int length, const char *filename);

void output_array_compressed(const format_t *format, output_t *out, uint8_t *compressed_data, unsigned int len);
void output_array(const format_t *format, output_t *out, uint8_t *data, unsigned int width, unsigned int height);
uint8_t *compress_image(uint8_t *image, unsigned int *size, unsigned int mode);

void force_image_bpp(uint8_t bpp, uint8_t *rgba, uint8_t *data, uint8_t *data_buffer, unsigned int *width, unsigned int height, unsigned int *size);
output_t *output_create(void);
char *strip_path(char *name);

void force_color_index(liq_color *color, liq_palette *pal, unsigned int *pal_len, unsigned int max_pal_len, unsigned int index);
unsigned int add_color_offsets(uint8_t *array, unsigned int len);
unsigned int remove_elements(uint8_t *array, unsigned int len, uint8_t val);
unsigned int group_rlet_output(uint8_t *data, uint8_t *data_buffer, unsigned int width, unsigned int height, uint8_t tp_index);

#endif
