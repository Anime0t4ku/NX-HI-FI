#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* RGB pixels are owned by the caller and freed with free(). */
struct nx_cover_rgb {
    unsigned char *pixels;
    int width, height, pitch, channels;
};
int nx_cover_decode_png(const unsigned char *bytes, size_t size, struct nx_cover_rgb *out);
int nx_cover_decode_jpeg(const unsigned char *bytes, size_t size, struct nx_cover_rgb *out);
int nx_cover_encode_jpeg(const unsigned char *pixels, int width, int height, int pitch,
                         unsigned char **out, size_t *size);
#ifdef __cplusplus
}
#endif
