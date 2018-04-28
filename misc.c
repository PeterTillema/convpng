#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <math.h>

#include "libs/lodepng.h"
#include "libs/zx7.h"

#include "main.h"
#include "misc.h"
#include "parser.h"
#include "logging.h"
#include "palettes.h"

void *safe_malloc(size_t n) {
    void* p = malloc(n);
    if (!p) { errorf("out of memory."); }
    return p;
}

void *safe_calloc(size_t n, size_t m) {
    void* p = calloc(n, m);
    if (!p) { errorf("out of memory."); }
    return p;
}

void *safe_realloc(void *a, size_t n) {
    void* p = realloc(a, n);
    if (!p) { errorf("out of memory."); }
    return p;
}

uint16_t rgb1555(const uint8_t r8, const uint8_t g8, const uint8_t b8) {
    uint8_t r5 = round((int)r8 * 31 / 255.0);
    uint8_t g6 = round((int)g8 * 63 / 255.0);
    uint8_t b5 = round((int)b8 * 31 / 255.0);
    return ((g6 & 1) << 15) | (r5 << 10) | ((g6 >> 1) << 5) | b5;
}

uint16_t rgb565(const uint8_t r8, const uint8_t g8, const uint8_t b8) {
    uint8_t r5 = round(((int)r8 * 31 + 127) / 255.0);
    uint8_t g6 = round(((int)g8 * 63 + 127) / 255.0);
    uint8_t b5 = round(((int)b8 * 31 + 127) / 255.0);
    return (b5 << 11) | (g6 << 5) | r5;
}

char *str_dup(const char *s) {
    char *d = safe_malloc(strlen(s)+1);  // allocate memory
    if (d) strcpy(d, s);                 // copy string if okay
    return d;                            // return new memory
}

char *str_dupcat(const char *s, const char *c) {
    if (!s) {
        return str_dup(c);
    } else
    if (!c) {
        return str_dup(s);
    }
    char *d = safe_malloc(strlen(s)+strlen(c)+1);
    if (d) { strcpy(d, s); strcat(d, c); }
    return d;
}

char *str_dupcatdir(const char *s, const char *c) {
    char *d = str_dupcat(s, c);
    if (d && convpng.directory) {
        char *f = d;
        d = str_dupcat(convpng.directory, d);
        free(f);
    }
    return d;
}

// encodes a PNG image (used for creating global palettes)
void encodePNG(const char* filename, const unsigned char* image, unsigned width, unsigned height) {
    unsigned char* png;
    size_t pngsize;
    unsigned error = lodepng_encode32(&png, &pngsize, image, width, height);

    /* if there's an error, display it */
    if (error == LODEPNG_ERR_OPEN) { errorf("could not open '%s'", filename); }
    else if (error) { errorf("writting '%s'", filename); }
    else { lodepng_save_file(png, pngsize, filename); }

    free(png);
}

// builds an image of the palette
void build_image_palette(const liq_palette *pal, const unsigned length, const char *filename) {
    uint8_t *image = safe_malloc(length * 4);
    unsigned int x;
    char *name = str_dupcat(convpng.directory, filename);

    for (x = 0; x < length; x++) {
        unsigned int o = x << 2;
        const liq_color *c = &pal->entries[x];
        image[o + 0] = c->r;
        image[o + 1] = c->g;
        image[o + 2] = c->b;
        image[o + 3] = 255;
    }
    encodePNG(name, image, length, 1);
    lof("Wrote palette image (%s)\n", name);
    free(image);
    free(name);
}

void output_array_compressed(const format_t *format, output_t *out, uint8_t *compressed_data, unsigned len) {
    unsigned j, k;

    // write the whole array in a big block
    for (k = j = 0; j < len; j++, k++) {
        format->print_byte(out, compressed_data[j], !(j+1 == len || (k+1) & 32));
        if ((k+1) & 32 && j+1 < len) {
            k = -1;
            format->print_next_array_line(out, false, false);
        }
    }
    format->print_next_array_line(out, false, true);
}

void output_array(const format_t *format, output_t *out, uint8_t *data, unsigned int width, unsigned int height) {
    unsigned int j, k;

    // write out the array
    for (k = 0; k < height; k++) {
        unsigned int o = k * width;
        for (j = 0; j < width; j++) {
            format->print_byte(out, data[j + o], j+1 != width);
        }
        format->print_next_array_line(out, false, k+1 == height);
    }
}

void set_image(image_t *i, uint8_t *data, unsigned int size) {
    data_t *b = &i->block;
	b->data = safe_realloc(b->data, b->total_size + size);
	memcpy(&b->data[b->total_size], data, size);
    b->size[b->num_sizes] = size;
    b->total_size += size;
    b->num_sizes++;
}

