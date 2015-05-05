/*
 * Jessu.cpp
 *
 * $Id: jessu.cpp,v 1.63 2004/03/28 02:56:12 lk Exp $
 *
 */

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

#include "resource.h"
#include "fileread.h"
#include "loaddir.h"
#include "scaletile.h"
#include "config.h"
#include "benchmark.h"
#include "text.hpp"
#include "jessu.h"
#include "graphics.hpp"

#if !USE_D3D
#  include <GL/gl.h>
#endif

// if RELEASE_QUALITY is set to 1, then we have some checks to make sure
// that we're optimizing, not in purify mode, not printing anything out, etc.
#define RELEASE_QUALITY         0

// in purify mode, keys and mouse moves don't quit the screen saver.  this
// makes it easier to keep a purify window on top of jessu.  the only
// way to get out is to click with the mouse.  It also affects some other
// parameters.
#define PURIFY_MODE             0

#define FADE_IN_SECONDS         1.5
#define FADE_OUT_SECONDS        1.5
#if PURIFY_MODE
#define TOTAL_SECONDS           10
#else
#define TOTAL_SECONDS           10
#endif
#define OVERLAP_SECONDS         2

#if TV_VIEWING
#define FILENAME_HEIGHT         80
#define FILENAME_BOTTOM_MARGIN  70
#else
#define FILENAME_HEIGHT         45
#define FILENAME_BOTTOM_MARGIN  90
#endif

#define NOTE_WIDTH              180     // width of window
#define NOTE_INTERNAL_MARGIN    20      // from text to edge of window
#define NOTE_PREVIEW_MARGIN     5       // left and right only
#define NOTE_EXTERNAL_MARGIN    100     // distance from edge of screen

#define ERROR_MOVE_TIME         10      // seconds before moving error window

#define USE_SMALL_WINDOW        0       // small normal window for testing

#if PURIFY_MODE || USE_SMALL_WINDOW
#define ALLOW_TOPMOST           0
#else
#define ALLOW_TOPMOST           1
#endif

#define OUTPUT_DEBUG_FILE       1
#define ALWAYS_PRINT_DEBUGGING  1           // even if switch is not given
#define PER_FRAME_OUTPUT        0           // verbose!
#define MALLOC_DEBUGGING        0
#define OUTPUT_FILENAME         "\\jessu.log"

#define EVAL_MAX_IMAGES         10

#define BLANK_OTHER_MONITORS    0
#define IGNORE_MOUSE_MOTION     0

// release checks

#if RELEASE_QUALITY && PURIFY_MODE
#error Turn off Purify mode for release
#endif
#if RELEASE_QUALITY && !OUTPUT_DEBUG_FILE
#error Turn on output file for release
#endif
#if RELEASE_QUALITY && DEBUG
#error Turn off debug in Makefile for release
#endif
#if RELEASE_QUALITY && EVAL_MAX_IMAGES != 10
#error Set EVAL_MAX_IMAGES sensibly for release
#endif
#if RELEASE_QUALITY && ALWAYS_PRINT_DEBUGGING
#error Turn off always print debugging for release
#endif
#if RELEASE_QUALITY && MALLOC_DEBUGGING
#error Turn off malloc debugging for release
#endif
#if RELEASE_QUALITY && BLANK_OTHER_MONITORS
#error Blanking other monitors not working yet.
#endif
#if RELEASE_QUALITY && USE_SMALL_WINDOW
#error Don't use small window for release
#endif
#if RELEASE_QUALITY && IGNORE_MOUSE_MOTION
#error Don't ignore mouse motion for release
#endif
#if RELEASE_QUALITY && PER_FRAME_OUTPUT
#error Turn off per frame output for release
#endif

struct SLIDE_INFO {
    /*
     * The "texture_ready" flag tells the GL thread that the texture
     * tiles are ready to be used.  It should then set "texture_used",
     * send the texture to the board, and reset both when it is done.
     */
    int texture_ready;

    /*
     * The "texture_used" flag marks whether the texture bits are
     * being used by the graphics thread.  This flag is true while
     * the texture is being downloaded to graphics memory.  If it
     * is "false" (and "texture_ready" is false) then the worker
     * thread is welcome to fill it with the next image.
     */
    int texture_used;

    /*
     * The "time_to_start" flag marks that this slide has reached
     * the time to start displaying.  It also needs to wait for
     * "texture_downloaded" to be true before it can actually start
     * displaying.
     */
    int time_to_start;

    /*
     * The "texture_downloaded" flag means that the texture has been
     * sent to the graphics board.  Once that's done then we're waiting
     * for "time_to_start" before we can display the slide.
     */
    int texture_downloaded;

    /*
     * The "being_displayed" flag marks whether the slide is being
     * displayed.  The "time" field represents the time when this
     * flag was set to true.
     */
    int being_displayed;

    /* This is the nice filename that's displayed if the user presses "f" */
    char beautiful_filename[MAX_PATH];
    char next_beautiful_filename[MAX_PATH];

    Filename_notice *filename_notice;

    /* Width and height of the image on disk (in "tile") */
    int width, height;

    /* Total number of seconds being displayed (from slideshow file) */
    double total_seconds;

    /* Other data about this image (thumbnail, etc.) */
    MISC_INFO *misc_info;
    MISC_INFO *next_misc_info;    // of the next image

    /* ratio to display (in texture memory), width/height */
    float ratio;

    /* start values: */
    DWORD time;         /* in milliseconds, from timeGetTime() */
    long delta_time;    /* when paused */
    double scale;
    double x, y;

    /* change per second to be applied to start values: */
    double dscale;
    double dx, dy;

    /* texture tiles */
    unsigned char **tile;  /* row-major order */
    int tile_number;       /* number of tiles that have been downloaded */

#if USE_D3D
    /* This is an array of textures for Direct3D */
    IDirect3DTexture8 **textures;
#else
    unsigned int *texture_id;
#endif

    SLIDE_INFO() {
        filename_notice = NULL;
    }

    ~SLIDE_INFO() {
        delete filename_notice;
    }
};

static SLIDE_INFO slide[2];

static int g_worker_thread_should_quit;

static char *directory = NULL;

static int tile_count;
static int tile_count_x;
static int tile_count_y;
static int tile_size_x;
static int tile_size_y;
static int texture_size_x;
static int texture_size_y;

static float tile_edge_texel_low_bias;
static float tile_edge_texel_high_bias;

static int g_frame_count = 0;
static bool g_show_lines = false;

static bool g_received_first_mousemove = false;

#if !USE_D3D
static GLenum texture_wrap_mode;
#endif

#if USE_D3D
static D3DFORMAT preferred_texture_internal_format = D3DFMT_A8R8G8B8;
#else
/*
 * One possibility is that a "honor display driver OpenGL settings"
 * toggle might set this to GL_RGB, in which case the OpenGL driver
 * can choose a low or high-quality format based on the display
 * control panel settings.  The default in the control panel is usually
 * to choose the faster internal format.
 */
static GLenum preferred_texture_internal_format = GL_RGB8;
#endif

static bool in_screensaver;
static bool in_slideshow;
bool in_fullscreen;   // in_screensaver || in_slideshow
static char *slideshow_file = NULL;
static bool do_benchmark = false;

static int loading_jpeg;
static int scaling_image;
static int downloading_texture;
int loading_jpeg_progress;
int scaling_image_progress;
int downloading_texture_progress;

static int paused = false;
static double paused_delta_y;
static double actual_paused_delta_y;
static float speed = 1;

static bool display_filename = false;
static char base_directory[MAX_PATH];

#if ALWAYS_PRINT_DEBUGGING
int print_debugging = 1;
#else
int print_debugging = 0;
#endif
int display_debugging = 0;

unsigned int seed;

FILE *debug_output;

static char *jessu_className = "Jessu Screensaver";
static HWND g_parent_window;
static HWND rendering_window;
static int window_width;
static int window_height;

#if !USE_D3D
static HDC gl_device;
static HGLRC gl_context = NULL;
#endif

static bool quit_requested = false;
static bool paint_scheduled = false;

static char *error_message = NULL;

static int cursor_is_shown = true;

static void schedule_paint(void);

void
set_error_message(char const *error)
{
    free(error_message);

    if (error == NULL) {
        error_message = NULL;
    } else {
        error_message = strdup(error);
    }
}

static void
show_cursor(void)
{
    if (!cursor_is_shown) {
        ShowCursor(1);
        cursor_is_shown = true;
    }
}

static void
hide_cursor(void)
{
    if (cursor_is_shown) {
        ShowCursor(0);
        cursor_is_shown = false;
    }
}

#if !USE_D3D
static void cleanup_gl()
{
    if (gl_context != NULL) {
        wglDeleteContext(gl_context);
        gl_context = NULL;

        ReleaseDC(rendering_window, gl_device);
        gl_device = NULL;
    }
}
#endif

