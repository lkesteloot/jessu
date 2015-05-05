
/*
 * ScaleTile.cpp
 *
 * Scaling and filter algorithm from Graphics Gems III, "Filtered
 * Image Rescaling", by Dale Schumacher.
 *
 * $Id: scaletile.cpp,v 1.17 2004/03/28 00:39:44 lk Exp $
 *
 */

#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "scaletile.h"
#include "jessu.h"

#if USE_D3D
// ARGB but little-endian
#define DST_RED     2
#define DST_GRN     1
#define DST_BLU     0
#define DST_ALP     3
#else
// RGBA
#define DST_RED     0
#define DST_GRN     1
#define DST_BLU     2
#define DST_ALP     3
#endif

#ifndef M_PI
// how is this not defined in math.h?!
#define M_PI  3.14159
#endif

#define TRANSPARENT_BORDER_WIDTH    1
#define WRITE_OUT_TILES             0
#define PRINT_CONTRIB_ARRAY         0

enum Direction {
    DIRECTION_HORIZONTAL,
    DIRECTION_VERTICAL
};

struct CONTRIB {
    int pixel;
    double weight;
};

struct CLIST {
    int n;          /* number of contributors */
    CONTRIB *p;     /* pointer to list of contributions */
};

static inline double
sinc(double x)
{
    x *= M_PI;
    if (x != 0) {
        return sin(x)/x;
    }

    return 1;
}

#define Lanczos3_support    3

static inline double
Lanczos3_filter(double t)
{
    if (t < 0) {
        t = -t;
    }

    if (t < 3) {
        return sinc(t)*sinc(t/3);
    }

    return 0;
}

static CLIST *
get_contrib_buffer(int length, int contrib_width, Direction direction)
{
    /*
       keep Direction in cache because we'll want one of these for
       horizontal and one for vertical at the same time and we don't
       want them to clobber each other.
    */
    struct CONTRIB_BUFFER_CACHE {
        int length;
        int contrib_width;
        Direction direction;
        CLIST *m_clist;
        CONTRIB_BUFFER_CACHE *next;
    };
    static CONTRIB_BUFFER_CACHE *head = NULL;
    static int cache_size = 0;

    // check if we have a buffer of the right size
    for (CONTRIB_BUFFER_CACHE *p = head; p != NULL; p = p->next) {
        if (p->length == length && p->contrib_width == contrib_width &&
                p->direction == direction) {

            return p->m_clist;
        }
    }

    // no, make one
    CLIST *clist = (CLIST *)jessu_malloc(THREAD_WORKER, length*sizeof(CLIST),
            "scale clist");

    for (int i = 0; i < length; i++) {
        clist[i].n = 0;
        clist[i].p = (CONTRIB *)jessu_calloc(THREAD_WORKER,
                contrib_width, sizeof(CONTRIB), "scale contrib");
    }

    // insert new array into linked list
    p = new CONTRIB_BUFFER_CACHE;
    p->length = length;
    p->contrib_width = contrib_width;
    p->direction = direction;
    p->m_clist = clist;
    p->next = head;
    head = p;

    cache_size++;
    jessu_printf(THREAD_WORKER, "Growing contrib buffer cache to %d %s",
            cache_size, cache_size == 1 ? "entry" : "entries");

    return clist;
}

