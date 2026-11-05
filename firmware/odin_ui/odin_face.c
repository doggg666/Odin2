#include "odin_face.h"

#if LV_COLOR_DEPTH != 16 || LV_COLOR_16_SWAP
#error "The embedded Odin artwork requires native little-endian RGB565 pixels."
#endif

extern const uint8_t odin_face_pixels[] asm("_binary_odin_face_start");

/* The pixels are embedded in flash. No network fetch or JPEG decoder is needed. */
const lv_img_dsc_t odin_face_image = {
    .header.always_zero = 0,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .header.w = ODIN_FACE_WIDTH,
    .header.h = ODIN_FACE_HEIGHT,
    .data_size = ODIN_FACE_WIDTH * ODIN_FACE_HEIGHT * 2,
    .data = odin_face_pixels,
};
