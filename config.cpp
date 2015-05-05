
/*
 * Config.cpp
 *
 * $Id: config.cpp,v 1.21 2004/03/28 00:39:44 lk Exp $
 */

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <io.h>

#include "config.h"
#include "jessu.h"
#include "resource.h"
#include "geteventname.h"
#include "key.h"

#define REGISTRY_KEY                "Control Panel\\Screen Saver.Jessu"
#define REGISTRY_DIR_VALUE          "ImageDirectory"
#define REGISTRY_REGKEY_VALUE       "RegistrationKey"
#define REGISTRY_LESSMEM_VALUE      "UseLessMemory"
#define REGISTRY_SHOWNAME_VALUE     "ShowFilenames"
#define REGISTRY_INSTALLDIR_VALUE   "InstallDir"

#define DEFAULT_DIR                 "C:\\My Documents"

#define MAX_INT_LENGTH              16

static HINSTANCE hInst;

static char *initial_browse_dir = NULL;

bool
get_install_dir(char *filename, int max_length)
{
    long result;
    HKEY hKey;
    DWORD length;
    bool success = false;

    result = RegOpenKeyEx(HKEY_CURRENT_USER,
            REGISTRY_KEY, NULL, KEY_QUERY_VALUE, &hKey);
    if (result == ERROR_SUCCESS) {
        length = max_length;
        result = RegQueryValueEx(hKey, REGISTRY_INSTALLDIR_VALUE,
                NULL, NULL, (unsigned char *)filename, &length);
        if (result == ERROR_SUCCESS) {
            success = true;
        }
        RegCloseKey(hKey);
    }

    return success;
}

static void
set_string(char *value, char *s)
{
    long result;
    HKEY hKey;

    result = RegCreateKeyEx(HKEY_CURRENT_USER,
            REGISTRY_KEY, 0, NULL,
            REG_OPTION_NON_VOLATILE,
            KEY_ALL_ACCESS, NULL, &hKey, NULL);
    if (result == ERROR_SUCCESS) {
        if (RegSetValueEx(hKey, value,
                    0, REG_SZ, (unsigned char *)s,
                    strlen(s) + 1) == ERROR_SUCCESS) {
            /* Yay! */
        } else {
            MessageBox(NULL,
                    (LPCTSTR)"Cannot save registry entry.",
                    "Cannot Save Options",
                    MB_OK | MB_ICONINFORMATION);
        }
        RegCloseKey(hKey);
    } else {
        MessageBox(NULL,
                (LPCTSTR)"Cannot create registry entry.",
                "Cannot Save Options",
                MB_OK | MB_ICONINFORMATION);
    }
}

static void
set_int(char *value, int i)
{
    char buffer[64];

    sprintf(buffer, "%d", i);
    set_string(value, buffer);
}

static int
get_int(char *value, int default_value)
{
    long result;
    HKEY hKey;
    DWORD length;
    static char buffer[MAX_INT_LENGTH];
    int i = default_value;

    result = RegOpenKeyEx(HKEY_CURRENT_USER,
            REGISTRY_KEY, NULL, KEY_QUERY_VALUE, &hKey);
    if (result == ERROR_SUCCESS) {
        length = sizeof(buffer);
        result = RegQueryValueEx(hKey, value,
                NULL, NULL, (unsigned char *)buffer, &length);
        if (result == ERROR_SUCCESS) {
            i = atoi(buffer);
        }
        RegCloseKey(hKey);
    }

    return i;
}

static char *
get_error_message(int error_number)
{
    LPVOID lpMsgBuf;

    FormatMessage( 
            FORMAT_MESSAGE_ALLOCATE_BUFFER | 
            FORMAT_MESSAGE_FROM_SYSTEM | 
            FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error_number,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // Default language
            (LPTSTR) &lpMsgBuf,
            0,
            NULL);

    return (char *)lpMsgBuf;
}

static char *
get_default_dir(void)
{
    /* this is My Documents\My Pictures */

    char *dir;
    LPITEMIDLIST lpID;
    static char my_documents[MAX_PATH];
    static char my_pictures[MAX_PATH];

    dir = DEFAULT_DIR;

    if (SHGetSpecialFolderLocation(NULL, CSIDL_PERSONAL, &lpID) != NOERROR) {
        return dir;
    }

    if (!SHGetPathFromIDList(lpID, my_documents)) {
        return dir;
    }

    strcpy(my_pictures, my_documents);
    strcat(my_pictures, "\\My Pictures");

    /* check if "My Pictures" exists */
    if (_access(my_pictures, 0) == -1) {
        /* if not, try "My Documents" */
        if (_access(my_documents, 0) == -1) {
            /* crap, pick something safe */
            strcpy(my_documents, DEFAULT_DIR);
        }
        dir = my_documents;
    } else {
        dir = my_pictures;
    }

    return dir;
}