static void
probe_rendering_capabilities(void)
{
#if USE_D3D
    if (g_pD3D == NULL) {
        return;
    }

    // Nothing yet to probe from D3D
    tile_edge_texel_low_bias = TILE_SHRINK;
    tile_edge_texel_high_bias = TILE_SHRINK;

#else
    if (gl_context == NULL) {
        return;
    }

    int gl_major_version, gl_minor_version;
    sscanf((const char *)glGetString(GL_VERSION), "%d.%d",
            &gl_major_version, &gl_minor_version);

    const char *gl_extensions = (const char *)glGetString(GL_EXTENSIONS);

    bool has_SGIS_texture_edge_clamp;
    has_SGIS_texture_edge_clamp =
        strstr(gl_extensions, "GL_SGIS_texture_edge_clamp") != NULL;

    /*
     * SGI's OpenGL extension registry at
     * http://oss.sgi.com/projects/ogl-sample/registry/
     * claims that the extension is "SGIS" but ATI's EXTENSIONS string
     * returns something with an "SGI" (no "S").  Whatever; assume they
     * meant SGIS and pick it up anyway.
     */
    bool has_SGI_texture_edge_clamp;
    has_SGI_texture_edge_clamp =
        strstr(gl_extensions, "GL_SGI_texture_edge_clamp") != NULL;

    bool has_clamp_to_edge;
    has_clamp_to_edge = gl_minor_version > 1 ||
                        has_SGIS_texture_edge_clamp ||
                        has_SGI_texture_edge_clamp;

    if (has_clamp_to_edge) {

        /* If has clamp, don't do texel edge hack, just use clamp-to-edge. */

        tile_edge_texel_low_bias = 0.0f;
        tile_edge_texel_high_bias = 0.0f;

        /*
         * Since Microsoft's headers in VC 6.0 don't have GL_CLAMP_TO_EDGE,
         * just use the value.
         */
        texture_wrap_mode = 0x812f; // GL_CLAMP_TO_EDGE

    } else {

        /*
         * Include texel edge bias to cover up black lines caused
         * by rasterizers that apparently rasterize slightly off
         * spec.  Pray it doesn't show up too obviously in pictures.
         * Low-resolution screens may show a discontinuity between
         * tiles.
         */

        tile_edge_texel_low_bias = 0.5f;
        tile_edge_texel_high_bias = 0.5f;

        texture_wrap_mode = GL_CLAMP;

    }

    if (print_debugging) {
        jessu_printf(THREAD_GL, "has_clamp_to_edge: %d", has_clamp_to_edge);
        jessu_printf(THREAD_GL,
                "tile_edge_texel_low_bias: %g", tile_edge_texel_low_bias);
        jessu_printf(THREAD_GL,
                "tile_edge_texel_high_bias: %g", tile_edge_texel_high_bias);
        jessu_printf(THREAD_GL, "texture_wrap_mode: %x", texture_wrap_mode);
    }
#endif
}

static void
set_up_textures(int small_window, int use_less_memory)
{
#if USE_D3D
    if (g_pD3D == NULL) {
        return;
    }
#else
    if (gl_context == NULL) {
        return;
    }
#endif

    if (small_window) {
        tile_size_x = 128;
        tile_size_y = 128;
        texture_size_x = 128;
        texture_size_y = 128;
    } else {
        // Direct3D tips says that 256x256 textures are the fastest:
        tile_size_x = 256;
        tile_size_y = 256;
        if (use_less_memory) {
            // uses 1/8 as much memory
            texture_size_x = 512;
            texture_size_y = 512;
        } else {
            texture_size_x = 1024;
            texture_size_y = 1024;
        }
    }

#if USE_D3D
    D3DCAPS8 d3dCaps;
    g_pd3dDevice->GetDeviceCaps( &d3dCaps );

    jessu_printf(THREAD_GL, "Max texture size: %dx%d\n",
            (int)d3dCaps.MaxTextureWidth,
            (int)d3dCaps.MaxTextureHeight);

    while (tile_size_x > 64 && tile_size_x > (int)d3dCaps.MaxTextureWidth) {
        tile_size_x /= 2;
    }
    while (tile_size_y > 64 && tile_size_y > (int)d3dCaps.MaxTextureHeight) {
        tile_size_y /= 2;
    }
#endif

    tile_count_x = texture_size_x/tile_size_x;
    tile_count_y = texture_size_y/tile_size_y;
    tile_count = tile_count_x*tile_count_y;

    for (int i = 0; i < 2; i++) {
        slide[i].tile = (unsigned char **)jessu_malloc(THREAD_GL,
                tile_count*sizeof(unsigned char *), "tile pointers");

#if USE_D3D
        slide[i].textures = (IDirect3DTexture8 **)jessu_malloc(THREAD_GL,
                tile_count*sizeof(IDirect3DTexture8 *), "tile textures");
#else
        slide[i].texture_id = (unsigned int *)jessu_malloc(THREAD_GL,
                tile_count*sizeof(unsigned int), "tile texture id");
        glGenTextures(tile_count, slide[i].texture_id);
#endif

        for (int j = 0; j < tile_count; j++) {
            slide[i].tile[j] = (unsigned char *)jessu_malloc(THREAD_GL,
                    tile_size_x*tile_size_y*BYTES_PER_TEXEL, "tile data");

#if USE_D3D
            HRESULT result = g_pd3dDevice->CreateTexture(tile_size_x,
                    tile_size_y, 1, 0, preferred_texture_internal_format,
                    D3DPOOL_MANAGED, &slide[i].textures[j]);
            if (FAILED(result)) {
                jessu_printf(THREAD_GL, "CreateTexture failed (%d)",
                        result & 0xffff);
                set_error_message("Cannot create textures");
                cleanup_d3d();
                return;
            }
#else
            glBindTexture(GL_TEXTURE_2D, slide[i].texture_id[j]);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                    texture_wrap_mode);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    texture_wrap_mode);
            /* we're not doing mipmapping */

            /* glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
               GL_LINEAR_MIPMAP_LINEAR); */
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#endif
        }
    }
}

static double
random_in_range(double low, double high)
{
    double x;

    x = rand()/(double)RAND_MAX;

    return low + x*(high - low);
}

static void
make_beautiful_filename(char *in, char *out, int out_len)
{
    int len = 0;
    char *last_element = NULL;

    if (strncmp(in, base_directory, strlen(base_directory)) != 0) {
        // we don't know where it is, skip it, maybe it's the jessu
        // limit-reached image
        *out = '\0';
        return;
    }

    // remove base directory, it's not descriptive of the picture
    in += strlen(base_directory);
    while (*in == '/' || *in == '\\') {
        in++;
    }

    // replace slashes with commas and underscores with spaces
    while (*in != '\0' && len < out_len - 2) {
        if (*in == '/' || *in == '\\') {
            last_element = out + len;
            out[len++] = ',';
            out[len++] = ' ';
        } else if (*in == '_') {
            out[len++] = ' ';
        } else {
            out[len++] = *in;
        }

        in++;
    }

    out[len] = '\0';

    if (len >= 4 && _stricmp(out + len - 4, ".jpg") == 0) {
        // remove .jpg suffix
        out[len - 4] = '\0';
    }

    // remove trailing component if it starts with "dcp" or "pict"
    if (last_element != NULL) {
        if (strnicmp(last_element, ", dcp", 5) == 0) {
            *last_element = '\0';
        } else if (strnicmp(last_element, ", pict", 6) == 0 &&
                isdigit(last_element[6])) {

            *last_element = '\0';
        }
    }
}

static void
load_next_picture(Vertical_scaler &vertical_scaler, SLIDE_INFO *info)
{
try_next_picture:
    char *filename = get_next_filename(&info->next_misc_info);

    if (print_debugging) {
        char *s = strrchr(filename, '\\');
        if (s != NULL) {
            s++;
        } else {
            s = filename;
        }
        jessu_printf(THREAD_WORKER, "loading \"%s\"", s);
    }

    make_beautiful_filename(filename, info->next_beautiful_filename,
            sizeof(info->next_beautiful_filename));
    jessu_printf(THREAD_WORKER, "beauty: \"%s\"",
            info->next_beautiful_filename);

    FILE *imgFile = fopen(filename, "rb");
    if (imgFile == NULL) {
        jessu_printf(THREAD_WORKER, "Can't open \"%s\" for reading",
                filename);
        _sleep(100);
        goto try_next_picture;
    }

    if (!read_image(filename, imgFile, vertical_scaler,
                &info->width, &info->height)) {

        jessu_printf(THREAD_WORKER, "Couldn't load an image from \"%s\"",
                filename);
        fclose(imgFile);
        _sleep(100);
        goto try_next_picture;
    }

    fclose(imgFile);

    jessu_printf(THREAD_WORKER, "%d by %d", info->width, info->height);
}

void
start_slide(int i)
{
    double startx, starty;
    double endx, endy;
    double startscale, endscale;

    jessu_printf(THREAD_GL, "starting slide %d", i);

    slide[i].time = timeGetTime();

    /* x = 0 is left, y = 0 is bottom */

    if (i == 0) {
        /* zoom in */
        if (slide[i].ratio < 1.0) {
            // vertical image, more motion, zoom into top half (face)
            startx = random_in_range(0.45, 0.55);
            starty = random_in_range(0.35, 0.65);
            endx = random_in_range(0.45, 0.55);
            endy = random_in_range(0.50, 0.70);
            startscale = 0.8;
            endscale = 1.0;
        } else {
            startx = 0.5;
            starty = 0.5;
            endx = random_in_range(0.45, 0.55);
            endy = random_in_range(0.45, 0.55);
            startscale = 1.0;
            endscale = 1.2;
        }
    } else {
        /* zoom out */
        if (slide[i].ratio < 1.0) {
            // vertical image, more motion, zoom out of top half (face)
            startx = random_in_range(0.45, 0.55);
            starty = random_in_range(0.50, 0.70);
            endx = random_in_range(0.45, 0.55);
            endy = random_in_range(0.35, 0.65);
            startscale = 1.0;
            endscale = 0.8;
        } else {
            startx = random_in_range(0.45, 0.55);
            starty = random_in_range(0.45, 0.55);
            endx = 0.5;
            endy = 0.5;
            startscale = 1.2;
            endscale = 1.0;
        }
    }

    if (slide[i].misc_info == NULL || slide[i].misc_info->seconds == 0) {
        slide[i].total_seconds = TOTAL_SECONDS;
    } else {
        slide[i].total_seconds = slide[i].misc_info->seconds;
    }

    slide[i].x = startx;
    slide[i].y = starty;
    slide[i].dx = (endx - startx)/slide[i].total_seconds;
    slide[i].dy = (endy - starty)/slide[i].total_seconds;
    slide[i].scale = startscale;
    slide[i].dscale = (endscale - startscale)/slide[i].total_seconds;

    slide[i].being_displayed = 1;
}

