
/*
 * Config.h
 *
 * $Id: config.h,v 1.9 2004/01/18 02:38:45 lk Exp $
 *
 * $Log: config.h,v $
 * Revision 1.9  2004/01/18 02:38:45  lk
 * Port to Direct3D
 *
 * Revision 1.8  2003/09/17 05:10:27  lk
 * Last-minute cleanup, getting ready for release.
 *
 * Revision 1.7  2003/07/25 06:14:35  lk
 * Flags for less memory and show filenames
 *
 * Revision 1.6  2003/07/22 04:50:37  lk
 * Use less memory.
 *
 * Revision 1.5  2002/02/21 22:59:56  lk
 * Added RCS tags and per-file comment
 *
 *
 */

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include <windows.h>

bool get_install_dir(char *filename, int max_length);
void show_config_window(HINSTANCE hInstance, HWND parent_window);
char *get_pictures_directory(void);
bool is_registered(void);
int get_less_memory();
bool get_show_filenames();

#endif /* __CONFIG_H__ */