uint8_t *compress_image(uint8_t *image, unsigned int *size, unsigned int mode) {
    long delta;
    Optimal *opt;
    uint8_t *ret = NULL;
    size_t s_size = *size;

    // select the compression mode
    switch (mode) {
        case COMPRESS_ZX7:
            opt = optimize(image, s_size);
            ret = compress(opt, image, s_size, &s_size, &delta);
            *size = s_size;
            free(opt);
            break;
        default:
            errorf("unexpected compression mode.");
            break;
    }
    return ret;
}

void force_image_bpp(uint8_t bpp, uint8_t *rgba, uint8_t *data, uint8_t *data_buffer, unsigned int *width, unsigned int height, unsigned int *size) {
    unsigned int j,k,i;
    j = k = i = 0;

    if (bpp == 16) {
        for (; j < *size; j++) {
            uint16_t _short = rgb565(rgba[i + 0], rgba[i + 1], rgba[i + 2]);
            data_buffer[k++] = _short >> 8;
            data_buffer[k++] = _short & 255;
            i += 4;
        }
        *width *= 2;
    } else {
        uint8_t lob, shift_amt = 1;
        switch (bpp) {
            case 1:
                shift_amt = 3; break;
            case 2:
                shift_amt = 2; break;
            case 4:
                shift_amt = 1; break;
            default:
                errorf("unexpected bpp mode");
                break;
        }

        uint8_t inc_amt = pow(2, shift_amt);

        for (; j < height; j++) {
            unsigned int o = j * *width;
            for (k = 0; k < *width; k += inc_amt) {
                uint8_t curr_inc = inc_amt;
                uint8_t byte = 0;
                for (lob = 0; lob < inc_amt; lob++) {
                    byte |= data[k + o + lob] << --curr_inc;
                }
                data_buffer[i++] = byte;
            }
        }

        *width /= inc_amt;
    }
    *size = *width * height;
}

void force_color_index(liq_color *color, liq_palette *pal, unsigned int *pal_len, unsigned int max_pal_len, unsigned int index) {
    unsigned int j;

    // if the user wants the index to be elsewhere, expand the array
    if (index > *pal_len) {
        if (max_pal_len) {
            errorf("index placed outside max palette size");
        }
        *pal_len = index + 1;
    }

    for (j = 0; j < *pal_len; j++) {
        if (!memcmp(color, &pal->entries[j], sizeof(liq_color)))
            break;
    }

    // move transparent color to index
    liq_color tmpc = pal->entries[j];
    pal->entries[j] = pal->entries[index];
    pal->entries[index] = tmpc;
}

unsigned int add_color_offsets(uint8_t *array, unsigned int len) {
    unsigned int i;
    unsigned int index = 0;
    unsigned int new_length = len + 4;
    unsigned int tcp_header_offset = len;
    unsigned int tcp_amount_of_pixels = 0;
    bool first_tcp_set_offset = true;
    
    for (i = 0; i < len; i++) {
        uint8_t byte = array[i];
        
        if (byte == 247 || byte == 249 || byte == 251 || byte == 253) {
            // Set the offset
            if (first_tcp_set_offset) {
                array[tcp_header_offset] = index >> 8;
                array[tcp_header_offset + 1] = index & 0xFF;
                first_tcp_set_offset = false;
            }
            
            if (tcp_amount_of_pixels) {
                if (index > 255) {
                    array[tcp_header_offset + 2] = -30 - ((tcp_amount_of_pixels - 1) % 8 * 5);
                    array[tcp_header_offset + 3] = (tcp_amount_of_pixels - 1) / 8 + 1;
                    tcp_header_offset = new_length;
                    new_length += 4;
                    array[tcp_header_offset] = index >> 8;
                    array[tcp_header_offset + 1] = index & 0xFF;
                    tcp_amount_of_pixels = 1;
                } else {
                    array[new_length++] = index;
                    tcp_amount_of_pixels++;
                }
            } else {
                array[tcp_header_offset] = index >> 8;
                array[tcp_header_offset + 1] = index & 0xFF;
                tcp_amount_of_pixels++;
            }
            index = 0;
        }
        index++;
    }
    array[tcp_header_offset + 2] = -30 - ((tcp_amount_of_pixels - 1) % 8 * 5);
    array[tcp_header_offset + 3] = (tcp_amount_of_pixels - 1) / 8 + 1;
    array[new_length++] = 0x80;
    
    return new_length;
}

unsigned int remove_elements(uint8_t *array, unsigned int len, uint8_t val) {
    unsigned int i;
    unsigned int l = 0;
    for (i = 0; i < len; i++) {
        if (array[i] != val) {
            array[l++] = array[i];
        }
    }
    return l;
}