void
end_of_slide(int i)
{
    SLIDE_INFO *info;

    jessu_printf(THREAD_GL, "end of slide %d", i);

    info = &slide[i];

    info->being_displayed = 0;
    info->time_to_start = 0;
    info->texture_downloaded = 0;

    if (in_fullscreen) {
        hide_cursor();  // in case it was turned on with mouse movement
    }
}

void
display_slide(int i)
{
    double seconds;
    double dissolve;
    double x, y, scale;
    SLIDE_INFO *info;

    info = &slide[i];

    if (!info->being_displayed) {
        return;
    }

    if (paused) {
        dissolve = 1;
        seconds = 4;

        actual_paused_delta_y += (paused_delta_y - actual_paused_delta_y)/10;

        x = 0.5;
        y = 0.5 + actual_paused_delta_y;
        scale = 1.0;
    } else {
        seconds = (timeGetTime() - info->time)/1000.0*speed;

        if (seconds > slide[i].total_seconds) {
            end_of_slide(i);
            return;
        }

        if (seconds > slide[i].total_seconds - OVERLAP_SECONDS) {
            slide[1 - i].time_to_start = 1;
        }

        if (seconds < FADE_IN_SECONDS) {
            dissolve = seconds/FADE_IN_SECONDS;
        } else if (seconds > slide[i].total_seconds - FADE_OUT_SECONDS) {
            dissolve = (slide[i].total_seconds - seconds)/FADE_OUT_SECONDS;
        } else {
            dissolve = 1;
        }

        if (dissolve < 0) dissolve = 0;
        if (dissolve > 1) dissolve = 1;

        x = info->x + info->dx*seconds;
        y = info->y + info->dy*seconds;
        scale = info->scale + info->dscale*seconds;
    }

#if PER_FRAME_OUTPUT
    jessu_printf(THREAD_GL, "painting slide %d, dissolve %.2g, scale %.4g",
            i, dissolve, scale);
#endif

    // we used to disable blend if dissolve was greater than 0.99,
    // but it turns out we need it all the time so that the edges
    // of the picture don't wobble.
#if USE_D3D
    g_pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
#else
    glEnable(GL_BLEND);
#endif

#if USE_D3D
    // D3D: put these 4 matrix operations together into one matrix and
    // load into D3D_VIEW?

    D3DXMATRIX slideTrans, m1, m2, m3;
    D3DXMatrixTranslation(&m1, 0.5f, 0.5f, 0.5f);
    D3DXMatrixScaling(&m2, (float)scale, (float)scale/info->ratio, 0.0f);
    D3DXMatrixTranslation(&m3, (float)-x, (float)-y, 0.0f);
    slideTrans = m3 * m2 * m1;
    g_pd3dDevice->SetTransform(D3DTS_VIEW, &slideTrans);

    int tx, ty;
    float x1, y1, x2, y2;
    int j;

    j = 0;
    for (ty = 0; ty < tile_count_y; ty++) {
        for (tx = 0; tx < tile_count_x; tx++) {
            // D3D: SetTexture(0, textureptr)
            g_pd3dDevice->SetTexture(0, info->textures[j]);

#if 0
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_ADDRESSU, D3DTADDRESS_CLAMP);
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_ADDRESSV, D3DTADDRESS_CLAMP);
#else
            // this will catch problems where we run off the edge
            // of the texture.  This shouldn't happen as long
            // as TILE_SHRINK >= 0.5.
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_ADDRESSU, D3DTADDRESS_BORDER);
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_ADDRESSV, D3DTADDRESS_BORDER);
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_BORDERCOLOR, 0x0000FF00);  // green
#endif
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_MINFILTER, D3DTEXF_LINEAR);
            g_pd3dDevice->SetTextureStageState(0,
                    //D3DTSS_COLOROP, D3DTOP_MODULATE);
                    D3DTSS_COLOROP, D3DTOP_SELECTARG1); // arg2 always white
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_COLORARG1, D3DTA_TEXTURE);
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_COLORARG2, D3DTA_DIFFUSE);   // always white
#if 0
            // the D3D docs say "Setting the alpha operation to D3DTOP_DISABLE
            // when color blending is enabled causes undefined behavior."
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_ALPHAOP, D3DTOP_DISABLE);
#else
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_ALPHAOP, D3DTOP_MODULATE);
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
            g_pd3dDevice->SetTextureStageState(0,
                    D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
#endif

            x1 = tx/(float)tile_count_x;
            y1 = (tile_count_y - 1 - ty)/(float)tile_count_y;
            x2 = (tx + 1)/(float)tile_count_x;
            y2 = (tile_count_y - 1 - ty + 1)/(float)tile_count_y;

            // D3D: Create vertex array
            textured_colored_2d_vertex vertices[4];

            vertices[0].Color4f(1.0f, 1.0f, 1.0f, (float)dissolve);
            vertices[0].TexCoord2f(0 + tile_edge_texel_low_bias / tile_size_x,
                    1 - tile_edge_texel_high_bias / tile_size_y);
            vertices[0].Vertex2f(x1, y1);

            vertices[1].Color4f(1.0f, 1.0f, 1.0f, (float)dissolve);
            vertices[1].TexCoord2f(1 - tile_edge_texel_high_bias / tile_size_x,
                    1 - tile_edge_texel_high_bias / tile_size_y);
            vertices[1].Vertex2f(x2, y1);

            vertices[2].Color4f(1.0f, 1.0f, 1.0f, (float)dissolve);
            vertices[2].TexCoord2f(1 - tile_edge_texel_high_bias / tile_size_x,
                    0 + tile_edge_texel_low_bias / tile_size_y);
            vertices[2].Vertex2f(x2, y2);

            vertices[3].Color4f(1.0f, 1.0f, 1.0f, (float)dissolve);
            vertices[3].TexCoord2f(0 + tile_edge_texel_low_bias / tile_size_x,
                    0 + tile_edge_texel_low_bias / tile_size_y);
            vertices[3].Vertex2f(x1, y2);

            // copy our vertices to the vertex buffer
            VOID* pVertices;
            if (FAILED(g_pVB->Lock(0, 0, (BYTE **)&pVertices, 0))) {
                jessu_printf(THREAD_GL, "Cannot lock vertex structure (%d,%d)",
                        tx, ty);
                continue;
            }

            memmove(pVertices, vertices, sizeof(vertices));
            g_pVB->Unlock();

            g_pd3dDevice->SetStreamSource(0, g_pVB,
                    sizeof(textured_colored_2d_vertex));
            g_pd3dDevice->SetVertexShader(D3DFVF_CUSTOMVERTEX);
            g_pd3dDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0, 2);

            j++;
        }
    }

    if (g_show_lines) {
        for (ty = 0; ty < tile_count_y; ty++) {
            for (tx = 0; tx < tile_count_x; tx++) {
                x1 = tx/(float)tile_count_x;
                y1 = (tile_count_y - 1 - ty)/(float)tile_count_y;
                x2 = (tx + 1)/(float)tile_count_x;
                y2 = (tile_count_y - 1 - ty + 1)/(float)tile_count_y;

                // D3D: Create vertex array
                textured_colored_2d_vertex vertices[4];

                vertices[0].Color4f(1.0f, 1.0f, 1.0f, (float)dissolve);
                vertices[0].TexCoord2f(0 + tile_edge_texel_low_bias / tile_size_x,
                        1 - tile_edge_texel_high_bias / tile_size_y);
                vertices[0].Vertex2f(x1, y1);

                vertices[1].Color4f(1.0f, 1.0f, 1.0f, (float)dissolve);
                vertices[1].TexCoord2f(1 - tile_edge_texel_high_bias / tile_size_x,
                        1 - tile_edge_texel_high_bias / tile_size_y);
                vertices[1].Vertex2f(x2, y1);

                vertices[2].Color4f(1.0f, 1.0f, 1.0f, (float)dissolve);
                vertices[2].TexCoord2f(1 - tile_edge_texel_high_bias / tile_size_x,
                        0 + tile_edge_texel_low_bias / tile_size_y);
                vertices[2].Vertex2f(x2, y2);

                vertices[3].Color4f(1.0f, 1.0f, 1.0f, (float)dissolve);
                vertices[3].TexCoord2f(0 + tile_edge_texel_low_bias / tile_size_x,
                        0 + tile_edge_texel_low_bias / tile_size_y);
                vertices[3].Vertex2f(x1, y2);

                // copy our vertices to the vertex buffer
                VOID* pVertices;
                if (FAILED(g_pVB->Lock(0, 0, (BYTE **)&pVertices, 0))) {
                    jessu_printf(THREAD_GL, "Cannot lock vertex structure (%d,%d)",
                            tx, ty);
                    continue;
                }

                memmove(pVertices, vertices, sizeof(vertices));
                g_pVB->Unlock();

                g_pd3dDevice->SetStreamSource(0, g_pVB,
                        sizeof(textured_colored_2d_vertex));
                g_pd3dDevice->SetVertexShader(D3DFVF_CUSTOMVERTEX);
                g_pd3dDevice->SetTexture(0, NULL);
                g_pd3dDevice->DrawPrimitive(D3DPT_LINESTRIP, 0, 2);
            }
        }
    }