static CLIST *
make_contrib_table(int src_size, int dst_size, int tile_size,
        Direction direction)
{
    CLIST *contrib;
    int i, j, pixel;
    int left, right;
    int contrib_width;
    double scale;
    double width, fscale, weight;
    double center;
    double fwidth;

    fwidth = Lanczos3_support;

    // the effective destination size takes into account the fact that
    // each tile is shrunk by TILE_SHRINK on each edge, reducing the
    // number of usable pixels.  TILE_SHRINK should be some multiple
    // of 0.5.
    int num_tiles = dst_size / tile_size;
    int effective_tile_size = (int)(tile_size - TILE_SHRINK*2);
    int effective_dst_size = num_tiles * effective_tile_size;

    scale = (double)effective_dst_size / src_size;

    if (scale < 1.0) {
        // making image smaller
        width = fwidth / scale;
        contrib_width = (int)(width*2 + 1);

        contrib = get_contrib_buffer(dst_size, contrib_width, direction);

        fscale = 1.0 / scale;
        for (i = 0; i < dst_size; i++) {
            int tile_number = i / tile_size;
            int tile_offset = i % tile_size;

            // spread out "i" to account for tile shrink
            double effective_offset = tile_offset - TILE_SHRINK;
            double effective_i = tile_number*effective_tile_size +
                effective_offset;

            center = effective_i / scale;
#if PRINT_CONTRIB_ARRAY
            jessu_printf(THREAD_WORKER, "scale=%g, i=%d, tile=%d, offset=%d, "
                    "eoffset=%g, ei=%g, center=%g",
                    scale, i, tile_number, tile_offset, effective_offset,
                    effective_i, center);
#endif

            int n = 0;

            left = (int)ceil(center - width);
            right = (int)floor(center + width);
            for (j = left; j <= right && n < contrib_width; j++) {
                weight = Lanczos3_filter((center - (double)j)/fscale)/fscale;
                if (weight != 0) {
                    pixel = j;

                    // loop in case it bounces more than once
                    for (;;) {
                        if (pixel < 0) {
                            pixel = -pixel;
                        } else if (pixel >= src_size) {
                            pixel = (src_size - pixel) + src_size - 1;
                        } else {
                            break;
                        }
                    }

                    contrib[i].p[n].pixel = pixel;
                    contrib[i].p[n].weight = weight;
                    n++;
                }
            }

            contrib[i].n = n;
        }
    } else {
        contrib_width = (int)(fwidth*2 + 1);

        contrib = get_contrib_buffer(dst_size, contrib_width, direction);

        // making image larger
        for (i = 0; i < dst_size; i++) {
            int tile_number = i / tile_size;
            int tile_offset = i % tile_size;

            // spread out "i" to account for tile shrink
            double effective_offset = tile_offset - TILE_SHRINK;
            double effective_i = tile_number*effective_tile_size +
                effective_offset;

            center = effective_i / scale;
#if PRINT_CONTRIB_ARRAY
            jessu_printf(THREAD_WORKER, "scale=%g, i=%d, tile=%d, offset=%d, "
                    "eoffset=%g, ei=%g, center=%g",
                    scale, i, tile_number, tile_offset, effective_offset,
                    effective_i, center);
#endif

            int n = 0;

            left = (int)ceil(center - fwidth);
            right = (int)floor(center + fwidth);

            for (j = left; j <= right && n < contrib_width; j++) {
                weight = Lanczos3_filter(center - (double)j);
                if (weight != 0) {
                    pixel = j;

                    // loop in case it bounces more than once
                    for (;;) {
                        if (pixel < 0) {
                            pixel = -pixel;
                        } else if (pixel >= src_size) {
                            pixel = (src_size - pixel) + src_size - 1;
                        } else {
                            break;
                        }
                    }

                    contrib[i].p[n].pixel = pixel;
                    contrib[i].p[n].weight = weight;
                    n++;
                }
            }

            contrib[i].n = n;
        }
    }

    return contrib;
}

inline unsigned char clamp_color(double color)
{
    int j = (int)color;

    if (j < 0) {
        j = 0;
    }
    if (j > 255) {
        j = 255;
    }

    return (unsigned char)j;
}

CLIST *get_scale_row_data(int src_size, int dst_size, int tile_size)
{
    /* OKAY so here we return a pointer to an array, and that array is passed
       into the scale_row() function, but we've got no guarantee that someone
       else won't call "make_contrib_table()" with similar enough parameters
       to reuse the array but different enough to get different coefficients.
       In the current program this won't happen because only the worker
       thread does scaling and it's either scaling or reading.  But I
       recognize that this is potentially bad. */

    return make_contrib_table(src_size, dst_size, tile_size,
            DIRECTION_HORIZONTAL);
}

void scale_row(CLIST *clist, unsigned char *src,
        unsigned char *dst, int dst_size)
{
    CLIST *c = &clist[0];
    CONTRIB *p;

    for (int i = 0; i < dst_size; i++) {
        double red = 0;
        double grn = 0;
        double blu = 0;

        p = &c->p[0];
        for (int j = 0; j < c->n; j++) {
            unsigned char *s = &src[p->pixel*BYTES_PER_PIXEL];
            double weight = p->weight;

            red += s[0]*weight;
            grn += s[1]*weight;
            blu += s[2]*weight;

            p++;
        }

        dst[0] = clamp_color(red);
        dst[1] = clamp_color(grn);
        dst[2] = clamp_color(blu);
        dst += BYTES_PER_PIXEL;

        c++;
    }
}

// --------------------------------------------------------------------------

Vertical_scaler::Vertical_scaler()
{
    m_clist = NULL;
    m_in_queue = NULL;
    m_cannot_do_rows = NULL;
    m_parameters_changed = true;
    m_cannot_do_rows_allocated_size = 0;
    m_in_queue_allocated_size = 0;
}

Vertical_scaler::~Vertical_scaler()
{
    // don't free clist, it's cached elsewhere
    // don't free tile, it was allocated elsewhere

    delete[] m_in_queue;
    delete[] m_cannot_do_rows;
}

