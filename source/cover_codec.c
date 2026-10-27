#include "cover_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <png.h>

struct cover_error {
    struct jpeg_error_mgr jpeg;
    jmp_buf jump;
};
static void cover_jpeg_error(j_common_ptr codec) {
    struct cover_error *error = (struct cover_error *)codec->err;
    longjmp(error->jump, 1);
}
static void cover_jpeg_message(j_common_ptr codec) { (void)codec; }
struct decode_job {
    struct jpeg_decompress_struct codec;
    struct cover_error error;
    unsigned char *pixels, *row;
};
int nx_cover_decode_jpeg(const unsigned char *bytes, size_t size, struct nx_cover_rgb *out) {
    struct decode_job *job;
    int width, height, pitch;
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!bytes || size < 4 || size > 16u * 1024u * 1024u)
        return 0;
    job = calloc(1, sizeof(*job));
    if (!job)
        return 0;
    job->codec.err = jpeg_std_error(&job->error.jpeg);
    job->error.jpeg.error_exit = cover_jpeg_error;
    job->error.jpeg.output_message = cover_jpeg_message;
    /* Keep all cleanup state on the heap: C longjmp invalidates modified
       automatic variables, and must never cross C++ object destructors. */
    if (setjmp(job->error.jump)) {
        jpeg_destroy_decompress(&job->codec);
        free(job->pixels);
        free(job->row);
        free(job);
        return 0;
    }
    jpeg_create_decompress(&job->codec);
    job->codec.mem->max_memory_to_use = 16 * 1024 * 1024;
    jpeg_mem_src(&job->codec, bytes, (unsigned long)size);
    if (jpeg_read_header(&job->codec, TRUE) != JPEG_HEADER_OK || !job->codec.image_width ||
        !job->codec.image_height || job->codec.image_width > 4096 ||
        job->codec.image_height > 4096) {
        jpeg_destroy_decompress(&job->codec);
        free(job);
        return 0;
    }
    job->codec.scale_num = 1;
    job->codec.scale_denom = 1;
    while ((job->codec.image_width + job->codec.scale_denom - 1) / job->codec.scale_denom > 1024 ||
           (job->codec.image_height + job->codec.scale_denom - 1) / job->codec.scale_denom > 1024)
        job->codec.scale_denom *= 2;
    job->codec.out_color_space =
        (job->codec.jpeg_color_space == JCS_CMYK || job->codec.jpeg_color_space == JCS_YCCK)
            ? JCS_CMYK
            : JCS_RGB;
    jpeg_start_decompress(&job->codec);
    width = (int)job->codec.output_width;
    height = (int)job->codec.output_height;
    if (width < 1 || height < 1 || width > 1024 || height > 1024 ||
        (job->codec.output_components != 3 && job->codec.output_components != 4)) {
        jpeg_destroy_decompress(&job->codec);
        free(job);
        return 0;
    }
    pitch = width * 3;
    job->pixels = malloc((size_t)pitch * height);
    if (!job->pixels) {
        jpeg_destroy_decompress(&job->codec);
        free(job);
        return 0;
    }
    if (job->codec.output_components == 4) {
        job->row = malloc((size_t)width * 4);
        if (!job->row) {
            jpeg_destroy_decompress(&job->codec);
            free(job->pixels);
            free(job);
            return 0;
        }
    }
    while (job->codec.output_scanline < job->codec.output_height) {
        unsigned char *target = job->pixels + (size_t)job->codec.output_scanline * pitch;
        JSAMPROW row = job->row ? job->row : target;
        jpeg_read_scanlines(&job->codec, &row, 1);
        if (job->row)
            for (int x = 0; x < width; ++x)
                for (int c = 0; c < 3; ++c) {
                    unsigned color = row[x * 4 + c], black = row[x * 4 + 3];
                    target[x * 3 + c] = job->codec.saw_Adobe_marker
                                            ? (color * black + 127) / 255
                                            : ((255 - color) * (255 - black) + 127) / 255;
                }
    }
    jpeg_finish_decompress(&job->codec);
    jpeg_destroy_decompress(&job->codec);
    out->pixels = job->pixels;
    out->width = width;
    out->height = height;
    out->pitch = pitch;
    out->channels = 3;
    free(job->row);
    free(job);
    return 1;
}
struct encode_job {
    struct jpeg_compress_struct codec;
    struct cover_error error;
    unsigned char *bytes;
    unsigned long size;
};
int nx_cover_encode_jpeg(const unsigned char *pixels, int width, int height, int pitch,
                         unsigned char **out, size_t *size) {
    struct encode_job *job;
    if (!out || !size)
        return 0;
    *out = NULL;
    *size = 0;
    if (!pixels || width < 1 || height < 1 || width > 512 || height > 512 || pitch < width * 3)
        return 0;
    job = calloc(1, sizeof(*job));
    if (!job)
        return 0;
    job->codec.err = jpeg_std_error(&job->error.jpeg);
    job->error.jpeg.error_exit = cover_jpeg_error;
    job->error.jpeg.output_message = cover_jpeg_message;
    if (setjmp(job->error.jump)) {
        jpeg_destroy_compress(&job->codec);
        free(job->bytes);
        free(job);
        return 0;
    }
    jpeg_create_compress(&job->codec);
    jpeg_mem_dest(&job->codec, &job->bytes, &job->size);
    job->codec.image_width = width;
    job->codec.image_height = height;
    job->codec.input_components = 3;
    job->codec.in_color_space = JCS_RGB;
    jpeg_set_defaults(&job->codec);
    jpeg_set_quality(&job->codec, 85, TRUE);
    jpeg_start_compress(&job->codec, TRUE);
    while (job->codec.next_scanline < job->codec.image_height) {
        JSAMPROW row = (JSAMPROW)(pixels + (size_t)job->codec.next_scanline * pitch);
        jpeg_write_scanlines(&job->codec, &row, 1);
    }
    jpeg_finish_compress(&job->codec);
    jpeg_destroy_compress(&job->codec);
    if (job->size > 1024u * 1024u) {
        free(job->bytes);
        free(job);
        return 0;
    }
    *out = job->bytes;
    *size = (size_t)job->size;
    free(job);
    return 1;
}