unsigned int group_rlet_output(uint8_t *data, uint8_t *data_buffer, unsigned int width, unsigned int height, uint8_t tp_index) {
    unsigned int size = 0;
    unsigned int j = 0;

    memset(data_buffer, 0, width * height * 2);

    for (; j < height; j++) {
        unsigned int offset = j * width;
        unsigned int left = width;
        while (left) {
            unsigned int o = 0, t = 0;
            while (tp_index == data[t + offset] && t < left) {
                t++;
            }
            data_buffer[size++] = t;
            if ((left -= t)) {
                uint8_t *fix = &data_buffer[size++];
                while (tp_index != data[t + o + offset] && o < left) {
                    data_buffer[size++] = data[t + o + offset];
                    o++;
                }
                *fix = o;
                left -= o;
            }
            offset += o + t;
        }
    }
    return size;
}

char *strip_path(char *name) {
    char *h_name = name;
    if (name) {
        if (convpng.directory) {
            (h_name = strrchr(name, '/')) ? h_name++ : (h_name = name);
        }
    }
    return h_name;
}

output_t *output_create(void) {
    output_t *output = safe_malloc(sizeof(output_t));
    output->c = NULL;
    output->h = NULL;
    return output;
}

// create an icon for the C toolchain
int create_icon(void) {
    liq_image *image = NULL;
    liq_result *res = NULL;
    liq_attr *attr = NULL;
    uint8_t *rgba = NULL;
    uint8_t *data = NULL;
    unsigned int width,height,size,error,x,y,h;
    liq_color rgba_color;
    char **icon_options;
    char *filename;

    int num = separate_args(convpng.iconc, &icon_options, ',');
    if(num < 3) { errorf("not enough options."); }

    filename = icon_options[1];

    error = lodepng_decode32_file(&rgba, &width, &height, icon_options[0]);
    if(error) { lof("[error] could not open %s for conversion\n", icon_options[0]); exit(1); }
    if(width != ICON_WIDTH || height != ICON_HEIGHT) { errorf("icon image dimensions are not 16x16."); }

    attr = liq_attr_create();
    if (!attr) { errorf("could not create image attributes."); }
    image = liq_image_create_rgba(attr, rgba, ICON_WIDTH, ICON_HEIGHT, 0);
    if (!image) { errorf("could not create icon."); }

    size = width * height;

    for (h = 0; h < MAX_PAL_LEN; h++) {
        unsigned int o = h << 2;
        rgba_color.r = xlibc_palette[o + 0];
        rgba_color.g = xlibc_palette[o + 1];
        rgba_color.b = xlibc_palette[o + 2];
        rgba_color.a = xlibc_palette[o + 3];

        liq_image_add_fixed_color(image, rgba_color);
    }

    data = safe_malloc(size + 1);
    res = liq_quantize_image(attr, image);
    if (!res) {errorf("could not quantize icon."); }
    liq_write_remapped_image(res, image, data, size);

    FILE *out = fopen(filename, "w");
    if (convpng.icon_zds) {
        fprintf(out, " .def __program_icon\n .def __program_description\n\n .assume adl=1\n");
        fprintf(out, " segment .icon\n");

        fprintf(out,"\n jp __program_description_end\n db 1\n__program_icon:\n db %u,%u", width, height);
        for (y = 0; y < height; y++) {
            fputs("\n db ", out);
            for (x = 0; x < width; x++) {
                fprintf(out, "0%02Xh%s", data[x+y*width], x + 1 == width ? "" : ",");
            }
        }

        fprintf(out,"\n\n__program_description:\n");
        fprintf(out," db \"%s\",0\n", icon_options[2]);
        fprintf(out,"__program_description_end:\n");
        fprintf(stdout, "created icon using '%s'\n", icon_options[0]);
    } else {
        fprintf(out, "__icon_begin:\n db 1,%u,%u", width, height);
        for (y = 0; y < height; y++) {
            fputs("\n db ",out);
            for (x = 0; x < width; x++) {
                fprintf(out, "0%02Xh%s", data[x+(y*width)], x + 1 == width ? "" : ",");
            }
        }

        fprintf(out, "\n__icon_end:\n__program_description:\n");
        fprintf(out, " db \"%s\",0\n__program_description_end:\n", icon_options[2]);
        fprintf(stdout, "Created icon '%s' -> '%s'\n", icon_options[0], filename);
    }

    liq_attr_destroy(attr);
    liq_image_destroy(image);
    free(convpng.iconc);
    free(icon_options);
    free(data);
    free(rgba);
    fclose(out);
    return 0;
}