void Vertical_scaler::Set_destination_parameters(unsigned char **tile,
        int tile_size_x, int tile_size_y,
        int tile_count_x, int tile_count_y,
        int texture_size_x, int texture_size_y)
{
    this->m_tile = tile;
    this->m_tile_size_x = tile_size_x;
    this->m_tile_size_y = tile_size_y;
    this->m_tile_count_x = tile_count_x;
    this->m_tile_count_y = tile_count_y;
    this->m_texture_size_x = texture_size_x;
    this->m_texture_size_y = texture_size_y;

    m_parameters_changed = true;
}

void Vertical_scaler::Set_source_parameters(int src_size_x, int src_size_y)
{
    this->m_src_size_x = src_size_x;
    this->m_src_size_y = src_size_y;

    m_parameters_changed = true;
}

void Vertical_scaler::Setup()
{
    if (!m_parameters_changed) {
        return;
    }

    m_clist = make_contrib_table(m_src_size_y, m_texture_size_y,
            m_tile_size_y, DIRECTION_VERTICAL);
    m_start_dst_y = 0;

    /* create the array that tells us which rows we can do once we
       have row y.  */

    // reallocate array
    if (m_cannot_do_rows_allocated_size < m_src_size_y) {
        delete[] m_cannot_do_rows;
        m_cannot_do_rows = new int[m_src_size_y];
        m_cannot_do_rows_allocated_size = m_src_size_y;
    }

    int cannot_do_dst_y = 0;
    for (int src_y = 0; src_y < m_src_size_y; src_y++) {
        while (cannot_do_dst_y < m_texture_size_y) {
            CLIST *c = &m_clist[cannot_do_dst_y];
            CONTRIB *p = &c->p[0];

            // see whether we could do "cannot_do_dst_y" if we had everything
            // up to and including "src_y".
            for (int i = 0; i < c->n; i++) {
                int required_src_y = p->pixel;
                if (required_src_y > src_y) {
                    // accesses a row we don't have, can't do this one.
                    // (this breaks out of both loops, does not increment
                    // cannot_do_dst_y)
                    break;
                }
                p++;
            }

            if (i < c->n) {
                break;
            }

            cannot_do_dst_y++;
        }

        // record the highest dst_y that we cannot do if we have this src_y.
        // we can do everything earlier than "cannot_do_dst_y".
        m_cannot_do_rows[src_y] = cannot_do_dst_y;
    }

    /*
     * create the in queue of source rows.  this is a circular buffer
     * where m_in_queue[y % m_in_queue_rows] has the data for row y.
     */
    m_in_queue_rows = 1;
#if 0
    // find the destination row that uses the most number of source rows.
    // this is now insufficient because it doesn't take into account
    // the fact that we might go backwards, thanks to TILE_SHRINK.
    for (int y = 0; y < m_texture_size_y; y++) {
        CLIST *c = &m_clist[y];
        if (c->n > m_in_queue_rows) {
            m_in_queue_rows = c->n;
        }
    }
#else
    // find the largest number of source rows that we need to keep.
    // do this by simulating what we'll do later.
    int start_dst_y = 0;
    for (src_y = 0; src_y < m_src_size_y; src_y++) {
        cannot_do_dst_y = m_cannot_do_rows[src_y];

        for (int dst_y = start_dst_y; dst_y < cannot_do_dst_y; dst_y++) {
            CLIST *c = &m_clist[dst_y];
            for (int i = 0; i < c->n; i++) {
                int diff = src_y - c->p[i].pixel + 1;

                if (diff > m_in_queue_rows) {
                    m_in_queue_rows = diff;
                }
            }
        }

        start_dst_y = cannot_do_dst_y;
    }
#endif

    jessu_printf(THREAD_WORKER, "Using %d rows in circular input buffer",
            m_in_queue_rows);
    int new_in_queue_size = m_texture_size_x*m_in_queue_rows*BYTES_PER_PIXEL;
    if (m_in_queue_allocated_size < new_in_queue_size) {
        delete[] m_in_queue;
        m_in_queue = new unsigned char[new_in_queue_size];
        m_in_queue_allocated_size = new_in_queue_size;
    }

    m_parameters_changed = false;
}

unsigned char *Vertical_scaler::Get_row_buffer(int src_y)
{
    Setup();

    int in_queue_y = src_y % m_in_queue_rows;

    return m_in_queue + in_queue_y*m_texture_size_x*BYTES_PER_PIXEL;
}

void Vertical_scaler::Process_row(int src_y)
{
    Setup();

    // assume new row is already in circular buffer

    // look up src_y in array to find first row that we cannot do
    int cannot_do_dst_y = m_cannot_do_rows[src_y];

    // go from m_start_dst_y to the last row we can do
    for (int y = m_start_dst_y; y < cannot_do_dst_y; y++) {
        Scale_row(y);
    }

    // set m_start_dst_y to the next row to do
    m_start_dst_y = cannot_do_dst_y;
}

