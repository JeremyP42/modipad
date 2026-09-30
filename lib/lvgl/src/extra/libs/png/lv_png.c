/**
 * @file lv_png.c
 *
 */

/*********************
 *      INCLUDES
 *********************/
#include "../../../lvgl.h"
#if LV_USE_PNG

#include "lv_png.h"
#include "lodepng.h"
#include <stdlib.h>

/*********************
 *      DEFINES
 *********************/

/**********************
 *      TYPEDEFS
 **********************/

/**********************
 *  STATIC PROTOTYPES
 **********************/
static lv_res_t decoder_info(struct _lv_img_decoder_t * decoder, const void * src, lv_img_header_t * header);
static lv_res_t decoder_open(lv_img_decoder_t * dec, lv_img_decoder_dsc_t * dsc);
static void decoder_close(lv_img_decoder_t * dec, lv_img_decoder_dsc_t * dsc);
static void convert_color_depth(uint8_t * img, uint32_t px_cnt);
static bool png_has_alpha(const char * fn);
static void convert_color_depth_opaque(uint8_t * img, uint32_t px_cnt);

/**********************
 *  STATIC VARIABLES
 **********************/

/**********************
 *      MACROS
 **********************/

/**********************
 *   GLOBAL FUNCTIONS
 **********************/

/**
 * Register the PNG decoder functions in LVGL
 */
void lv_png_init(void)
{
    lv_img_decoder_t * dec = lv_img_decoder_create();
    lv_img_decoder_set_info_cb(dec, decoder_info);
    lv_img_decoder_set_open_cb(dec, decoder_open);
    lv_img_decoder_set_close_cb(dec, decoder_close);
}

/**********************
 *   STATIC FUNCTIONS
 **********************/

/**
 * Get info about a PNG image
 * @param src can be file name or pointer to a C array
 * @param header store the info here
 * @return LV_RES_OK: no error; LV_RES_INV: can't get the info
 */
static lv_res_t decoder_info(struct _lv_img_decoder_t * decoder, const void * src, lv_img_header_t * header)
{
    (void) decoder; /*Unused*/
    lv_img_src_t src_type = lv_img_src_get_type(src);          /*Get the source type*/

    /*If it's a PNG file...*/
    if(src_type == LV_IMG_SRC_FILE) {
        const char * fn = src;
        if(strcmp(lv_fs_get_ext(fn), "png") == 0) {              /*Check the extension*/

            /* Read the width and height from the file. They have a constant location:
             * [16..23]: width
             * [24..27]: height
             */
            uint32_t size[2];
            lv_fs_file_t f;
            lv_fs_res_t res = lv_fs_open(&f, fn, LV_FS_MODE_RD);
            if(res != LV_FS_RES_OK) return LV_RES_INV;

            lv_fs_seek(&f, 16, LV_FS_SEEK_SET);

            uint32_t rn;
            lv_fs_read(&f, &size, 8, &rn);
            lv_fs_close(&f);

            if(rn != 8) return LV_RES_INV;

            /*Save the data in the header*/
            header->always_zero = 0;
            /* Decode opaque PNGs as TRUE_COLOR (no per-pixel alpha blending,
             * 2 bytes/px) instead of always TRUE_COLOR_ALPHA. The stock LVGL
             * decoder always uses lodepng_decode32, so even a fully opaque
             * full-screen background was blended and stored with an alpha
             * channel on every draw. */
            header->cf = png_has_alpha(fn) ? LV_IMG_CF_TRUE_COLOR_ALPHA : LV_IMG_CF_TRUE_COLOR;
            /*The width and height are stored in Big endian format so convert them to little endian*/
            header->w = (lv_coord_t)((size[0] & 0xff000000) >> 24) + ((size[0] & 0x00ff0000) >> 8);
            header->h = (lv_coord_t)((size[1] & 0xff000000) >> 24) + ((size[1] & 0x00ff0000) >> 8);

            return LV_RES_OK;
        }
    }
    /*If it's a PNG file in a  C array...*/
    else if(src_type == LV_IMG_SRC_VARIABLE) {
        const lv_img_dsc_t * img_dsc = src;
        const uint32_t data_size = img_dsc->data_size;
        const uint32_t * size = ((uint32_t *)img_dsc->data) + 4;
        const uint8_t magic[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};
        if(data_size < sizeof(magic)) return LV_RES_INV;
        if(memcmp(magic, img_dsc->data, sizeof(magic))) return LV_RES_INV;
        header->always_zero = 0;

        if(img_dsc->header.cf) {
            header->cf = img_dsc->header.cf;       /*Save the color format*/
        }
        else {
            header->cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        }

        if(img_dsc->header.w) {
            header->w = img_dsc->header.w;         /*Save the image width*/
        }
        else {
            header->w = (lv_coord_t)((size[0] & 0xff000000) >> 24) + ((size[0] & 0x00ff0000) >> 8);
        }

        if(img_dsc->header.h) {
            header->h = img_dsc->header.h;         /*Save the color height*/
        }
        else {
            header->h = (lv_coord_t)((size[1] & 0xff000000) >> 24) + ((size[1] & 0x00ff0000) >> 8);
        }

        return LV_RES_OK;
    }

    return LV_RES_INV;         /*If didn't succeeded earlier then it's an error*/
}

