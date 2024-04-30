#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "vendor/stb/stb_image.h"

#if defined (__unix__) || (defined (__APPLE__) && defined (__MACH__))

#include <unistd.h>

static inline int my_isatty(FILE *f)
{
    return isatty(fileno(f));
}

#elif defined(_WIN32) || defined(WIN32) 

#include <io.h>

static inline int my_isatty(FILE *f)
{
    return _isatty(_fileno(f));
}

#else

static inline int my_isatty(FILE *f)
{
    (void) f;

    fprintf(stderr, "Warning: Could not check for tty-ness\n");

    return 0;
}

#endif

enum {
    QOI_COLORSPACE_SRGB = 0,
    QOI_COLORSPACE_LINEAR = 1,
};

#define QOI_HEADER_SIZE 14
#define QOI_HEADER_MAGIC_INDEX 0
#define QOI_HEADER_WIDTH_INDEX 4
#define QOI_HEADER_HEIGHT_INDEX 8
#define QOI_HEADER_CHANNELS_INDEX 12
#define QOI_HEADER_COLORSPACE_INDEX 13
#define QOI_HEADER_MAGIC_SIZE 4
#define QOI_HEADER_MAGIC "qoif"

#define QOI_ARRAY_SIZE 64

#define QOI_OP_RGB 0xFE
#define QOI_OP_RGBA 0xFF

#define QOI_END_SIZE 8
#define QOI_END "\0\0\0\0\0\0\0\1"

#if defined(__GNUC__) && ((__GNUC__ > 3) || (__GNUC__ == 3 && __GNUC_MINOR__ >= 1))
#   define SKP_restrict __restrict
#elif defined(_MSC_VER) && _MSC_VER >= 1400
#   define SKP_restrict __restrict
#else
#   define SKP_restrict
#endif


static void fwrite_chk(const void* SKP_restrict buffer, size_t size, size_t count, FILE* SKP_restrict stream) {
    if (fwrite(buffer, size, count, stream) < count) {
        perror("failed to write to file");
        exit(1);
    }
}

static void fclose_chk(FILE *stream) {
    if (fclose(stream) != 0) {
        perror("failed to close file");
        exit(1);
    }
}

static inline uint8_t calc_array_pos(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    return ((uint32_t) r * 3 + (uint32_t) g * 5 + (uint32_t) b * 7 + (uint32_t) a * 11) % 64;
}

static void write_be_32(unsigned char *buf, uint32_t value);
static void write_header(FILE *out, uint32_t x, uint32_t y, uint8_t channels, uint8_t colorspace);
static void write_pixels(FILE *out, unsigned char *data, int x, int y, int n);
static void write_end(FILE *out);
static void get_next_pixel(unsigned char **buf, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a, uint8_t n);

// Program options
int ttyCheckInput = 1;
int ttyCheckOutput = 1;
FILE *in;
FILE *out;

static void parse_opts(int argc, char **argv)
{
    int i, j, len;
    int bigt = 0, brkfor;
    int nt = 0, ni = 0, no = 0;
    char *instr = NULL, *outstr = NULL;

    in = stdin;
    out = stdout;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }

        if (argv[i][0] != '-') {
            break;
        }

        len = strlen(argv[i]);
        for (j = 1; j < len; j++) {
            brkfor = 0;

            switch (argv[i][j]) {
            case 'T':
                bigt = 1;
                nt++;

                break;
            case 't':
                ttyCheckInput = ttyCheckOutput = 0;
                nt++;

                break;
            case 'i':
                instr = (argv[i][j + 1] == '\0') ? argv[++i] : argv[i] + j + 1;
                ni++;
                ttyCheckInput = 0;

                brkfor = 1;

                break;
            case 'o':
                outstr = (argv[i][j + 1] == '\0') ? argv[++i] : argv[i] + j + 1;
                no++;
                ttyCheckOutput = 0;

                brkfor = 1;

                break;
            default:
                fprintf(stderr, "unrecognized arument '-%c'\n", argv[i][j]);
                break;
            }

            if (brkfor) {
                break;
            }
        }
    }

    if (i < argc) {
        fprintf(stderr, "bad usage: positional arguments provided\n");
        exit(3);
    }

    if (ni > 1) {
        fprintf(stderr, "multiple -i flags provided\n");
        exit(3);
    }

    if (no > 1) {
        fprintf(stderr, "multiple -o flags provided\n");
        exit(3);
    }

    if (nt > 1) {
        fprintf(stderr, "multiple -t or -T flags provided\n");
        exit(3);
    }

    if (ni > 0 && instr == NULL) {
        fprintf(stderr, "-i provided but no filename provided\n");
        exit(3);
    }

    if (no > 0 && outstr == NULL) {
        fprintf(stderr, "-o provided but no filename provided\n");
        exit(3);
    }

    if (bigt) {
        ttyCheckInput = ttyCheckOutput = 1;
    }

    if (instr != NULL) {
        in = fopen(instr, "rb");
        if (in == NULL) {
            perror("failed to open input file");
            exit(1);
        }
    }

    if (outstr != NULL) {
        out = fopen(outstr, "wb");
        if (out == NULL) {
            perror("failed to open output file");
            exit(1);
        }
    }
}