void Vertical_scaler::Scale_row(int dst_y)
{
    int ty = dst_y/m_tile_size_y;
    CLIST *c = &m_clist[dst_y];

    for (int tx = 0; tx < m_tile_count_x; tx++) {
        unsigned char *t = m_tile[ty*m_tile_count_x + tx];
        unsigned char *dst = t +
            (dst_y - ty*m_tile_size_y)*m_tile_size_x*BYTES_PER_TEXEL;

        // this is also src_x since the rows are the same width now
        int dst_x = tx*m_tile_size_x;

        for (int x = 0; x < m_tile_size_x; x++) {
            double red = 0;
            double grn = 0;
            double blu = 0;
            CONTRIB *p = &c->p[0];

            for (int j = 0; j < c->n; j++) {
                int in_row = p->pixel % m_in_queue_rows;
                unsigned char *s = &m_in_queue[(dst_x +
                        in_row*m_texture_size_x)*BYTES_PER_PIXEL];
                double weight = p->weight;

                red += s[0]*weight;
                grn += s[1]*weight;
                blu += s[2]*weight;
                p++;
            }

            dst[DST_RED] = clamp_color(red);
            dst[DST_GRN] = clamp_color(grn);
            dst[DST_BLU] = clamp_color(blu);
            dst[DST_ALP] = 255;  // we'll fix it up after the image is done

            dst += BYTES_PER_TEXEL;

            dst_x++;
        }
    }

    if (dst_y == m_src_size_y - 1) {
        Finish_image();
    }
}

void Vertical_scaler::Finish_image()
{
    int i;
    int tx, ty;
    int x, y;  /* within tile */
    unsigned char *t;
    unsigned char *dst;

    // make the border transparent

    for (ty = 0; ty < m_tile_count_y; ty++) {
        // left edge
        t = m_tile[ty*m_tile_count_x];
        dst = t;
        for (y = 0; y < m_tile_size_y; y++) {
            for (i = 0; i < TRANSPARENT_BORDER_WIDTH; i++) {
                dst[i*BYTES_PER_TEXEL + DST_ALP] = 0;
            }
            dst += BYTES_PER_TEXEL*m_tile_size_x;
        }

        // right edge
        t = m_tile[ty*m_tile_count_x + m_tile_count_x - 1];
        dst = t + BYTES_PER_TEXEL*(m_tile_size_x - TRANSPARENT_BORDER_WIDTH);
        for (y = 0; y < m_tile_size_y; y++) {
            for (i = 0; i < TRANSPARENT_BORDER_WIDTH; i++) {
                dst[i*BYTES_PER_TEXEL + DST_ALP] = 0;
            }
            dst += BYTES_PER_TEXEL*m_tile_size_x;
        }
    }

    for (tx = 0; tx < m_tile_count_x; tx++) {
        // top edge
        t = m_tile[tx];
        dst = t;
        for (x = 0; x < m_tile_size_x; x++) {
            for (i = 0; i < TRANSPARENT_BORDER_WIDTH; i++) {
                dst[i*(BYTES_PER_TEXEL*m_tile_size_x) + DST_ALP] = 0;
            }
            dst += BYTES_PER_TEXEL;
        }

        // bottom edge
        t = m_tile[(m_tile_count_y - 1)*m_tile_count_x + tx];
        dst = t + BYTES_PER_TEXEL*m_tile_size_x*
            (m_tile_size_y - TRANSPARENT_BORDER_WIDTH);
        for (x = 0; x < m_tile_size_x; x++) {
            for (i = 0; i < TRANSPARENT_BORDER_WIDTH; i++) {
                dst[i*(BYTES_PER_TEXEL*m_tile_size_x) + DST_ALP] = 0;
            }
            dst += BYTES_PER_TEXEL;
        }
    }

#if WRITE_OUT_TILES
    for (ty = 0; ty < m_tile_count_y; ty++) {
        for (tx = 0; tx < m_tile_count_x; tx++) {
            int tile_number = ty*m_tile_count_x + tx;
            t = m_tile[tile_number];

            char filename[256];
            sprintf(filename, "\\tile%02d.ppm", tile_number);

            FILE *f = fopen(filename, "w");

            fprintf(f, "P6 %d %d 255 ", m_tile_size_x, m_tile_size_y);
            for (int i = 0; i < m_tile_size_x*m_tile_size_y; i++) {
                fprintf(f, "%c%c%c",
                        t[i*BYTES_PER_TEXEL + DST_RED],
                        t[i*BYTES_PER_TEXEL + DST_GRN],
                        t[i*BYTES_PER_TEXEL + DST_BLU]);
            }

            fclose(f);
        }
    }
#endif
}
