
/*
 * Jessu.h
 *
 * $Id: jessu.h,v 1.14 2004/03/28 02:56:12 lk Exp $
 *
 * $Log: jessu.h,v $
 * Revision 1.14  2004/03/28 02:56:12  lk
 * Ignore mouse motion due to video mode change.
 *
 * Revision 1.13  2004/03/28 00:39:44  lk
 * Fix tile edge clamp problem.
 *
 * Revision 1.12  2004/03/20 02:22:30  lk
 * Code cleanup; allow debug file even in release (to help people with
 * problems).
 *
 * Revision 1.11  2004/03/05 21:57:47  lk
 * Start to put D3D code into separate file (graphics.cpp).
 *
 * Revision 1.10  2004/02/19 06:41:55  lk
 * Larger text for display on TV
 *
 * Revision 1.9  2004/01/18 02:38:45  lk
 * Port to Direct3D
 *
 * Revision 1.8  2003/08/03 05:34:19  lk
 * Add pause
 *
 * Revision 1.7  2003/01/28 05:54:39  lk
 * Even better debugging.
 *
 * Revision 1.6  2003/01/28 02:46:21  lk
 * Much better logging, horizontal resize on load
 *
 * Revision 1.5  2003/01/27 22:07:21  lk
 * Keep around contribution arrays
 *
 * Revision 1.4  2002/02/24 19:04:15  lk
 * Smarter about memory usage and seeding random generator.
 *
 * Revision 1.3  2002/02/21 22:59:56  lk
 * Added RCS tags and per-file comment
 *
 *
 */


#ifndef __JESSU_H__
#define __JESSU_H__


#include <windows.h>
#include <stdio.h>

// 0 for OpenGL, 1 for DirectX:
#define USE_D3D             1

// 1 for TV viewing, 0 for monitor viewing
#define TV_VIEWING          1

// how much to shrink each tile so that we never read past its edge.
// must be a multiple of 0.5.  Typically 0.5 is enough.
#define TILE_SHRINK             0.5

extern FILE *debug_output;

extern int loading_jpeg_progress;
extern int scaling_image_progress;
extern int print_debugging;
extern bool in_fullscreen;   // in_screensaver || in_slideshow

extern unsigned int seed;

enum THREAD_TYPE {
    THREAD_GL,
    THREAD_WORKER,
    THREAD_LOADDIR
};

void *jessu_malloc(THREAD_TYPE thread, size_t size, char *description);
void *jessu_calloc(THREAD_TYPE thread,
        size_t count, size_t size, char *description);
void *jessu_realloc(THREAD_TYPE thread,
        void *ptr, size_t size, char *description);
void jessu_free(THREAD_TYPE thread, void *ptr, char *description);
char *jessu_strdup(THREAD_TYPE thread, const char *string, char *description);

void jessu_printf(THREAD_TYPE thread, char *fmt, ...);
char *jessu_strerror();
void set_error_message(char const *error);

void reset_mouse_motion();

#endif /* __JESSU_H__ */