#else
    glColor4f(1.0f, 1.0f, 1.0f, (float)dissolve);

    glPushMatrix();
        glTranslatef(0.5f, 0.5f, 0.0f);
        glScalef(scale, scale/info->ratio, scale);
        glTranslatef(-x, -y, 0.0f);

        int tx, ty;
        float x1, y1, x2, y2;
        int j;

        j = 0;
        for (ty = 0; ty < tile_count_y; ty++) {
            for (tx = 0; tx < tile_count_x; tx++) {
                glBindTexture(GL_TEXTURE_2D, info->texture_id[j]);
                x1 = tx/(float)tile_count_x;
                y1 = (tile_count_y - 1 - ty)/(float)tile_count_y;
                x2 = (tx + 1)/(float)tile_count_x;
                y2 = (tile_count_y - 1 - ty + 1)/(float)tile_count_y;

                glEnable(GL_TEXTURE_2D);
                glColor4f(1.0f, 1.0f, 1.0f, (float)dissolve);
                glBegin(GL_QUADS);
                glTexCoord2f(0 + tile_edge_texel_low_bias / tile_size_x,
                        1 - tile_edge_texel_high_bias / tile_size_y);
                glVertex2f(x1, y1);
                glTexCoord2f(1 - tile_edge_texel_high_bias / tile_size_x,
                        1 - tile_edge_texel_high_bias / tile_size_y);
                glVertex2f(x2, y1);
                glTexCoord2f(1 - tile_edge_texel_high_bias / tile_size_x,
                        0 + tile_edge_texel_low_bias / tile_size_y);
                glVertex2f(x2, y2);
                glTexCoord2f(0 + tile_edge_texel_low_bias / tile_size_x,
                        0 + tile_edge_texel_low_bias / tile_size_y);
                glVertex2f(x1, y2);
                glEnd();

                if (g_show_lines) {
                    glDisable(GL_TEXTURE_2D);
                    glColor4f(1.0, 0.0, 0.0, 0.75);
                    glBegin(GL_LINE_LOOP);
                    glVertex2f(x1, y1);
                    glVertex2f(x2, y1);
                    glVertex2f(x2, y2);
                    glVertex2f(x1, y2);
                    glEnd();
                }

                j++;
            }
        }
    glPopMatrix();
#endif
}

static void
set_nice_font(HDC hdc, int *font_height)
{
    static bool initialized = false;
    static HFONT font;
    static char *font_names[] = {
        "Verdana",
        "Tahoma",
        "Arial",
        "Helvetica",
        "MS Sans Serif"
    };
    static int font_names_count = sizeof(font_names)/sizeof(font_names[0]);

    // we pick an explicit font height (instead of just passing 0,
    // which means "default") because we want to know the font height
    // to figure out how big to make the frame when showing filenames.
    // Ideally we'd let the system pick the height then get the height
    // given an HFONT, but I can't figure out how to do that.
    int height = (in_fullscreen ? 19 : 15);

    if (!initialized) {
        font = NULL;

        for (int i = 0; i < font_names_count; i++) {
            font = CreateFont(
                    height,                         // height -- 0 = default
                    0,                              // width -- 0 = closest
                    0,                              // escapement -- none
                    0,                              // orientation -- none
                    FW_NORMAL,                      // weight -- normal
                    false,                          // italics
                    false,                          // underline
                    false,                          // strikeout
                    DEFAULT_CHARSET,                // character set
                    OUT_TT_PRECIS,                  // get a TrueType font
                    CLIP_DEFAULT_PRECIS,            // default clipping
                    DEFAULT_QUALITY,                // whatever
                    DEFAULT_PITCH | FF_DONTCARE,    // pitch and family
                    font_names[i]);                 // font name

            if (font != NULL) {
                break;
            }
        }

        if (font == NULL) {
            font = GetStockFont(SYSTEM_FONT);
        }

        initialized = true;
    }

    if (font_height != NULL) {
        *font_height = height;
    }

    SelectFont(hdc, font);
}

void
display_slides(HDC
#if !USE_D3D   // avoid compiler warning
        hdc
#endif
        )
{
    char *beautiful_filename = NULL;
    Filename_notice *filename_notice = NULL;

#if USE_D3D
    if (g_pd3dDevice == NULL) {
        return;
    }
#else
    if (gl_context == NULL) {
        return;
    }
#endif

#if PER_FRAME_OUTPUT
    DWORD start_time = timeGetTime();
    jessu_printf(THREAD_GL, "starting to paint");
#endif

#if USE_D3D
    // D3D: setViewPort?
    D3DVIEWPORT8 vp;
    vp.X = 0;
    vp.Y = 0;
    vp.Width = window_width;
    vp.Height = window_height;
    vp.MinZ = 0.0f;
    vp.MaxZ = 1.0f;
    g_pd3dDevice->SetViewport(&vp);

    float ortho_height = window_height / (float)window_width;
    D3DXMATRIX matProj;
    D3DXMatrixOrthoOffCenterLH(&matProj,
            0, 1,
            0.5f - ortho_height/2, 0.5f + ortho_height/2,
            -1, 1);
    g_pd3dDevice->SetTransform(D3DTS_PROJECTION, &matProj);

    // D3D: Clear
    g_pd3dDevice->Clear(0, NULL, D3DCLEAR_TARGET,
            D3DCOLOR_XRGB(0,0,0), 1.0f, 0);

    // D3D: BeginScene
    if (FAILED(g_pd3dDevice->BeginScene())) {
        jessu_printf(THREAD_GL, "BeginScene() failed");
        return;
    }
#else
    glMatrixMode(GL_PROJECTION);
    glViewport(0, 0, window_width, window_height);
    glLoadIdentity();
    float ortho_height = window_height / (float)window_width;
    glOrtho(0.0, 1.0,
        0.5f - ortho_height/2, 0.5f + ortho_height/2,
        -1.0f, 1.0f);
    glMatrixMode(GL_MODELVIEW);

    glClear(GL_COLOR_BUFFER_BIT);  // uses glClearColor
#endif

    if (paused) {
        // display oldest one
        int s;

        if (!slide[0].being_displayed) {
            if (!slide[1].being_displayed) {
                // show nothing
                s = -1;
            } else {
                s = 1;
            }
        } else if (!slide[1].being_displayed) {
            s = 0;
        } else if (slide[1].time < slide[0].time) {
            s = 1;
        } else {
            s = 0;
        }

        if (s != -1) {
            beautiful_filename = slide[s].beautiful_filename;
            filename_notice = slide[s].filename_notice;
            display_slide(s);
        }
    } else {
        if (slide[0].time < slide[1].time) {
            display_slide(1);
            display_slide(0);
        } else {
            display_slide(0);
            display_slide(1);
        }

        if (!slide[0].being_displayed) {
            if (slide[1].being_displayed) {
                beautiful_filename = slide[1].beautiful_filename;
                filename_notice = slide[1].filename_notice;
            } else {
                // leave NULL
            }
        } else if (!slide[1].being_displayed) {
            beautiful_filename = slide[0].beautiful_filename;
            filename_notice = slide[0].filename_notice;
        } else if (slide[0].time < slide[1].time) {
            beautiful_filename = slide[0].beautiful_filename;
            filename_notice = slide[0].filename_notice;
        } else {
            beautiful_filename = slide[1].beautiful_filename;
            filename_notice = slide[1].filename_notice;
        }
    }

#if !USE_D3D
    /* debugging stuff */
    if (display_debugging) {
        if (loading_jpeg) {
            int i = loading_jpeg_progress;
            glDisable(GL_TEXTURE_2D);
            glColor4f(1.0f, 0.0f, 0.0f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.5, 0.5 + loading_jpeg/10.0);
            glVertex2f(0.5 + i*0.001, 0.5 + loading_jpeg/10.0);
            glVertex2f(0.5 + i*0.001, 0.6 + loading_jpeg/10.0);
            glVertex2f(0.5, 0.6 + loading_jpeg/10.0);
            glEnd();
            glColor4f(1.0f, 0.5f, 0.5f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.5 + i*0.001, 0.5 + loading_jpeg/10.0);
            glVertex2f(0.6, 0.5 + loading_jpeg/10.0);
            glVertex2f(0.6, 0.6 + loading_jpeg/10.0);
            glVertex2f(0.5 + i*0.001, 0.6 + loading_jpeg/10.0);
            glEnd();
        }
        if (scaling_image) {
            int i = scaling_image_progress;
            glDisable(GL_TEXTURE_2D);
            glColor4f(0.0f, 1.0f, 0.0f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.6, 0.5 + scaling_image/10.0);
            glVertex2f(0.6 + i*0.001, 0.5 + scaling_image/10.0);
            glVertex2f(0.6 + i*0.001, 0.6 + scaling_image/10.0);
            glVertex2f(0.6, 0.6 + scaling_image/10.0);
            glEnd();
            glColor4f(0.5f, 1.0f, 0.5f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.6 + i*0.001, 0.5 + scaling_image/10.0);
            glVertex2f(0.7, 0.5 + scaling_image/10.0);
            glVertex2f(0.7, 0.6 + scaling_image/10.0);
            glVertex2f(0.6 + i*0.001, 0.6 + scaling_image/10.0);
            glEnd();
        }
        if (downloading_texture) {
            int i = downloading_texture_progress;
            glDisable(GL_TEXTURE_2D);
            glColor4f(0.0f, 0.0f, 1.0f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.7, 0.5 + downloading_texture/10.0);
            glVertex2f(0.7 + i*0.001, 0.5 + downloading_texture/10.0);
            glVertex2f(0.7 + i*0.001, 0.6 + downloading_texture/10.0);
            glVertex2f(0.7, 0.6 + downloading_texture/10.0);
            glEnd();
            glColor4f(0.5f, 0.5f, 1.0f, 1.0f);
            glBegin(GL_QUADS);
            glVertex2f(0.7 + i*0.001, 0.5 + downloading_texture/10.0);
            glVertex2f(0.8, 0.5 + downloading_texture/10.0);
            glVertex2f(0.8, 0.6 + downloading_texture/10.0);
            glVertex2f(0.7 + i*0.001, 0.6 + downloading_texture/10.0);
            glEnd();
        }
    }
#endif

    if (in_fullscreen && display_filename && beautiful_filename != NULL) {
#if USE_D3D
        int y2 = window_height - FILENAME_BOTTOM_MARGIN;
        int y1 = y2 - FILENAME_HEIGHT;

        draw_filename_background(y1, y2, filename_notice);
        draw_filename_notice(y1, y2, filename_notice);
#else
        static bool font_lists_initialized = false;
        static int font_height;
        if (!font_lists_initialized) {
            set_nice_font(hdc, &font_height);
            wglUseFontBitmaps(hdc, 0, 256, 1000); 
            font_lists_initialized = true;
        }

        double text_y = 0.18;
        double frame_y = 0.17;
        // this is awkward because the text_y is the baseline, so we
        // have descenders below that.  I'd really like to get the full
        // font info, including descenders and ascenders, so we can center
        // this text vertically properly.  The 0.7 multiplier is to
        // compensate for all that.
        double height = (double)ortho_height*font_height/window_height*0.65 +
            (text_y - frame_y)*2;

        glEnable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
        glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
        glBegin(GL_QUADS);
        glVertex2f(0.1, frame_y);
        glVertex2f(0.9, frame_y);
        glVertex2f(0.9, frame_y + height);
        glVertex2f(0.1, frame_y + height);
        glEnd();

        glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
        glRasterPos2f(0.112, text_y);
        glListBase(1000); 
        glCallLists(strlen(beautiful_filename),
                GL_UNSIGNED_BYTE, beautiful_filename);
#endif
    }

#if USE_D3D
    g_pd3dDevice->EndScene();

    // Present the backbuffer contents to the display
    g_pd3dDevice->Present(NULL, NULL, NULL, NULL);
#else
    SwapBuffers(gl_device);
#endif

#if PER_FRAME_OUTPUT
    DWORD end_time = timeGetTime();
    jessu_printf(THREAD_GL, "done painting (took %g seconds)",
            (end_time - start_time)/1000.0);
#endif

    g_frame_count++;

#if !PER_FRAME_OUTPUT
    // keep an eye out for very slow frames (less than 20 fps)
    static DWORD last_frame_time = 0;
    DWORD now = timeGetTime();

    if (last_frame_time != 0 && now - last_frame_time > 1000/20) {
        jessu_printf(THREAD_GL, "Slow frame: %lu ms (%lu FPS)",
                now - last_frame_time, 1000/(now - last_frame_time));
    }

    last_frame_time = now;
#endif
}

