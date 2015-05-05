
/*
 * FileRead.h
 *
 * $Id: fileread.h,v 1.6 2003/07/22 04:50:37 lk Exp $
 *
 * $Log: fileread.h,v $
 * Revision 1.6  2003/07/22 04:50:37  lk
 * Use less memory.
 *
 * Revision 1.5  2003/01/28 02:46:21  lk
 * Much better logging, horizontal resize on load
 *
 * Revision 1.4  2002/02/21 22:59:56  lk
 * Added RCS tags and per-file comment
 *
 *
 */

#ifndef __FILEREAD_H__
#define __FILEREAD_H__


#include <stdio.h>

#include "scaletile.h"

// fills the tiles as set up by the vertical scaler
int read_image(char *name, FILE *fp, Vertical_scaler &vertical_scaler,
        int *width, int *height);

#endif /* __FILEREAD_H__ */