char *
get_pictures_directory(void)
{
    long result;
    HKEY hKey;
    DWORD length;
    static char cur_folder[MAX_PATH];
    char *dir;

    result = RegOpenKeyEx(HKEY_CURRENT_USER,
            REGISTRY_KEY, NULL, KEY_QUERY_VALUE, &hKey);
    if (result == ERROR_SUCCESS) {
        length = sizeof(cur_folder);
        result = RegQueryValueEx(hKey, REGISTRY_DIR_VALUE,
                NULL, NULL, (unsigned char *)cur_folder, &length);
        if (result == ERROR_SUCCESS) {
            dir = cur_folder;
        } else {
            dir = get_default_dir();
        }
        RegCloseKey(hKey);
    } else {
        dir = get_default_dir();
    }

    return dir;
}

static char const *
get_key()
{
    long result;
    HKEY hKey;
    DWORD length;
    static char key_buffer[MAX_KEY_LENGTH];
    char const *key = "";

    result = RegOpenKeyEx(HKEY_CURRENT_USER,
            REGISTRY_KEY, NULL, KEY_QUERY_VALUE, &hKey);
    if (result == ERROR_SUCCESS) {
        length = sizeof(key_buffer);
        result = RegQueryValueEx(hKey, REGISTRY_REGKEY_VALUE,
                NULL, NULL, (unsigned char *)key_buffer, &length);
        if (result == ERROR_SUCCESS) {
            key = key_buffer;
        }
        RegCloseKey(hKey);
    }

    return key;
}

int
get_less_memory()
{
    return get_int(REGISTRY_LESSMEM_VALUE, 0);
}

bool
get_show_filenames()
{
    return get_int(REGISTRY_SHOWNAME_VALUE, 0) != 0;
}

static void
set_pictures_directory(char *dir)
{
    set_string(REGISTRY_DIR_VALUE, dir);
}

static void
set_key(char *key)
{
    set_string(REGISTRY_REGKEY_VALUE, key);
}

static void
set_less_memory(int less_memory)
{
    set_int(REGISTRY_LESSMEM_VALUE, less_memory);
}

static void
set_show_filenames(int show_filenames)
{
    set_int(REGISTRY_SHOWNAME_VALUE, show_filenames);
}

bool
is_registered(void)
{
    return key_is_valid(get_key());
}

static int CALLBACK
BrowseCallbackProc(HWND hWnd, UINT uMsg, LPARAM /* lParam */,
        LPARAM /* lpData */)
{
    switch (uMsg) {
        case BFFM_INITIALIZED:
            if (initial_browse_dir != NULL) {
                SendMessage(hWnd, BFFM_SETSELECTION, TRUE,
                        (LPARAM)initial_browse_dir);
            }
            break;
    }

    return 0;
}

static char *
browse_for_folder(HWND hDlg, char *dir)
{
    BROWSEINFO bi;
    static char chosen_dir[MAX_PATH];

    bi.hwndOwner = hDlg;
    bi.pidlRoot = NULL;
    bi.pszDisplayName = chosen_dir;
    bi.lpszTitle = "Choose a folder from which to display images:";
    bi.ulFlags = BIF_RETURNONLYFSDIRS;
    bi.lpfn = BrowseCallbackProc;
    bi.lParam = 0;
    bi.iImage = 0;

    initial_browse_dir = dir;

    LPITEMIDLIST lpID = SHBrowseForFolder(&bi);
    if (lpID != NULL) {
        if (SHGetPathFromIDList(lpID, chosen_dir)) {
            return chosen_dir;
        } else {
            /* Function failed for some reason.  This shouldn't happen
             * because we have BIF_RETURNONLYFSDIRS above. */
            MessageBox(NULL, (LPCTSTR)"The path you selected is not "
                    "a real folder.",
                    "Cannot Select Folder", MB_OK | MB_ICONINFORMATION);
        }
    }

    return NULL;
}

static int CALLBACK
AboutDlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM /* lParam */)
{
#if 0
    fprintf(debug_output, "AboutDlgProc: dlg %x message \"%s\"\n", hDlg,
            get_event_name(message));
#endif

    switch (message) {
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_OK:
                    EndDialog(hDlg, IDC_OK);
                    break;
            }
            break;

        case WM_CLOSE:
            EndDialog(hDlg, IDC_OK);
            break;

        default:
            return FALSE;
    }

    return TRUE;
}

