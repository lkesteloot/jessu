
/*
 * ScaleTile.h
 *
 * $Id: scaletile.h,v 1.6 2004/03/28 00:39:44 lk Exp $
 *
 * $Log: scaletile.h,v $
 * Revision 1.6  2004/03/28 00:39:44  lk
 * Fix tile edge clamp problem.
 *
 * Revision 1.5  2003/07/22 04:50:37  lk
 * Use less memory.
 *
 * Revision 1.4  2003/01/28 03:27:54  lk
 * Nicer scaling horizontally.
 *
 * Revision 1.3  2002/02/22 05:05:44  lk
 * Add alpha border to avoid jittering edge
 *
 * Revision 1.2  2002/02/21 22:59:56  lk
 * Added RCS tags and per-file comment
 *
 *
 */

#ifndef __SCALETILE_H__
#define __SCALETILE_H__

#define BYTES_PER_PIXEL         3   // input image
#define BYTES_PER_TEXEL         4   // output texture

struct CLIST;

void scale_and_tile(unsigned char *pixels, int width, int height,
        unsigned char **tile, int tile_size_x, int tile_size_y,
        int tile_count_x, int tile_count_y,
        int texture_size_x, int texture_size_y);

CLIST *get_scale_row_data(int src_size, int dst_size, int tile_size);
void scale_row(CLIST *clist, unsigned char *src,
        unsigned char *dst, int dst_size);

class Vertical_scaler {
public:
    Vertical_scaler();
    ~Vertical_scaler();

    void Set_destination_parameters(
            unsigned char **tile, int tile_size_x, int tile_size_y,
            int tile_count_x, int tile_count_y,
            int texture_size_x, int texture_size_y);
    void Set_source_parameters(int src_size_x, int src_size_y);

    unsigned char *Get_row_buffer(int src_y);
    void Process_row(int src_y);

    int m_src_size_x;
    int m_src_size_y;
    unsigned char **m_tile;
    int m_tile_size_x;
    int m_tile_size_y;
    int m_tile_count_x;
    int m_tile_count_y;
    int m_texture_size_x;
    int m_texture_size_y;

private:
    void Setup();
    void Scale_row(int dst_y);
    void Finish_image();

    bool m_parameters_changed;

    CLIST *m_clist;
    int m_start_dst_y;

    int m_cannot_do_rows_allocated_size;
    int *m_cannot_do_rows;

    int m_in_queue_rows;
    int m_in_queue_allocated_size;
    unsigned char *m_in_queue;
};

#endif  /* __SCALETILE_H__ */