int main(int argc, char **argv)
{
    unsigned char *data;
    int x, y, nchannels;

    parse_opts(argc, argv);

    if (ttyCheckInput && my_isatty(in)) {
        fprintf(stderr, "refusing to read from tty input\n");
        exit(2);
    }

    if (ttyCheckOutput && my_isatty(out)) {
        fprintf(stderr, "refusing to write tty output\n");
        exit(2);
    }

    data = stbi_load_from_file(in, &x, &y, &nchannels, 0);

    if (data == NULL) {
        fprintf(stderr, "Failed to read file: %s\n", stbi_failure_reason());
        exit(1);
    }

    write_header(out, x, y, nchannels % 2 + 3, QOI_COLORSPACE_SRGB);
    write_pixels(out, data, x, y, nchannels);
    write_end(out);

    stbi_image_free(data);
    
    if (in != stdin) {
        fclose_chk(stdin);
    }

    if (out != stdout) {
        fclose_chk(stdout);
    }
}

static void write_be_32(unsigned char *buf, uint32_t value)
{
    buf[0] = (uint8_t) (value >> 24);
    buf[1] = (uint8_t) (value >> 16);
    buf[2] = (uint8_t) (value >> 8);
    buf[3] = (uint8_t) (value);
}

static void write_header(FILE *out, uint32_t x, uint32_t y, uint8_t channels, uint8_t colorspace)
{
    unsigned char buf[QOI_HEADER_SIZE];

    memcpy(buf + QOI_HEADER_MAGIC_INDEX, QOI_HEADER_MAGIC, QOI_HEADER_MAGIC_SIZE);
    write_be_32(buf + QOI_HEADER_WIDTH_INDEX, x);
    write_be_32(buf + QOI_HEADER_HEIGHT_INDEX, y);
    buf[QOI_HEADER_CHANNELS_INDEX] = channels;
    buf[QOI_HEADER_COLORSPACE_INDEX] = colorspace;

    fwrite_chk(buf, 1, QOI_HEADER_SIZE, out);
}

static void write_rgb_pixel(FILE *out, uint8_t r, uint8_t g, uint8_t b)
{
    unsigned char buf[4];

    buf[0] = QOI_OP_RGB;
    buf[1] = r;
    buf[2] = g;
    buf[3] = b;

    fwrite_chk(buf, 1, 4, out);
}

static void write_rgba_pixel(FILE *out, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    unsigned char buf[5];

    buf[0] = QOI_OP_RGBA;
    buf[1] = r;
    buf[2] = g;
    buf[3] = b;
    buf[4] = a;

    fwrite_chk(buf, 1, 5, out);
}

static void write_index_pixel(FILE *out, uint8_t index)
{
    assert(index < 64);

    fwrite_chk(&index, 1, 1, out);
}

static void write_diff_pixel(FILE *out, int8_t rdiff, int8_t gdiff, int8_t bdiff)
{
    unsigned char buf;

    assert(rdiff <= 1 && gdiff <= 1 && bdiff <= 1);
    assert(rdiff >= -2 && gdiff >= -2 && bdiff >= -2);

    rdiff += 2;
    gdiff += 2;
    bdiff += 2;

    buf = 0x40 | (rdiff << 4) | (gdiff << 2) | bdiff;

    fwrite_chk(&buf, 1, 1, out);
}