static void
display_error_message(HWND hWnd, HDC hdc, PAINTSTRUCT *ps)
{
    static int initialized = false;
    static COLORREF gray_color = RGB(64, 64, 64);
    static COLORREF black_color = RGB(0, 0, 0);
    static COLORREF white_color = RGB(255, 255, 255);
    static HBRUSH black_brush;
    static HBRUSH gray_brush;
    static HPEN white_pen;
    RECT window_rect;
    RECT note_rect;

    if (!initialized) {
        black_brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
        gray_brush = (HBRUSH)CreateSolidBrush(gray_color);
        white_pen = (HPEN)GetStockObject(WHITE_PEN);
        initialized = true;
    }

    FillRect(hdc, &ps->rcPaint, black_brush);
    SetTextColor(hdc, white_color);

    GetClientRect(hWnd, &window_rect);

    set_nice_font(hdc, NULL);

    // Use DT_CALCRECT to figure out the height of the error window
    note_rect.left = 0;
    if (in_fullscreen) {
        note_rect.right = NOTE_WIDTH - 2*NOTE_INTERNAL_MARGIN;
    } else {
        note_rect.right = window_rect.right - window_rect.left -
            2*NOTE_PREVIEW_MARGIN;
    }
    note_rect.top = 0;
    note_rect.bottom = 0;  // will be filled in

    DrawText(hdc, error_message, -1, &note_rect,
            DT_CENTER | DT_WORDBREAK | DT_CALCRECT);

    int height = note_rect.bottom - note_rect.top + 2*NOTE_INTERNAL_MARGIN;

    if (in_fullscreen) {
        SelectObject(hdc, white_pen);
        SelectObject(hdc, gray_brush);
        SetBkColor(hdc, gray_color);

        note_rect.left = (int)random_in_range(window_rect.left +
                NOTE_EXTERNAL_MARGIN,
                window_rect.right - NOTE_WIDTH - NOTE_EXTERNAL_MARGIN);
        note_rect.right = note_rect.left + NOTE_WIDTH;
        note_rect.top = (int)random_in_range(window_rect.top +
                NOTE_EXTERNAL_MARGIN,
                window_rect.bottom - height - NOTE_EXTERNAL_MARGIN);
        note_rect.bottom = note_rect.top + height;

        Rectangle(hdc, note_rect.left, note_rect.top,
                note_rect.right, note_rect.bottom);

        note_rect.left += NOTE_INTERNAL_MARGIN;
        note_rect.top += NOTE_INTERNAL_MARGIN;
        note_rect.right -= NOTE_INTERNAL_MARGIN;
        note_rect.bottom -= NOTE_INTERNAL_MARGIN;

        DrawText(hdc, error_message, -1, &note_rect, DT_CENTER | DT_WORDBREAK);
    } else {
        SelectObject(hdc, white_pen);
        SetBkColor(hdc, black_color);

        note_rect.left = window_rect.left + NOTE_PREVIEW_MARGIN;
        note_rect.right = window_rect.right - NOTE_PREVIEW_MARGIN;

        note_rect.top = (window_rect.bottom + window_rect.top)/2 -
            height/2 + NOTE_INTERNAL_MARGIN;
        note_rect.bottom = (window_rect.bottom + window_rect.top)/2 +
            height/2 - NOTE_INTERNAL_MARGIN;

        DrawText(hdc, error_message, -1, &note_rect, DT_CENTER | DT_WORDBREAK);
    }
}


static void
reshape(int width, int height)
{
    window_width = width;
    window_height = height;

#if !USE_D3D
    if (gl_context != NULL) {
        glViewport(0, 0, window_width, window_height);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0,
                0.5f - window_height / (float)window_width / 2,
                0.5f + window_height / (float)window_width / 2,
                -1.0f, 1.0f);
        glMatrixMode(GL_MODELVIEW);
    }
#endif
}

static void
idle(void)
{
    /* This is essentially the GL thread.  We're going to check
       to see if there are any textures to be downloaded to the
       graphics board, then we're going to check to see if there
       are any slides to display.  */

    int j;
    bool did_something = false;

    for (int i = 0; i < 2; i++) {
        if (slide[i].texture_ready && !slide[i].texture_downloaded) {
            did_something = true;

            if (!slide[i].texture_used) {
                downloading_texture = 1 + i;
                jessu_printf(THREAD_GL, "start download of %d", i);
                slide[i].tile_number = 0;
                slide[i].texture_used = 1;
            }

            /* download tile "tile_number" */
            j = slide[i].tile_number;
            downloading_texture_progress = j*100/tile_count;
            jessu_printf(THREAD_GL, "download tile %d of %d for %d",
                    j, tile_count, i);

#if USE_D3D
            D3DLOCKED_RECT rect;

            int result = slide[i].textures[j]->LockRect(0, &rect, NULL, 0);
            if (FAILED(result)) {
                jessu_printf(THREAD_GL, "LockRect() failed (%d)",
                        result & 0xffff);
            } else {
                memmove(rect.pBits, slide[i].tile[j],
                        tile_size_x*tile_size_y*BYTES_PER_TEXEL);
                slide[i].textures[j]->UnlockRect(0);
            }
#else
            glBindTexture(GL_TEXTURE_2D, slide[i].texture_id[j]);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(GL_TEXTURE_2D, 0, preferred_texture_internal_format,
                    tile_size_x, tile_size_y,
                    0, GL_RGBA, GL_UNSIGNED_BYTE, slide[i].tile[j]);
#endif

            slide[i].tile_number++;
            if (slide[i].tile_number >= tile_count) {
                /* tell the worker thread that it can fill this
                   slide with the next image */
                jessu_printf(THREAD_GL, "end download of %d", i);
                slide[i].texture_ready = 0;
                slide[i].texture_used = 0;

                /* mark that the textures have been downloaded */
                slide[i].ratio = (float)slide[i].width/slide[i].height;
                slide[i].texture_downloaded = 1;
                downloading_texture = 0;
                strcpy(slide[i].beautiful_filename,
                        slide[i].next_beautiful_filename);

#if USE_D3D
                delete slide[i].filename_notice;
                slide[i].filename_notice = prepare_filename_notice(g_pd3dDevice,
                        slide[i].beautiful_filename);
#endif

                slide[i].misc_info = slide[i].next_misc_info;
            }
        }
    }

    /*
     * See if any slides are ready to go.
     */

    for (i = 0; i < 2; i++) {
        if (!slide[i].being_displayed &&
                slide[i].texture_downloaded &&
                slide[i].time_to_start &&
                !paused) {

            /* set up the parameters and set "being_displayed" to true */
            did_something = true;
            start_slide(i);
        }
    }

    if (slide[0].being_displayed || slide[1].being_displayed) {
        did_something = true;
        schedule_paint();
    }

    if (!did_something) {
        // sleep a bit so we don't busy wait
        Sleep(50);
    }
}

