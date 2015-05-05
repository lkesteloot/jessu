
/* 
 * checkx.cpp
 *
 * $Id: checkx.cpp,v 1.1 2004/03/28 23:00:13 lk Exp $
 *
 * Checks whether DirectX 8.0 is installed on the system.
 */

#include <windows.h>
#include <stdio.h>

#include <lmerr.h>

#define ALWAYS_SHOW_ERROR       0

static const char *Direct8_error =
"The Jessu Screen Saver requires the DirectX 8.0 graphics library from Microsoft.\n"
"\n"
"Please go to http://microsoft.com/directx and download and install the\n"
"DirectX runtime.  You may need to download version 9.0, since 8.0 is\n"
"no longer available.\n"
"\n"
"The installation of the Jessu Screen Saver will continue now, but running\n"
"the screen saver will result in an error about \"d3d8.dll\" until DirectX\n"
"is installed.";

#if 0
void DisplayErrorText(DWORD dwLastError)
{
    HMODULE hModule = NULL; // default to system source
    LPSTR MessageBuffer;
    DWORD dwBufferLength;
    char NumberString[16];

    sprintf(NumberString, "%lu", dwLastError);
    MessageBox(NULL,
            (LPCTSTR)NumberString,
            "Error Number",
            MB_OK | MB_ICONEXCLAMATION);

    DWORD dwFormatFlags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_IGNORE_INSERTS |
        FORMAT_MESSAGE_FROM_SYSTEM ;

    //
    // If dwLastError is in the network range, 
    //  load the message source.
    //

    if (dwLastError >= NERR_BASE && dwLastError <= MAX_NERR) {
        hModule = LoadLibraryEx(
                TEXT("netmsg.dll"),
                NULL,
                LOAD_LIBRARY_AS_DATAFILE
                );

        if (hModule != NULL) {
            dwFormatFlags |= FORMAT_MESSAGE_FROM_HMODULE;
        }
    }

    //
    // Call FormatMessage() to allow for message 
    //  text to be acquired from the system 
    //  or from the supplied module handle.
    //

    dwBufferLength = FormatMessageA(
            dwFormatFlags,
            hModule, // module to get message from (NULL == system)
            dwLastError,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), // default language
            (LPSTR) &MessageBuffer,
            0,
            NULL
            );

    if (dwBufferLength != 0) {
        //
        // Output message string on stderr.
        //
        MessageBox(NULL,
                (LPCTSTR)MessageBuffer,
                "Error Message",
                MB_OK | MB_ICONEXCLAMATION);

        //
        // Free the buffer allocated by the system.
        //
        LocalFree(MessageBuffer);
    }

    //
    // If we loaded a message source, unload it.
    //
    if (hModule != NULL) {
        FreeLibrary(hModule);
    }
}
#endif

int APIENTRY
WinMain(HINSTANCE /* hInstance */, HINSTANCE /* hPrevInst */,
        LPSTR, int /* nCmdShow */)
{
    HMODULE mod;

#if ALWAYS_SHOW_ERROR
    mod = NULL;
#else
    mod = LoadLibraryEx("D3D8.DLL", NULL, DONT_RESOLVE_DLL_REFERENCES);
#endif

    if (mod == NULL) {
        // DWORD dwLastError = GetLastError();

        // it'll be dwLastError == 126, but I don't know what to display
        // if it's not, so just show this every time.

        MessageBox(NULL,
                (LPCTSTR)Direct8_error,
                "Jessu Screen Saver",
                MB_OK | MB_ICONEXCLAMATION);

        return 1;
    }

    return 0;
}