static void write_luma_pixel(FILE *out, int8_t rdiff, int8_t gdiff, int8_t bdiff)
{
    unsigned char buf[2];

    int8_t dr_dg = rdiff - gdiff;
    int8_t db_dg = bdiff - gdiff;

    assert(gdiff >= -32 && gdiff <= 31);
    assert(dr_dg >= -8 && dr_dg <= 7);
    assert(db_dg >= -8 && db_dg <= 7);

    gdiff += 32;
    dr_dg += 8;
    db_dg += 8;

    buf[0] = 0x80 | gdiff;
    buf[1] = (dr_dg << 4) | db_dg;

    fwrite_chk(buf, 1, 2, out);
}

static void write_run(FILE *out, uint8_t run)
{
    unsigned char buf;

    assert(run >= 1 && run <= 62);
    
    buf = 0xC0 | (run - 1);

    fwrite_chk(&buf, 1, 1, out);
}

static void write_pixels(FILE *out, unsigned char *data, int x, int y, int n)
{
    uint8_t r, g, b, a;
    uint8_t last_r = 0, last_g = 0, last_b = 0, last_a = 255;
    int8_t rdiff, gdiff, bdiff, adiff;
    int run = 0;
    uint8_t array_r[QOI_ARRAY_SIZE], array_b[QOI_ARRAY_SIZE], array_g[QOI_ARRAY_SIZE], array_a[QOI_ARRAY_SIZE];
    uint8_t array_pos;

    memset(array_r, 0, QOI_ARRAY_SIZE);
    memset(array_g, 0, QOI_ARRAY_SIZE);
    memset(array_b, 0, QOI_ARRAY_SIZE);
    memset(array_a, 0, QOI_ARRAY_SIZE);

    for (long long i = 0; i < x * y; i++) {
        get_next_pixel(&data, &r, &g, &b, &a, n);
        if (r == last_r && g == last_g && b == last_b && a == last_a) {
            run++;

            if (run == 62) {
                write_run(out, run);
                run = 0;
            }
        } else {
            if (run > 0) {
                write_run(out, run);
                run = 0;
            }

            array_pos = calc_array_pos(r, g, b, a);

            if (array_r[array_pos] == r && array_g[array_pos] == g && array_b[array_pos] == b && array_a[array_pos] == a) {
                write_index_pixel(out, array_pos);
            } else {
                array_r[array_pos] = r;
                array_g[array_pos] = g;
                array_b[array_pos] = b;
                array_a[array_pos] = a;

                rdiff = (int8_t) r - last_r;
                gdiff = (int8_t) g - last_g;
                bdiff = (int8_t) b - last_b;
                adiff = (int8_t) a - last_a;

                if (adiff == 0 && rdiff >= -2 && rdiff <= 1 && bdiff >= -2 && bdiff <= 1 && gdiff >= -2 && gdiff <= 1) {
                    write_diff_pixel(out, rdiff, gdiff, bdiff);
                } else if (
                    adiff == 0 &&
                    gdiff >= -32 && gdiff <= 31 &&
                    (rdiff - gdiff) >= -8 && (rdiff - gdiff) <= 7 &&
                    (bdiff - gdiff) >= -8 && (bdiff - gdiff) <= 7
                ) {
                    write_luma_pixel(out, rdiff, gdiff, bdiff);
                } else if (adiff == 0) {
                    write_rgb_pixel(out, r, g, b);
                } else {
                    write_rgba_pixel(out, r, g, b, a);
                }
            }

            last_r = r;
            last_g = g;
            last_b = b;
            last_a = a;
        }
    }

    if (run > 0) {
        write_run(out, run);
        run = 0;
    }
}

static void write_end(FILE *out)
{
    unsigned char buf[QOI_END_SIZE];

    memcpy(buf, QOI_END, QOI_END_SIZE);

    fwrite_chk(buf, 1, QOI_END_SIZE, out);
}

static void get_next_pixel(unsigned char **buf, uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a, uint8_t n)
{
    switch (n) {
    case 1:
        *r = *g = *b = **buf;
        *a = 255;
        (*buf)++;
        break;
    case 2:
        *r = *g = *b = **buf;
        (*buf)++;
        *a = **buf;
        (*buf)++;
        break;
    case 3:
        *r = (*buf)[0];
        *g = (*buf)[1];
        *b = (*buf)[2];
        *a = 255;
        (*buf) += 3;
        break;
    case 4:
        *r = (*buf)[0];
        *g = (*buf)[1];
        *b = (*buf)[2];
        *a = (*buf)[3];
        (*buf) += 4;
        break;
    default:
        assert(0);
    }
}