static void
setup_rendering_on_window(HWND hWnd)
{
#if USE_D3D
    setup_d3d_rendering(hWnd);
#else
    PIXELFORMATDESCRIPTOR pfd = {
        sizeof(PIXELFORMATDESCRIPTOR),  //  size of this pfd
        1,                     // version number
        PFD_DRAW_TO_WINDOW |   // support window
        PFD_SUPPORT_OPENGL |   // support OpenGL
        PFD_DOUBLEBUFFER,      // double buffered
        PFD_TYPE_RGBA,         // RGBA type
        16,                    // at least 16 bits
        0, 0, 0, 0, 0, 0,      // color bits ignored
        0,                     // alpha buffer
        0,                     // shift bit ignored
        0,                     // no accumulation buffer
        0, 0, 0, 0,            // accum bits ignored
        16,                    // 16-bit z-buffer but we don't use it anyway
        0,                     // no stencil buffer
        0,                     // no auxiliary buffer
        PFD_MAIN_PLANE,        // main layer
        0,                     // reserved
        0, 0, 0                // layer masks ignored
    };

    gl_device = GetDC(rendering_window);

    int pixelformat = ChoosePixelFormat(gl_device, &pfd);

    if (pixelformat == 0) {
        set_error_message("Could not find the OpenGL pixel format we wanted. "
            "Contact jessu@plunk.org");
        gl_context = NULL;
        return;
    }

    if (SetPixelFormat(gl_device, pixelformat, &pfd) == FALSE) {
        set_error_message("Could not set OpenGL pixel format.  Contact "
            "jessu@plunk.org");
        gl_context = NULL;
        return;
    }

    gl_context = wglCreateContext(gl_device);
    wglMakeCurrent(gl_device, gl_context);
#endif
}

static void
schedule_paint(void)
{
    paint_scheduled = true;
}

static void
convert_speed(float old_speed, float speed, DWORD now, SLIDE_INFO *slide)
{
    if (slide->time != 0) {
        slide->time =
            (unsigned long)(now - (now - slide->time)/speed*old_speed);
    }
}


static void
convert_speeds(float old_speed, float speed)
{
    // adjust time stamp on images so that speed multiplier
    // doesn't make zooming jump

    DWORD now = timeGetTime();

    convert_speed(old_speed, speed, now, &slide[0]);
    convert_speed(old_speed, speed, now, &slide[1]);
}

static void
relocate_error_message()
{
    static bool initialized = false;
    static DWORD last_time;

    if (!initialized) {
        last_time = timeGetTime();
        initialized = true;
        schedule_paint();
    } else {
        DWORD now = timeGetTime();

        if (now > last_time + ERROR_MOVE_TIME*1000) {
            last_time = now;
            schedule_paint();
        }

        _sleep(100);
    }
}

static void
log_fps()
{
    static DWORD last_time = 0;
    DWORD now = timeGetTime();

    if (last_time == 0) {
        last_time = now;
        g_frame_count = 0;
    } else {
        if (now > last_time + 1000) {
            jessu_printf(THREAD_GL, "%lu FPS", g_frame_count);

            last_time = now;
            g_frame_count = 0;
        }
    }
}

