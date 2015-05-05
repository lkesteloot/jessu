
/*
 * LoadDir.h
 *
 * $Id: loaddir.h,v 1.6 2003/07/22 04:50:37 lk Exp $
 *
 * $Log: loaddir.h,v $
 * Revision 1.6  2003/07/22 04:50:37  lk
 * Use less memory.
 *
 * Revision 1.5  2002/06/13 06:56:49  lk
 * First cut at slideshow mode.  Not complete or releasable.
 *
 * Revision 1.4  2002/02/24 19:04:15  lk
 * Smarter about memory usage and seeding random generator.
 *
 * Revision 1.3  2002/02/21 22:59:56  lk
 * Added RCS tags and per-file comment
 *
 *
 */

#ifndef __LOADDIR_H__
#define __LOADDIR_H__

typedef struct {
    int index;              // index into array of slides
    int seconds;            // seconds to display, or 0 for default
    char *filename;         // full filename on disk
    char *local_filename;   // filename local to folder (NOT ALLOCATED)
} MISC_INFO;

void set_max_images(int max);
bool start_getting_filenames_from_directory(char *directory);
bool get_filenames_from_file(char *slideshow_file);
char *get_next_filename(MISC_INFO **misc_info);
void set_direction(int dir);

#endif /* __LOADDIR_H__ */

