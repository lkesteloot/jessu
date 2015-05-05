
/*
 * LoadDir.cpp
 *
 * $Id: loaddir.cpp,v 1.19 2004/03/06 22:43:04 lk Exp $
 *
 * $Log: loaddir.cpp,v $
 * Revision 1.19  2004/03/06 22:43:04  lk
 * Use the discovered adapters, devices, and modes to choose something
 * good.  If using hardware, then try to find a hardware-accelerated mode
 * that matches the current desktop and is 32 bpp.  If using software
 * or reference implementation, then find the lowest resolution and 16 bpp
 * (but bottom out at 640x480).
 *
 * Also there's full support for multiple monitors (both drawing on them
 * and blanking them out) but it's not enabled since I can't test it.
 *
 * Wow this added about 1700 lines to graphics.cpp.
 *
 * Revision 1.18  2003/09/17 05:10:27  lk
 * Last-minute cleanup, getting ready for release.
 *
 * Revision 1.17  2003/08/03 05:34:19  lk
 * Add pause
 *
 * Revision 1.16  2003/07/23 05:27:21  lk
 * Store and use key for full version.
 *
 * Revision 1.15  2003/07/22 04:50:37  lk
 * Use less memory.
 *
 * Revision 1.14  2003/01/30 06:30:11  lk
 * Skip hidden files and directories
 *
 * Revision 1.13  2003/01/28 02:46:21  lk
 * Much better logging, horizontal resize on load
 *
 * Revision 1.12  2003/01/27 22:07:21  lk
 * Keep around contribution arrays
 *
 * Revision 1.11  2002/06/13 06:56:49  lk
 * First cut at slideshow mode.  Not complete or releasable.
 *
 * Revision 1.10  2002/02/24 19:04:15  lk
 * Smarter about memory usage and seeding random generator.
 *
 * Revision 1.9  2002/02/23 05:12:57  lk
 * Added routine to display error message.
 *
 * Revision 1.8  2002/02/21 22:56:20  lk
 * Whoops, wrong capitalization on RCS tags
 *
 * Revision 1.7  2002/02/21 22:55:15  lk
 * Added RCS tags and per-file comment
 *
 *
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <errno.h>

#include "jessu.h"
#include "loaddir.h"
#include "config.h"

#define EVAL_LIMIT_IMAGE "jessu_limit.jpg"

static int file_count = 0;
static int file_size = 512;
static char **file_names = NULL;
static MISC_INFO **file_misc_info = NULL;
static int file_pointer = 0;

static int max_images = -1;

static int direction = 1;   // 1 = forward, -1 = backward

static int done_loading_files;

static CRITICAL_SECTION loading_files_mutex;

static char *
get_jessu_limit_filename()
{
    char filename[MAX_PATH];

    if (!get_install_dir(filename, sizeof(filename))) {
        // we probably just don't have it at all
        filename[0] = '\0';
    }

    int len = strlen(filename);
    if (len > 0 && filename[len - 1] != '\\') {
        strcat(filename, "\\");
        len++;
    }
    strcat(filename, EVAL_LIMIT_IMAGE);

    jessu_printf(THREAD_LOADDIR, "Jessu limit image: \"%s\"", filename);

    return jessu_strdup(THREAD_LOADDIR, filename, "jessu limit path");
}

void
set_max_images(int max)
{
    max_images = max;
}

static int
get_next_free_entry(bool need_misc_info)
{
    // misc info is for when reading filenames from file (slideshow)

    if (file_names == NULL) {
        file_names = (char **)jessu_malloc(THREAD_LOADDIR,
                file_size*sizeof(char *), "dir path array");
        if (need_misc_info) {
            file_misc_info = (MISC_INFO **)jessu_malloc(THREAD_LOADDIR,
                    file_size*sizeof(MISC_INFO *), "dir misc info array");
        }
    }

    if (file_count < file_size) {
        return file_count++;
    }

    file_size *= 2;
    file_names = (char **)jessu_realloc(THREAD_LOADDIR,
            file_names, file_size*sizeof(char *), "dir path array");
    if (need_misc_info) {
        file_misc_info = (MISC_INFO **)jessu_realloc(THREAD_LOADDIR,
                file_misc_info, file_size*sizeof(MISC_INFO *),
                "dir misc info array");
    }

    return file_count++;
}


static void
load_directory(char *dir)
{
    _finddata_t filestruct;
    long hnd;
    char path[1024];
    char wildcard[1024];
    int len;

    /* add trailing \ if necessary */
    strcpy(path, dir);
    len = strlen(path);
    if (len > 0 && path[len - 1] != '\\') {
        path[len] = '\\';
        path[len + 1] = '\0';
    }

    /* add wildcard */
    strcpy(wildcard, path);
    strcat(wildcard, "*");

    hnd = _findfirst(wildcard, &filestruct);
    if (hnd == -1) {
        return;
    }

    do {
        char *ext;

        if ((filestruct.attrib & _A_HIDDEN) != 0) {
            /* hidden file or directory, do nothing */
        } else if ((filestruct.attrib & _A_SUBDIR) != 0) {
            if (filestruct.name[0] != '.') {
                char subdir[1024];

                strcpy(subdir, path);
                strcat(subdir, filestruct.name);

                load_directory(subdir);
            }
        } else {
            ext = strrchr(filestruct.name, '.');
            if (ext != NULL && (
                        stricmp(ext, ".jpg") == 0 ||
                        stricmp(ext, ".jpeg") == 0)) {

                EnterCriticalSection(&loading_files_mutex);

                int new_entry = get_next_free_entry(false);

                int path_len = strlen(path) + strlen(filestruct.name) + 1;
                file_names[new_entry] = (char *)jessu_malloc(THREAD_LOADDIR,
                        path_len, "dir path");
                sprintf(file_names[new_entry], "%s%s", path, filestruct.name);

                /* swap with random entry to shuffle.  Don't shuffle
                   with an entry that's already been displayed. */
                int other_entry = file_pointer +
                    rand() % (file_count - file_pointer);
                char *tmp = file_names[new_entry];
                file_names[new_entry] = file_names[other_entry];
                file_names[other_entry] = tmp;

                LeaveCriticalSection(&loading_files_mutex);
            }
        }
    } while (!_findnext(hnd, &filestruct) &&
            (max_images == -1 || file_count < max_images));

    if (max_images != -1 && file_count == max_images) {
        // append image that tells user they've got the eval version
        int new_entry = get_next_free_entry(false);

        file_names[new_entry] = get_jessu_limit_filename();
    }

    _findclose(hnd);
}