/**
 * Open a PNG image and return the decided image
 * @param src can be file name or pointer to a C array
 * @param style style of the image object (unused now but certain formats might use it)
 * @return pointer to the decoded image or `LV_IMG_DECODER_OPEN_FAIL` if failed
 */
static lv_res_t decoder_open(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{

    (void) decoder; /*Unused*/
    uint32_t error;                 /*For the return values of PNG decoder functions*/

    uint8_t * img_data = NULL;

    /*If it's a PNG file...*/
    if(dsc->src_type == LV_IMG_SRC_FILE) {
        const char * fn = dsc->src;
        if(strcmp(lv_fs_get_ext(fn), "png") == 0) {              /*Check the extension*/

            /*Load the PNG file into buffer. It's still compressed (not decoded)*/
            unsigned char * png_data;      /*Pointer to the loaded data. Same as the original file just loaded into the RAM*/
            size_t png_data_size;          /*Size of `png_data` in bytes*/

            error = lodepng_load_file(&png_data, &png_data_size, fn);   /*Load the file*/
            if(error) {
                LV_LOG_WARN("error %" LV_PRIu32 ": %s\n", error, lodepng_error_text(error));
                return LV_RES_INV;
            }

            /*Decode the PNG image*/
            unsigned png_width;             /*Will be the width of the decoded image*/
            unsigned png_height;            /*Will be the height of the decoded image*/

#if LV_COLOR_DEPTH == 16
            bool opaque = !png_has_alpha(fn);
#else
            bool opaque = false;
#endif
            if(opaque) {
                /*RGB888, no alpha channel needed*/
                error = lodepng_decode24(&img_data, &png_width, &png_height, png_data, png_data_size);
            }
            else {
                /*Decode the loaded image in ARGB8888 */
                error = lodepng_decode32(&img_data, &png_width, &png_height, png_data, png_data_size);
            }
            lv_mem_free(png_data); /*Free the loaded file*/
            if(error) {
                if(img_data != NULL) {
                    lv_mem_free(img_data);
                }
                LV_LOG_WARN("error %" LV_PRIu32 ": %s\n", error, lodepng_error_text(error));
                return LV_RES_INV;
            }

            /*Convert the image to the system's color depth*/
            if(opaque) {
                convert_color_depth_opaque(img_data, png_width * png_height);
            }
            else {
                convert_color_depth(img_data,  png_width * png_height);
            }
            dsc->img_data = img_data;
            return LV_RES_OK;     /*The image is fully decoded. Return with its pointer*/
        }
    }
    /*If it's a PNG file in a  C array...*/
    else if(dsc->src_type == LV_IMG_SRC_VARIABLE) {
        const lv_img_dsc_t * img_dsc = dsc->src;
        unsigned png_width;             /*No used, just required by he decoder*/
        unsigned png_height;            /*No used, just required by he decoder*/

        /*Decode the image in ARGB8888 */
        error = lodepng_decode32(&img_data, &png_width, &png_height, img_dsc->data, img_dsc->data_size);

        if(error) {
            if(img_data != NULL) {
                lv_mem_free(img_data);
            }
            return LV_RES_INV;
        }

        /*Convert the image to the system's color depth*/
        convert_color_depth(img_data,  png_width * png_height);

        dsc->img_data = img_data;
        return LV_RES_OK;     /*Return with its pointer*/
    }

    return LV_RES_INV;    /*If not returned earlier then it failed*/
}

/**
 * Free the allocated resources
 */
static void decoder_close(lv_img_decoder_t * decoder, lv_img_decoder_dsc_t * dsc)
{
    LV_UNUSED(decoder); /*Unused*/
    if(dsc->img_data) {
        lv_mem_free((uint8_t *)dsc->img_data);
        dsc->img_data = NULL;
    }
}

/**
 * If the display is not in 32 bit format (ARGB888) then covert the image to the current color depth
 * @param img the ARGB888 image
 * @param px_cnt number of pixels in `img`
 */
static void convert_color_depth(uint8_t * img, uint32_t px_cnt)
{
#if LV_COLOR_DEPTH == 32
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    lv_color_t * img_c = (lv_color_t *) img;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img_c[i].ch.red = c.ch.blue;
        img_c[i].ch.blue = c.ch.red;
    }
#elif LV_COLOR_DEPTH == 16
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.blue, img_argb[i].ch.green, img_argb[i].ch.red);
        img[i * 3 + 2] = img_argb[i].ch.alpha;
        img[i * 3 + 1] = c.full >> 8;
        img[i * 3 + 0] = c.full & 0xFF;
    }