struct png_job {
    png_structp png;
    png_infop info;
    jmp_buf jump;
    const unsigned char *bytes;
    size_t size, offset;
    unsigned char *pixels, *row;
};
static void cover_png_error(png_structp png, png_const_charp message) {
    struct png_job *job = png_get_error_ptr(png);
    (void)message;
    longjmp(job->jump, 1);
}
static void cover_png_warning(png_structp png, png_const_charp message) {
    (void)png;
    (void)message;
}
static void cover_png_read(png_structp png, png_bytep out, png_size_t count) {
    struct png_job *job = png_get_io_ptr(png);
    if (count > job->size - job->offset)
        png_error(png, "Truncated artwork");
    memcpy(out, job->bytes + job->offset, count);
    job->offset += count;
}
int nx_cover_decode_png(const unsigned char *bytes, size_t size, struct nx_cover_rgb *out) {
    struct png_job *job;
    png_uint_32 width, height;
    int type, depth, passes, step = 1, out_width, out_height, pitch;
    static const int xstart[7] = {0, 4, 0, 2, 0, 1, 0}, xstep[7] = {8, 8, 4, 4, 2, 2, 1};
    static const int ystart[7] = {0, 0, 4, 0, 2, 0, 1}, ystep[7] = {8, 8, 8, 4, 4, 2, 2};
    if (!out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!bytes || size < 24 || size > 16u * 1024u * 1024u)
        return 0;
    job = calloc(1, sizeof(*job));
    if (!job)
        return 0;
    job->bytes = bytes;
    job->size = size;
    if (setjmp(job->jump)) {
        png_destroy_read_struct(&job->png, &job->info, NULL);
        free(job->pixels);
        free(job->row);
        free(job);
        return 0;
    }
    job->png =
        png_create_read_struct(PNG_LIBPNG_VER_STRING, job, cover_png_error, cover_png_warning);
    if (!job->png) {
        free(job);
        return 0;
    }
    job->info = png_create_info_struct(job->png);
    if (!job->info)
        png_error(job->png, "No artwork header memory");
    png_set_read_fn(job->png, job, cover_png_read);
    png_set_user_limits(job->png, 4096, 4096);
    png_set_chunk_malloc_max(job->png, 1024 * 1024);
    png_set_chunk_cache_max(job->png, 16);
    png_read_info(job->png, job->info);
    width = png_get_image_width(job->png, job->info);
    height = png_get_image_height(job->png, job->info);
    if (!width || !height || width > 4096 || height > 4096)
        png_error(job->png, "Artwork dimensions out of range");
    type = png_get_color_type(job->png, job->info);
    depth = png_get_bit_depth(job->png, job->info);
    if (depth == 16)
        png_set_strip_16(job->png);
    if (type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(job->png);
    if (type == PNG_COLOR_TYPE_GRAY && depth < 8)
        png_set_expand_gray_1_2_4_to_8(job->png);
    if (png_get_valid(job->png, job->info, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(job->png);
    else if (!(type & PNG_COLOR_MASK_ALPHA))
        png_set_add_alpha(job->png, 255, PNG_FILLER_AFTER);
    if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(job->png);
    passes = png_set_interlace_handling(job->png);
    png_read_update_info(job->png, job->info);
    if (png_get_channels(job->png, job->info) != 4 ||
        png_get_rowbytes(job->png, job->info) != (size_t)width * 4)
        png_error(job->png, "Unsupported artwork pixels");
    while ((width + step - 1) / (unsigned)step > 1024 ||
           (height + step - 1) / (unsigned)step > 1024)
        step *= 2;
    out_width = (width + step - 1) / step;
    out_height = (height + step - 1) / step;
    pitch = out_width * 4;
    job->pixels = calloc((size_t)pitch, out_height);
    job->row = malloc((size_t)width * 4);
    if (!job->pixels || !job->row)
        png_error(job->png, "No artwork pixel memory");
    for (int pass = 0; pass < passes; ++pass)
        for (png_uint_32 y = 0; y < height; ++y) {
            memset(job->row, 0, (size_t)width * 4);
            png_read_row(job->png, job->row, NULL);
            if (y % (unsigned)step)
                continue;
            if (passes == 7 &&
                (y < (unsigned)ystart[pass] || (y - ystart[pass]) % (unsigned)ystep[pass]))
                continue;
            for (png_uint_32 x = 0; x < width; x += step) {
                if (passes == 7 &&
                    (x < (unsigned)xstart[pass] || (x - xstart[pass]) % (unsigned)xstep[pass]))
                    continue;
                memcpy(job->pixels + (size_t)(y / step) * pitch + (x / step) * 4,
                       job->row + (size_t)x * 4, 4);
            }
        }
    png_read_end(job->png, NULL);
    png_destroy_read_struct(&job->png, &job->info, NULL);
    out->pixels = job->pixels;
    out->width = out_width;
    out->height = out_height;
    out->pitch = pitch;
    out->channels = 4;
    free(job->row);
    free(job);
    return 1;
}