char *
get_next_filename(MISC_INFO **misc_info)
{
    EnterCriticalSection(&loading_files_mutex);

    char *filename = file_names[file_pointer];
    if (file_misc_info == NULL) {
        *misc_info = NULL;
    } else if (misc_info != NULL) {
        *misc_info = file_misc_info[file_pointer];
    }

    if (direction == 1) {
        file_pointer = (file_pointer + 1) % file_count;
    } else {
        file_pointer = (file_pointer + file_count - 1) % file_count;
    }

    LeaveCriticalSection(&loading_files_mutex);

    return filename;
}


void
set_direction(int dir)
{
    // 1 = forward, -1 = backward
    direction = dir;
}


static unsigned long __stdcall
load_directory_thread(void *params)
{
    char *dir = (char *)params;

    srand(seed);
    load_directory(dir);
    done_loading_files = 1;
    jessu_printf(THREAD_LOADDIR, "Read %d filenames in all", file_count);

    return 0;
}


bool
start_getting_filenames_from_directory(char *directory)
{
    /* returns true if there are pictures, false if there are none */

    DWORD load_thread_id;

    file_count = 0;
    done_loading_files = 0;
    InitializeCriticalSection(&loading_files_mutex);
    CreateThread(NULL, 0, load_directory_thread,
            directory, 0, &load_thread_id);
    while (file_count < 10 && !done_loading_files) {
        /* wait for 10 files so that it's not always the same image we
         * see come up first */
        _sleep(100);
    }

    return file_count > 0;
}


bool
get_filenames_from_file(char *slideshow_file)
{
    FILE *f;
    char buf[4096];

    file_count = 0;

    // we don't use the mutex, but it makes other parts of the code cleaner
    InitializeCriticalSection(&loading_files_mutex);

    f = fopen(slideshow_file, "r");
    if (f == NULL) {
        sprintf(buf, "Cannot open file \"%s\" (%s).",
                slideshow_file, jessu_strerror());
	MessageBox(NULL,
		(LPCTSTR)buf,
		"Cannot Open Slideshow File",
		MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    while (fgets(buf, sizeof(buf), f) != NULL) {
        char *s;
        int seconds;

        // remove comment
        s = strchr(buf, '#');
        if (s != NULL) {
            *s = '\0';
        }

        // remove trailing whitespace
        s = buf + strlen(buf);
        while (s > buf && isspace(s[-1])) {
            s--;
        }
        *s = '\0';

        // skip empty line
        if (s == buf) {
            continue;
        }

        // find trailing number
        s = buf + strlen(buf);
        while (s > buf && isdigit(s[-1])) {
            s--;
        }

        if (s == buf) {
            // line with only digits
            continue;
        }

        if (*s == '\0') {
            // use default
            seconds = 0;
        } else {
            seconds = atoi(s);
        }

        // remove trailing whitespace
        while (s > buf && isspace(s[-1])) {
            s--;
        }
        *s = '\0';

        int new_entry = get_next_free_entry(true);

        int file_len = strlen(buf);
        file_names[new_entry] = (char *)jessu_malloc(THREAD_LOADDIR,
                file_len + 1, "dir entry");
        strcpy(file_names[new_entry], buf);

        MISC_INFO *info = (MISC_INFO *)jessu_malloc(THREAD_LOADDIR,
                sizeof(MISC_INFO), "dir misc info");
        file_misc_info[new_entry] = info;

        info->index = new_entry;
        info->seconds = seconds;
        info->filename = file_names[new_entry];
        s = strrchr(info->filename, '/');
        if (s != NULL) {
            info->local_filename = s + 1;
        } else {
            s = strrchr(info->filename, '\\');
            if (s != NULL) {
                info->local_filename = s + 1;
            } else {
                info->local_filename = info->filename;
            }
        }
    }

    fclose(f);

    if (file_count == 0) {
        sprintf(buf, "Slideshow file \"%s\" contains no filenames.",
                slideshow_file);
	MessageBox(NULL,
		(LPCTSTR)buf,
		"Cannot Run Slideshow",
		MB_OK | MB_ICONEXCLAMATION);
        return false;
    }

    return true;
}