#elif LV_COLOR_DEPTH == 8
    lv_color32_t * img_argb = (lv_color32_t *)img;
    lv_color_t c;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        c = lv_color_make(img_argb[i].ch.red, img_argb[i].ch.green, img_argb[i].ch.blue);
        img[i * 2 + 1] = img_argb[i].ch.alpha;
        img[i * 2 + 0] = c.full;
    }
#elif LV_COLOR_DEPTH == 1
    lv_color32_t * img_argb = (lv_color32_t *)img;
    uint8_t b;
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        b = img_argb[i].ch.red | img_argb[i].ch.green | img_argb[i].ch.blue;
        img[i * 2 + 1] = img_argb[i].ch.alpha;
        img[i * 2 + 0] = b > 128 ? 1 : 0;
    }
#endif
}

/**
 * Return true if the PNG file actually uses an alpha channel.
 * Opaque images (color type 0/2, or palette without a usable tRNS) are decoded
 * without alpha so they are drawn without per-pixel blending and stored at 2
 * bytes/px instead of 3.
 */
static bool png_has_alpha(const char * fn)
{
    lv_fs_file_t f;
    if(lv_fs_open(&f, fn, LV_FS_MODE_RD) != LV_FS_RES_OK) return true;

    uint8_t hdr[33]; /* signature(8) + IHDR len/type(8) + IHDR data(13) + crc(4) */
    uint32_t rn = 0;
    lv_fs_read(&f, hdr, sizeof(hdr), &rn);
    if(rn < sizeof(hdr)) {
        lv_fs_close(&f);
        return true;
    }

    uint8_t color_type = hdr[25]; /* IHDR data: bitdepth at 24, colortype at 25 */
    bool has_alpha = (color_type == 4 || color_type == 6);

    if(color_type == 3) {
        /* Palette image: transparency only via a tRNS chunk (which must come
         * before IDAT). If absent or fully opaque, treat as opaque. */
        while(1) {
            uint8_t ch[8];
            lv_fs_read(&f, ch, sizeof(ch), &rn);
            if(rn != sizeof(ch)) break;

            uint32_t len = ((uint32_t)ch[0] << 24) | ((uint32_t)ch[1] << 16) |
                           ((uint32_t)ch[2] << 8) | (uint32_t)ch[3];
            if(memcmp(&ch[4], "IDAT", 4) == 0 || memcmp(&ch[4], "IEND", 4) == 0) break;

            if(memcmp(&ch[4], "tRNS", 4) == 0) {
                uint8_t buf[256];
                uint32_t toread = (len < sizeof(buf)) ? len : (uint32_t)sizeof(buf);
                lv_fs_read(&f, buf, toread, &rn);
                for(uint32_t i = 0; i < rn; i++) {
                    if(buf[i] != 0xFF) has_alpha = true;
                }
                break;
            }
            else {
                lv_fs_seek(&f, (uint32_t)(len + 4), LV_FS_SEEK_CUR); /* skip data + CRC */
            }
        }
    }

    lv_fs_close(&f);
    return has_alpha;
}

#if LV_COLOR_DEPTH == 16
/**
 * Convert an opaque RGB888 image (3 bytes/px) to RGB565 (2 bytes/px) in place.
 * Output indices are always <= input indices, so a forward copy is safe.
 */
static void convert_color_depth_opaque(uint8_t * img, uint32_t px_cnt)
{
    uint32_t i;
    for(i = 0; i < px_cnt; i++) {
        lv_color_t c = lv_color_make(img[i * 3 + 0], img[i * 3 + 1], img[i * 3 + 2]);
        img[i * 2 + 0] = c.full & 0xFF;
        img[i * 2 + 1] = c.full >> 8;
    }
}
#else
static void convert_color_depth_opaque(uint8_t * img, uint32_t px_cnt)
{
    LV_UNUSED(img);
    LV_UNUSED(px_cnt);
}
#endif

#endif /*LV_USE_PNG*/