static void
handle_events_until_done(void)
{
    MSG msg;

    /* Message loop */
    while (!quit_requested) {
        if (PeekMessage(&msg, rendering_window, 0, 0, PM_REMOVE) == FALSE) {
#if USE_D3D
            if (g_pD3D != NULL) {
                idle();
            }
#else
            if (gl_context != NULL) {
                idle();
            }
#endif

            if (paint_scheduled) {
                // we used to call display_slides() directly here, but
                // now that we need to pass it the hdc, we call RedrawWindow()
                // instead.  Seems to work just as well.
                RedrawWindow(rendering_window, NULL, NULL, RDW_INVALIDATE);
                paint_scheduled = false;
            }

            if (error_message != NULL) {
                relocate_error_message();
            }

            log_fps();
        } else {
            if (msg.message == WM_QUIT) {
                return;
            }
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

void
reset_mouse_motion()
{
    g_received_first_mousemove = false;
}

#if !PURIFY_MODE && !IGNORE_MOUSE_MOTION
static void
handle_mouse_motion(int x, int y)
{
    static int original_x;
    static int original_y;

    if (!in_fullscreen) {
        return;
    }

    if (!g_received_first_mousemove) {
        g_received_first_mousemove = true;
        original_x = x;
        original_y = y;
    } else {
        int distance_squared = (x - original_x)*(x - original_x) +
            (y - original_y)*(y - original_y);

        if (distance_squared > 256) { // 16 squared
            if (in_slideshow || paused) {
                // if they want to point to something.
                // turns itself off at the end of this slide.
                show_cursor();
                g_received_first_mousemove = false;
            } else if (in_screensaver) {
                jessu_printf(THREAD_GL, "User moved mouse, quitting");
                quit_requested = true;
            }
        }
    }
}
#endif

static void
toggle_pause()
{
    paused = !paused;
    DWORD now = timeGetTime();

    if (paused) {
        /* keep track of where we are in the slide */
        slide[0].delta_time = now - slide[0].time;
        slide[1].delta_time = now - slide[1].time;
        paused_delta_y = 0;
        actual_paused_delta_y = 0;
    } else {
        /* restore that */
        slide[0].time = now - slide[0].delta_time;
        slide[1].time = now - slide[1].delta_time;
    }
}

static int
handle_slideshow_key(int code)
{
    switch (code) {
        case VK_LEFT:
            set_direction(-1);
            return true;
            break;

        case VK_RIGHT:
            set_direction(1);
            return true;
            break;

        case VK_UP:
            paused_delta_y += 0.1;
            return true;
            break;

        case VK_DOWN:
            paused_delta_y -= 0.1;
            return true;
            break;
    }

    return false;
}


static void
handle_slideshow_char(int key)
{
    float old_speed;
    int i;

    switch (key) {
        case 27:  // ESC
        case 'q':
            jessu_printf(THREAD_GL,
                    "User pressed ESC or 'q' in slideshow, quitting");
            quit_requested = true;
            break;

        case 's': // slow
            old_speed = speed;
            speed = 0.5;
            convert_speeds(old_speed, speed);
            break;

        case 'n': // normal speed
            old_speed = speed;
            speed = 1;
            convert_speeds(old_speed, speed);
            break;

        case 'f': // fast
            old_speed = speed;
            speed = 5;
            convert_speeds(old_speed, speed);
            break;

        case '+': // skip 10
            for (i = 0; i < 10; i++) {
                get_next_filename(NULL);
            }
            break;

        case '-': // skip 10 backwards
            set_direction(-1);
            for (i = 0; i < 10; i++) {
                get_next_filename(NULL);
            }
            set_direction(1);
            break;

        case ' ':
            toggle_pause();
            break;
    }
}

static bool
handle_screensaver_key(int vkey, bool pressed) // virtual key, like VK_LEFT
{
    switch (vkey) {
        case 'F':
            if (pressed) {
                display_filename = !display_filename;

                if (display_filename) {
                    jessu_printf(THREAD_GL, "Displaying filenames");
                } else {
                    jessu_printf(THREAD_GL, "Hiding filenames");
                }
            }
            return true;

        case 'P':
            if (pressed) {
                toggle_pause();

                if (paused) {
                    jessu_printf(THREAD_GL, "Paused");
                } else {
                    jessu_printf(THREAD_GL, "Unpaused");
                }
            }
            return true;

        case VK_UP:
            if (pressed) {
                if (!paused) {
                    toggle_pause();
                }
                paused_delta_y += 0.1;
            }
            return true;

        case VK_DOWN:
            if (pressed) {
                if (!paused) {
                    toggle_pause();
                }
                paused_delta_y -= 0.1;
            }
            return true;
    }

    return false;
}

static LRESULT APIENTRY
gl_wnd_proc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (hWnd != rendering_window) {
        return DefWindowProc(hWnd, message, wParam, lParam);
    }

#if USE_D3D
    // setup is done earlier
#else
    if (gl_context == NULL && error_message == NULL) {
        setup_rendering_on_window(hWnd);
    }
#endif

    switch (message) {
        case WM_CREATE:
            return 0;
            break;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            break;

        case WM_SIZE:
            reshape((int) LOWORD(lParam),(int) HIWORD(lParam));
            return 0;
            break;

        case WM_PAINT:
            {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hWnd, &ps);
#if USE_D3D
                if (g_pD3D != NULL) {
#else
                if (gl_context != NULL) {
#endif
                    display_slides(hdc);
                } else if (error_message != NULL) {
                    display_error_message(hWnd, hdc, &ps);
                }
                EndPaint(hWnd, &ps);    /* clears WM_PAINT from queue? */
            }
            return 0;
            break;

        case WM_MOUSEMOVE:
#if !PURIFY_MODE && !IGNORE_MOUSE_MOTION
            handle_mouse_motion((int) LOWORD(lParam), (int) HIWORD(lParam));
#endif
            return 0;
            break;

        // catch only the ups.  if we caught the downs then the
        // application underneath would get the up.
        // Note: this is slightly annoying because the screensaver
        // takes slightly longer to quit.  Maybe revisit this.
        // A possible solution would be to blank the screen on
        // key down so that the user would get instant feedback.
        // but can we be guaranteed a key up after a key down?
        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
#if !PURIFY_MODE
        case WM_SYSKEYUP:  // alt, etc.
        case WM_KEYUP:
#endif
            if (in_screensaver) {
                if (!handle_screensaver_key(wParam, false)) {
                    jessu_printf(THREAD_GL, "Got button or key, quitting");
                    quit_requested = true;
                }
            }
            return 0;
            break;

        case WM_KEYDOWN:
            if (in_slideshow) {
                if (handle_slideshow_key(wParam)) {
                    return 0;
                }
            }
            if (in_screensaver) {
                if (handle_screensaver_key(wParam, true)) {
                    return 0;
                }
            }
            break;

        case WM_CHAR:
            // we first get a WM_KEYDOWN, then when we pass it on to
            // the default handler then it calls us again with this
            // key
            if (in_slideshow) {
                handle_slideshow_char(wParam);
                return 0;
            }
            break;

        case WM_ERASEBKGND:
            // don't erase background
            // XXX maybe not if error or if not our window, see sample
            return true;

        default:
            break;
    }

    /* Deal with any unprocessed messages */
    return DefWindowProc(hWnd, message, wParam, lParam);
}

void register_jessu_class(HINSTANCE hInstance)
{
    WNDCLASS wndClass;

    /* Define and register the window class */
    wndClass.style = CS_HREDRAW | CS_VREDRAW;
    wndClass.lpfnWndProc = gl_wnd_proc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = hInstance;
    wndClass.hIcon = LoadIcon(NULL, MAKEINTRESOURCE(IDI_JESSU));
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (struct HBRUSH__ *)GetStockObject(BLACK_BRUSH);
    wndClass.lpszMenuName = NULL;
    wndClass.lpszClassName = jessu_className;
    RegisterClass(&wndClass);
}

#if BLANK_OTHER_MONITORS

BOOL CALLBACK
monitorProc( HMONITOR hMonitor, HDC hdcMonitor,
  LPRECT lprcMonitor, LPARAM dwData)
{
    HINSTANCE hInstance = (HINSTANCE)dwData;
    MONITORINFOEX monitor;
    HWND screen_window;

    monitor.cbSize = sizeof(MONITORINFOEX);
    GetMonitorInfo(hMonitor, &monitor);
    if(!(monitor.dwFlags & MONITORINFOF_PRIMARY)) {

        char const *windowName = "Jessu Screen Blanking Window";

        int     x;
        int     y;
        int     width;
        int     height;
        DWORD   style;

        x = monitor.rcMonitor.left;
        y = monitor.rcMonitor.top;
        width = monitor.rcMonitor.right - monitor.rcMonitor.left;
        height = monitor.rcMonitor.bottom - monitor.rcMonitor.top;
        style = WS_POPUP | WS_MAXIMIZE;

        /* Create a window of the previously defined class */
        screen_window = CreateWindowEx(
                (!do_benchmark && ALLOW_TOPMOST) ? WS_EX_TOPMOST : 0,
                jessu_className,
                windowName,
                style,
                x, y,
                width, height,     
                NULL,
                NULL,
                hInstance,
                NULL);   

        if (rendering_window == NULL) {
            fprintf(debug_output, "couldn't open blanking Window for "
                "{%d, %d, %d, %d}\n",
                monitor.rcMonitor.left, monitor.rcMonitor.right,
                monitor.rcMonitor.top, monitor.rcMonitor.bottom);
            // ignore this failure.
        }

        /* Map the window to the screen */
        ShowWindow(screen_window, SW_SHOW);

        /* Force the window to repaint itself */
        UpdateWindow(screen_window);

        /* Drain the queue */
        MSG msg;
        while (PeekMessage(&msg, screen_window, 0, 0, PM_REMOVE) != FALSE) {
            if (msg.message == WM_QUIT)
                return FALSE;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return TRUE;
}

#endif // if BLANK_OTHER_MONITORS

static void
blank_other_monitors(HINSTANCE
#if BLANK_OTHER_MONITORS        // avoid compiler warning
        hInstance
#endif
        )
{
#if BLANK_OTHER_MONITORS
    EnumDisplayMonitors(GetDC(NULL), NULL, monitorProc, (LPARAM)hInstance);
#endif // if BLANK_OTHER_MONITORS
}

static void
create_rendering_window(HINSTANCE hInstance, int nCmdShow)
{
    char const *windowName = "Jessu Screensaver";

    int         x;
    int         y;
    int         width;
    int         height;
    DWORD       style;

    if (in_fullscreen) {
        x = 0;
        y = 0;
#if USE_SMALL_WINDOW
        width = 256;
        height = 256;
        style = WS_OVERLAPPEDWINDOW;
#else
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
        style = WS_POPUP | WS_MAXIMIZE;
#endif
    } else {
        RECT parent_rect;
        GetClientRect(g_parent_window, &parent_rect);

        x = 0;
        y = 0;
        window_width = width = parent_rect.right - parent_rect.left;
        window_height = height = parent_rect.bottom - parent_rect.top;
        style = WS_CHILD;
    }

    /* Create a window of the previously defined class */
    rendering_window = CreateWindowEx(
            (in_fullscreen && !do_benchmark && ALLOW_TOPMOST) ?
                WS_EX_TOPMOST : 0,
            jessu_className,
            windowName,
            style,
            x, y,
            width, height,     
            g_parent_window,
            NULL,
            hInstance,
            NULL);   

    if (rendering_window == NULL) {
        MessageBox(NULL,
                (LPCTSTR)"Cannot Open Rendering Window.",
                "Cannot Open Rendering Window",
                MB_OK | MB_ICONEXCLAMATION);
        fprintf(debug_output, "couldn't open Rendering Window\n");
        exit(1);
    }

    /* Map the window to the screen */
    ShowWindow(rendering_window, nCmdShow);

    /* Force the window to repaint itself */
    UpdateWindow(rendering_window);

    /* Drain the queue, allowing OpenGL/D3D to be initialized */
    MSG msg;
    while (PeekMessage(&msg, rendering_window, 0, 0, PM_REMOVE) != FALSE) {
        if (msg.message == WM_QUIT) {
            return;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

static unsigned long __stdcall
worker_thread(void *  /* params */)
{
    /*
     * The worker thread checks the slides and fills in from disk
     * images whenever slots are no longer needed.  Basically it
     * checks for textures that are no longer used and it loads
     * texture space.  It's up to the main thread to send that
     * to the graphics board for display.
     */

    int i;
    int did_something;
    static Vertical_scaler vertical_scaler;

    srand(seed);

    while (!g_worker_thread_should_quit) {
        did_something = 0;

        for (i = 0; i < 2; i++) {
            if (!slide[i].texture_ready && !slide[i].texture_used) {
                loading_jpeg = 1 + i;
                jessu_printf(THREAD_WORKER, "reading texture %d", i);
                vertical_scaler.Set_destination_parameters(
                        slide[i].tile, tile_size_x, tile_size_y,
                        tile_count_x, tile_count_y, texture_size_x,
                        texture_size_y);
                load_next_picture(vertical_scaler, &slide[i]);
                loading_jpeg = 0;
#if 0
                scaling_image = 1 + i;
                jessu_printf(THREAD_WORKER, "scaling texture %d", i);
                scale_and_tile(pixels, texture_size_x, slide[i].height,
                        slide[i].tile, tile_size_x, tile_size_y,
                        tile_count_x, tile_count_y,
                        texture_size_x, texture_size_y);
                scaling_image = 0;
                // don't free "pixels" -- the buffer is reused
#endif

                /* tell GL thread that it can download this texture */
                slide[i].texture_ready = 1;
                jessu_printf(THREAD_WORKER, "texture for %d is ready", i);
                did_something = 1;
            }
        }

        if (!did_something) {
            /* avoid busy looping */
            jessu_printf(THREAD_WORKER, "sleeping");
            _sleep(1000);
        }
    }

    jessu_printf(THREAD_WORKER, "asked to quit");
    return 0;
}

#if MALLOC_DEBUGGING
void *jessu_malloc(THREAD_TYPE thread, size_t size, char *description)
{
    jessu_printf(thread, "%s: malloc(%d)", description, size);
    return malloc(size);
}
#else
void *jessu_malloc(THREAD_TYPE, size_t size, char *)
{
    return malloc(size);
}
#endif

#if MALLOC_DEBUGGING
void *jessu_calloc(THREAD_TYPE thread,
        size_t count, size_t size, char *description)
{
    jessu_printf(thread, "%s: calloc(%d, %d)", description, count, size);
    return calloc(count, size);
}
#else
void *jessu_calloc(THREAD_TYPE, size_t count, size_t size, char *)
{
    return calloc(count, size);
}
#endif

#if MALLOC_DEBUGGING
void *jessu_realloc(THREAD_TYPE thread,
        void *ptr, size_t size, char *description)
{
    jessu_printf(thread, "%s: realloc(%d)", description, size);
    return realloc(ptr, size);
}
#else
void *jessu_realloc(THREAD_TYPE, void *ptr, size_t size, char *)
{
    return realloc(ptr, size);
}
#endif

#if MALLOC_DEBUGGING
void jessu_free(THREAD_TYPE thread, void *ptr, char *description)
{
    jessu_printf(thread, "%s: free()", description);
    free(ptr);
}
#else
void jessu_free(THREAD_TYPE, void *ptr, char *)
{
    free(ptr);
}
#endif

#if MALLOC_DEBUGGING
char *jessu_strdup(THREAD_TYPE thread, const char *string, char *description)
{
    jessu_printf(thread, "%s: strdup(%d)", description, strlen(string));
    return strdup(string);
}
#else
char *jessu_strdup(THREAD_TYPE, const char *string, char *)
{
    return strdup(string);
}
#endif

void jessu_printf(THREAD_TYPE thread, char *fmt, ...)
{
#if OUTPUT_DEBUG_FILE
    if (print_debugging) {
        DWORD time = timeGetTime();
        int seconds = time/1000%100;
        int mseconds = time%1000;
        char buf[256];

#if PER_FRAME_OUTPUT
        static last_time = 0;
        DWORD diff = time - last_time;

        if (last_time != 0 && diff > 250) {
            fprintf(debug_output, "------- %d.%03d seconds gap\n",
                    diff/1000, diff%1000);
        }
        last_time = time;
#endif

        sprintf(buf, "%2d.%03d ", seconds, mseconds);
        int len = strlen(buf);
        int spaces = thread*20;

        memset(buf + len, ' ', spaces);

        va_list ap;
        va_start(ap, fmt);
        vsprintf(buf + len + spaces, fmt, ap);
        va_end(ap);

        fprintf(debug_output, "%s\n", buf);
        fflush(debug_output);
    }
#endif
}

char *jessu_strerror()
{
    static char buffer[128];
    char *error;

    // normal strerror() doesn't work because I can't get access to
    // errno, and _strerror() has a trailing \n, so we have to strip
    // that out.
    error = _strerror(NULL);

    if (error == NULL) {
        strcpy(buffer, "Unknown error");
    } else {
        strncpy(buffer, error, sizeof(buffer) - 1);
        buffer[sizeof(buffer) - 1] = '\0';

        int len;

        while ((len = strlen(buffer)) > 0 && buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
        }
    }

    return buffer;
}

static void
usage(void)
{
    static char *usage_message =
        "Usage: ssjessu.scr [options] [mode]\n"
        "\n"
        "Options:\n"
        "    /b\t\trun benchmark and report results\n"
#if OUTPUT_DEBUG_FILE
        "    /d\t\tprint debugging information\n"
#endif
#if !RELEASE_QUALITY
        "    /D\t\tdisplay debugging information\n"
        "    /seed n\tset the random seed to \"n\"\n"
        "    /dir d\tset the pictures directory to \"d\"\n"
#endif
        "\n"
        "Modes:\n"
        "    /c:n\t\tshow options dialog box, child of window \"n\"\n"
        "    /p n\t\trun as child of window \"n\"\n"
        "    /s\t\trun in full-screen mode\n"
#if !RELEASE_QUALITY
        "    /show f\tslide show of text file \"f\"\n"
#endif
        ;

    MessageBox(NULL,
            (LPCTSTR)usage_message,
            "Usage",
            MB_OK | MB_ICONEXCLAMATION);
    exit(EXIT_FAILURE);
}

static void cleanup()
{
    if (in_fullscreen) {
        show_cursor();
    }

    jessu_printf(THREAD_GL, "cleanup()");

#if USE_D3D
    cleanup_d3d();
#else
    cleanup_gl();
#endif
}

int APIENTRY
WinMain(HINSTANCE hInstance, HINSTANCE /* hPrevInst */,
        LPSTR, int nCmdShow)
{
    debug_output = stdout;  // basically ignored

    seed = (unsigned int)time(NULL);

    int argc = __argc;
    char **argv = __argv;

    /* ---- parse parameters ---------------------------------------- */

    in_screensaver = false;
    in_slideshow = false;
    g_parent_window = NULL;

    if (argc == 1) {
        usage();
    }

    while (argc > 1) {
        if (strncmp(argv[1], "/c", 2) == 0) {
            if (argv[1][2] == ':') {
                g_parent_window = (HWND)atol(argv[1] + 3);
            } else {
                g_parent_window = GetDesktopWindow();
            }
            show_config_window(hInstance, g_parent_window);
            exit(EXIT_SUCCESS);
#if OUTPUT_DEBUG_FILE
        } else if (strcmp(argv[1], "/d") == 0) {
            print_debugging = 1;
            argc--;
            argv++;
#endif
#if !RELEASE_QUALITY
        } else if (strcmp(argv[1], "/D") == 0) {
            display_debugging = 1;
            argc--;
            argv++;
#endif
        } else if (strcmp(argv[1], "/b") == 0) {
            /* benchmark in full-screen mode */
            argc--;
            argv++;
            in_screensaver = true;
            do_benchmark = true;
        } else if (strcmp(argv[1], "/s") == 0) {
            /* regular full-screen */
            argc--;
            argv++;
            in_screensaver = true;
#if !RELEASE_QUALITY
        } else if (strcmp(argv[1], "/show") == 0) {
            /* full-screen slide show */
            argc--;
            argv++;
            if (argc < 2) {
                usage();
            }
            slideshow_file = argv[1];
            argc--;
            argv++;
            in_slideshow = true;
#endif
#if !RELEASE_QUALITY
        } else if (strcmp(argv[1], "/dir") == 0) {
            /* directory specified on command line */
            argc--;
            argv++;
            if (argc < 2) {
                usage();
            }
            directory = argv[1];
            argc--;
            argv++;
#endif
        } else if (strcmp(argv[1], "/p") == 0) {
            /* in a sub-window */
            argc--;
            argv++;
            if (argc < 2) {
                usage();
            }
            g_parent_window = (HWND)atol(argv[1]);
            argc--;
            argv++;
#if !RELEASE_QUALITY
        } else if (strcmp(argv[1], "/seed") == 0) {
            /* set random seed */
            argc--;
            argv++;
            if (argc < 2) {
                usage();
            }
            seed = (unsigned int)atol(argv[1]);
            argc--;
            argv++;
#endif
        } else {
            usage();
        }
    }

#if OUTPUT_DEBUG_FILE
    if (print_debugging) {
        debug_output = fopen(OUTPUT_FILENAME, "w");
        if (debug_output == NULL) {
            MessageBox(NULL,
                    (LPCTSTR)"Couldn't open " OUTPUT_FILENAME ".",
                    "Couldn't open " OUTPUT_FILENAME,
                    MB_OK | MB_ICONEXCLAMATION);
        }
    } else {
        debug_output = stdout;  // basically ignored
    }
#endif

    in_fullscreen = (in_slideshow || in_screensaver);

    if (argc != 1) {
        usage();
    }

    /* ---- debugging output ---------------------------------------- */

    for (int i = 0; i < argc; i++) {
        jessu_printf(THREAD_GL, "-- %s", argv[i]);
    }

    /* ---- get defaults from the registry -------------------------- */

    display_filename = get_show_filenames();
    int use_less_memory = get_less_memory();

    /* ---- seed the random number generator ------------------------ */

    jessu_printf(THREAD_GL, "Random seed = %u", seed);
    srand(seed);

    /* ---- get list of files to display ---------------------------- */

    if (in_slideshow) {
        if (!get_filenames_from_file(slideshow_file)) {
            exit(EXIT_FAILURE);
        }
    } else {
        if (directory == NULL) {
            directory = get_pictures_directory();
        }

        if (is_registered()) {
            jessu_printf(THREAD_GL, "Program is registered");
        } else {
            jessu_printf(THREAD_GL, "Program is not registered, limiting "
                    "to %d images", EVAL_MAX_IMAGES);
            set_max_images(EVAL_MAX_IMAGES);
        }

        strcpy(base_directory, directory);

        if (!start_getting_filenames_from_directory(directory)) {
            if (in_fullscreen) {
                set_error_message("There are no pictures "
                    "in the specified folder.");
            } else {
                set_error_message("There are no pictures "
                    "in the specified folder. Click \"Settings\".");
            }
        }
    }

    /* ---- Create a Windows "class" for Jessu's windows ------------ */

    register_jessu_class(hInstance);

    /* ---- Blank out non-primary monitors -------------------------- */

    if (in_fullscreen) {
        blank_other_monitors(hInstance);
    }

    /* ---- initialize GLUT ----------------------------------------- */

    create_rendering_window(hInstance, nCmdShow);
#if USE_D3D
    if (error_message == NULL) {
        setup_rendering_on_window(rendering_window);
    }
#endif

    /* ---- set up textures ----------------------------------------- */

    probe_rendering_capabilities();
    set_up_textures(!in_fullscreen, use_less_memory);

    /* ---- benchmarking -------------------------------------------- */

#if 0
    if (do_benchmark) {
        FILE *log;

        log = fopen("c:/jessu_benchmark.txt", "w");
        bench_do_benchmarks(log);
        fclose(log);
        exit(0);
    }
#endif

    /* ---- set up graphics ----------------------------------------- */

#if USE_D3D
    // set up blending and other states
    if (g_pD3D != NULL) {
        g_pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        g_pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    }
#else
    if (gl_context != NULL) {
        glMatrixMode(GL_PROJECTION);
        glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(0);
    }
#endif

    /* ---- start the worker thread --------------------------------- */

    if (error_message == NULL) {
        HANDLE worker_thread_handle;
        DWORD worker_thread_id;
        HANDLE graphics_thread_handle;

        g_worker_thread_should_quit = 0;
        worker_thread_handle = CreateThread(NULL, 0, worker_thread, NULL, 0,
                &worker_thread_id);
        graphics_thread_handle = GetCurrentThread();

#if 0
        // starves worker thread
        SetThreadPriority(worker_thread_handle, THREAD_PRIORITY_LOWEST);
        //SetThreadPriority(graphics_thread_handle, THREAD_PRIORITY_HIGHEST);
#endif
    }

    /* ---- seed the system ----------------------------------------- */

    slide[0].time_to_start = 1;

    /* ---- main loop ----------------------------------------------- */

#if !USE_D3D
    if (print_debugging && gl_context != NULL) {
        fprintf(debug_output, "GL_VERSION: %s\n", glGetString(GL_VERSION));
        fprintf(debug_output, "GL_RENDERER: %s\n", glGetString(GL_RENDERER));
        fprintf(debug_output,
                "GL_EXTENSIONS: %s\n", glGetString(GL_EXTENSIONS));
        fprintf(debug_output, "GL_VENDOR: %s\n", glGetString(GL_VENDOR));
    }
#endif

    if (in_fullscreen) {
        hide_cursor();
    }

    handle_events_until_done();
    g_worker_thread_should_quit = 1;

    cleanup();

    fclose(debug_output);

    return EXIT_SUCCESS;
}