static int CALLBACK
DlgProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM /* lParam */)
{
    char *new_folder;
    char cur_folder[MAX_PATH];
    char key[MAX_KEY_LENGTH];
    int result;

#if 0
    fprintf(debug_output, "DlgProc: dlg %x message \"%s\"\n", hDlg,
            get_event_name(message));
#endif

    switch (message) {
        case WM_INITDIALOG:
            SetDlgItemText(hDlg, IDC_FOLDER, get_pictures_directory());
            SetDlgItemText(hDlg, IDC_KEY, get_key());
            CheckDlgButton(hDlg, IDC_LESS_MEMORY, get_less_memory());
            CheckDlgButton(hDlg, IDC_SHOW_FILENAMES, get_show_filenames());
            break;

        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BROWSE:
                    GetDlgItemText(hDlg, IDC_FOLDER, cur_folder,
                            sizeof(cur_folder));
                    new_folder = browse_for_folder(hDlg, cur_folder);
                    if (new_folder != NULL) {
                        SetDlgItemText(hDlg, IDC_FOLDER, new_folder);
                    }
                    break;

                case IDC_OK:
                    GetDlgItemText(hDlg, IDC_FOLDER, cur_folder,
                            sizeof(cur_folder));
                    GetDlgItemText(hDlg, IDC_KEY, key, sizeof(key));
                    if (strlen(cur_folder) == 0) {
                        MessageBox(NULL, (LPCTSTR)"You must specify a folder.",
                                "No Folder Name",
                                MB_OK | MB_ICONINFORMATION);
                    } else {
                        /* save data to registry */
                        set_pictures_directory(cur_folder);
                        set_key(key);
                        set_less_memory(
                                IsDlgButtonChecked(hDlg, IDC_LESS_MEMORY));
                        set_show_filenames(
                                IsDlgButtonChecked(hDlg, IDC_SHOW_FILENAMES));
                        EndDialog(hDlg, IDC_OK);
                    }
                    break;

                case IDC_CANCEL:
                    EndDialog(hDlg, IDC_CANCEL);
                    break;

                case IDC_ABOUT:
                    result = DialogBox(hInst, "ABOUT", hDlg, AboutDlgProc);
                    if (result == -1) {
                        MessageBox(NULL,
                                (LPCTSTR)get_error_message(GetLastError()),
                                "Cannot Create About Window",
                                MB_OK | MB_ICONINFORMATION);
                    }
                    break;
            }
            break;

        case WM_CLOSE:
            EndDialog(hDlg, IDC_CANCEL);
            break;

        default:
            return FALSE;
    }

    return TRUE;
}


static void
get_message_font(void)
{
    char *values[] = {
        "CaptionFont", "IconFont", "MenuFont", "MessageFont",
        "SmCaptionFont", "StatusFont"
    };
    int values_count = sizeof(values)/sizeof(values[0]);

    // Find out the correct entry in the registry
    HKEY hKey = 0;
    if ( ERROR_SUCCESS != RegOpenKeyEx(
                HKEY_CURRENT_USER,
                "Control Panel\\Desktop\\WindowMetrics",
                0,
                KEY_QUERY_VALUE,
                &hKey ) ) {

        fprintf(debug_output, "Cannot get metrics key\n");
        return;
    }

    for (int j = 0; j < values_count; j++) {
        LOGFONTW	font;
        DWORD	size = sizeof(font);
        if (RegQueryValueEx(hKey, values[j],
                    NULL, NULL, (LPBYTE)&font, &size) != ERROR_SUCCESS)
        {
            RegCloseKey( hKey );
            fprintf(debug_output, "Cannot get metrics value\n");
            return;
        }

        // getting number of pixels per logical inch along the display height
        HDC hDC    = GetDC(NULL);
        int nLPixY = GetDeviceCaps(hDC, LOGPIXELSY);
        ReleaseDC(NULL,hDC);

        int nPointSize  = -MulDiv(font.lfHeight,72,nLPixY);
        char strFontName[LF_FACESIZE + 1];
        for (int i = 0; i < LF_FACESIZE; i++) {
            strFontName[i] = (char)font.lfFaceName[i];
        }
        strFontName[LF_FACESIZE] = '\0';

        fprintf(debug_output, "Value = \"%s\", font = \"%s\", size = %d\n",
                values[j], strFontName, nPointSize);
    }

    RegCloseKey( hKey );
}


void
show_config_window(HINSTANCE hInstance, HWND parent_window)
{
    InitCommonControls();

    hInst = hInstance;

    get_message_font();

    int result = DialogBox(hInstance, "CONFIG", parent_window, DlgProc);
    if (result == -1) {
        MessageBox(NULL, (LPCTSTR)get_error_message(GetLastError()),
                "Cannot Create Window", MB_OK | MB_ICONINFORMATION);
    }

    /* here we don't care what "result" is -- whether it's okay or cancel
     * has already been handled in the callback. */
}


