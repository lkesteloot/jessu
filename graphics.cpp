
/* Direct3D graphics support routines and globals */

#include <Windows.h>
#define COMPILE_MULTIMON_STUBS
#include <multimon.h>

#include "graphics.hpp"

#if USE_D3D

#define D3DAPPERR_NODIRECT3D          0x82000001
#define D3DAPPERR_NOWINDOW            0x82000002
#define D3DAPPERR_NOCOMPATIBLEDEVICES 0x82000003
#define D3DAPPERR_NOWINDOWABLEDEVICES 0x82000004
#define D3DAPPERR_NOHARDWAREDEVICE    0x82000005
#define D3DAPPERR_HALNOTCOMPATIBLE    0x82000006
#define D3DAPPERR_NOWINDOWEDHAL       0x82000007
#define D3DAPPERR_NODESKTOPHAL        0x82000008
#define D3DAPPERR_NOHALTHISMODE       0x82000009
#define D3DAPPERR_NONZEROREFCOUNT     0x8200000a
#define D3DAPPERR_MEDIANOTFOUND       0x8200000b
#define D3DAPPERR_RESIZEFAILED        0x8200000c
#define D3DAPPERR_INITDEVICEOBJECTSFAILED 0x8200000d
#define D3DAPPERR_CREATEDEVICEFAILED  0x8200000e
#define D3DAPPERR_NOPREVIEW           0x8200000f

LPDIRECT3D8 g_pD3D = NULL;
LPDIRECT3DDEVICE8 g_pd3dDevice = NULL;
LPDIRECT3DVERTEXBUFFER8 g_pVB = NULL;

// put configuration here
static DWORD g_dwMinDepthBits = 0;      // don't use z-buffer
static DWORD g_dwMinStencilBits = 0;    // don't use stencil
static bool g_bMultithreaded = false;   // graphics aren't multithreaded
static bool g_bUseDepthBuffer = false;  // don't use z-buffer
static bool g_bOneScreenOnly = true;    // keep other screens black
static bool g_bAllowRef = true;         // whether to allow REF D3D device
static bool g_bDisableHW = false;       // for testing, force software or ref
// SwapEffect in fullscreen:
static D3DSWAPEFFECT m_SwapEffectFullscreen = D3DSWAPEFFECT_DISCARD;
// SwapEffect in windowed:
static D3DSWAPEFFECT m_SwapEffectWindowed = D3DSWAPEFFECT_COPY_VSYNC;

//-----------------------------------------------------------------------------
// Name: struct MonitorInfo
// Desc: Structure for holding information about a monitor
//-----------------------------------------------------------------------------
struct MonitorInfo {
    TCHAR          strDeviceName[128];
    TCHAR          strMonitorName[128];
    HMONITOR       hMonitor;
    RECT           rcScreen;
    DWORD          iAdapter; // Which D3DAdapterInfo corresponds to this monitor
    HWND           hWnd;

    // Error message state
    FLOAT          xError;
    FLOAT          yError;
    FLOAT          widthError;
    FLOAT          heightError;
    FLOAT          xVelError;
    FLOAT          yVelError;

    void dump() {
        jessu_printf(THREAD_GL, "-- device \"%s\"", strDeviceName);
        jessu_printf(THREAD_GL, "-- monitor \"%s\"", strMonitorName);
        jessu_printf(THREAD_GL, "-- size %dx%d",
                rcScreen.right - rcScreen.left,
                rcScreen.bottom - rcScreen.top);
    }
};

static const DWORD MAX_DISPLAYS = 9;
static const DWORD NO_ADAPTER = 0xffffffff;
static const DWORD NO_MONITOR = 0xffffffff;

struct MonitorsInfo {
    DWORD m_dwNumMonitors;
    MonitorInfo m_Monitors[MAX_DISPLAYS];

    MonitorsInfo() {
        m_dwNumMonitors = 0;
    }

    bool addMonitor(MonitorInfo *newMonitor) {
        if (m_dwNumMonitors == MAX_DISPLAYS) {
            return false;
        }

        m_Monitors[m_dwNumMonitors++] = *newMonitor;
        return true;
    }

    void dump() {
        jessu_printf(THREAD_GL, "Number of monitors: %lu", m_dwNumMonitors);

        for (DWORD i = 0; i < m_dwNumMonitors; i++) {
            m_Monitors[i].dump();
        }
    }
};

//-----------------------------------------------------------------------------
// Name: struct D3DModeInfo
// Desc: Structure for holding information about a display mode
//-----------------------------------------------------------------------------
struct D3DModeInfo {
    DWORD      Width;      // Screen width in this mode
    DWORD      Height;     // Screen height in this mode
    D3DFORMAT  Format;     // Pixel format in this mode
    DWORD      dwBehavior; // Hardware / Software / Mixed vertex processing
    D3DFORMAT  DepthStencilFormat; // Which depth/stencil format to use with this mode

    void dump(DWORD i) {
        /* from D3DFORMAT:
           D3DFMT_X8R8G8B8             = 22,
           D3DFMT_R5G6B5               = 23,
           */

        jessu_printf(THREAD_GL, "------ %lu: %lux%lu (%lu %lu %lu)",
                i, Width, Height,
                (DWORD)Format, dwBehavior, (DWORD)DepthStencilFormat);
    }
};

//-----------------------------------------------------------------------------
// Name: struct D3DWindowedModeInfo
// Desc: Structure for holding information about a display mode
//-----------------------------------------------------------------------------
struct D3DWindowedModeInfo {
    D3DFORMAT  DisplayFormat;
    D3DFORMAT  BackBufferFormat;
    DWORD      dwBehavior; // Hardware / Software / Mixed vertex processing
    D3DFORMAT  DepthStencilFormat; // Which depth/stencil format to use with this mode
};

//-----------------------------------------------------------------------------
// Name: struct D3DDeviceInfo
// Desc: Structure for holding information about a Direct3D device, including
//       a list of modes compatible with this device
//-----------------------------------------------------------------------------
struct D3DDeviceInfo {
    // Device data
    D3DDEVTYPE   DeviceType;      // Reference, HAL, etc.
    D3DCAPS8     d3dCaps;         // Capabilities of this device
    const TCHAR* strDesc;         // Name of this device
    BOOL         bCanDoWindowed;  // Whether this device can work in windowed mode

    // Modes for this device
    DWORD        dwNumModes;
    D3DModeInfo  modes[150];

    // Current state
    DWORD        dwCurrentMode;
    BOOL         bWindowed;
    D3DMULTISAMPLE_TYPE MultiSampleType;

    void dump() {
        jessu_printf(THREAD_GL, "---- name: \"%s\"", strDesc);
        jessu_printf(THREAD_GL, "---- can do windowed: %d", bCanDoWindowed);
        jessu_printf(THREAD_GL, "---- currently windowed: %d", bWindowed);

        jessu_printf(THREAD_GL, "---- number of modes: %lu", dwNumModes);
        for (DWORD i = 0; i < dwNumModes; i++) {
            modes[i].dump(i);
        }
    }
};

//-----------------------------------------------------------------------------
// Name: struct D3DAdapterInfo
// Desc: Structure for holding information about an adapter, including a list
//       of devices available on this adapter
//-----------------------------------------------------------------------------
struct D3DAdapterInfo {
    // Adapter data
    DWORD          iMonitor; // Which MonitorInfo corresponds to this adapter
    D3DADAPTER_IDENTIFIER8 d3dAdapterIdentifier;
    D3DDISPLAYMODE d3ddmDesktop;      // Desktop display mode for this adapter

    // Devices for this adapter
    DWORD          dwNumDevices;
    D3DDeviceInfo  devices[3];
    BOOL           bHasHAL;
    BOOL           bHasAppCompatHAL;
    BOOL           bHasSW;
    BOOL           bHasAppCompatSW;

    // User's preferred mode settings for this adapter
    DWORD          dwUserPrefWidth;
    DWORD          dwUserPrefHeight;
    D3DFORMAT      d3dfmtUserPrefFormat;
    BOOL           bLeaveBlack;  // If TRUE, don't render to this display
    BOOL           bDisableHW;   // If TRUE, don't use HAL on this display

    // Current state
    DWORD          dwCurrentDevice;
    HWND           hWndDevice;

    void dump() {
        jessu_printf(THREAD_GL, "-- monitor %lu", iMonitor);
        jessu_printf(THREAD_GL, "-- has HAL: %d", bHasHAL);
        jessu_printf(THREAD_GL, "-- has app-compat HAL: %d", bHasAppCompatHAL);
        jessu_printf(THREAD_GL, "-- has SW: %d", bHasSW);
        jessu_printf(THREAD_GL, "-- has app-compat SW: %d", bHasAppCompatSW);
        jessu_printf(THREAD_GL, "-- current device %lu", dwCurrentDevice);

        jessu_printf(THREAD_GL, "-- number of devices: %lu", dwNumDevices);
        for (DWORD i = 0; i < dwNumDevices; i++) {
            devices[i].dump();
        }
    }
};

struct D3DAdaptersInfo {
    D3DAdapterInfo *m_Adapters[MAX_DISPLAYS];
    DWORD m_dwNumAdapters;

    D3DAdaptersInfo() {
        m_dwNumAdapters = 0;
        ZeroMemory(m_Adapters, sizeof(m_Adapters));
    }

    ~D3DAdaptersInfo() {
        for (DWORD i = 0; i < MAX_DISPLAYS; i++) {
            delete m_Adapters[i];
        }
    }

    D3DAdapterInfo *nextFreeAdapter() {
        if (m_dwNumAdapters == MAX_DISPLAYS) {
            return NULL;
        }

        if (m_Adapters[m_dwNumAdapters] == NULL) {
            m_Adapters[m_dwNumAdapters] = new D3DAdapterInfo;
            if (m_Adapters[m_dwNumAdapters] == NULL) {
                return NULL;
            }
        }

        ZeroMemory(m_Adapters[m_dwNumAdapters], sizeof(D3DAdapterInfo));
        m_Adapters[m_dwNumAdapters]->bDisableHW = g_bDisableHW;

        return m_Adapters[m_dwNumAdapters];
    }

    void acceptAdapter() {
        m_dwNumAdapters++;
    }

    void dump() {
        jessu_printf(THREAD_GL, "Number of adapters: %lu", m_dwNumAdapters);

        for (DWORD i = 0; i < m_dwNumAdapters; i++) {
            m_Adapters[i]->dump();
        }
    }

    BOOL compatibleDeviceFound() {
        BOOL bCompatibleDeviceFound = FALSE;
        for (DWORD iAdapter = 0; iAdapter < m_dwNumAdapters; iAdapter++) {
            if (m_Adapters[iAdapter]->bHasAppCompatHAL ||
                m_Adapters[iAdapter]->bHasAppCompatSW) {

                bCompatibleDeviceFound = TRUE;
                break;
            }
        }

        return bCompatibleDeviceFound;
    }
};

//-----------------------------------------------------------------------------
// Name: struct RenderUnit
// Desc: there's one of these for every screen we draw
//-----------------------------------------------------------------------------
struct RenderUnit {
    UINT                  iAdapter;
    UINT                  iMonitor;
    D3DDEVTYPE            DeviceType;      // Reference, HAL, etc.
    DWORD                 dwBehavior;
    IDirect3DDevice8 *    pd3dDevice;
    D3DPRESENT_PARAMETERS d3dpp;
    BOOL                  bDeviceObjectsInited; // InitDeviceObjects was called
    BOOL                  bDeviceObjectsRestored; // RestoreDeviceObjects was called
    TCHAR                 strDeviceStats[90];// String to hold D3D device stats
    TCHAR                 strFrameStats[40]; // String to hold frame stats

    RenderUnit() {
        strDeviceStats[0] = '\0';
        strFrameStats[0] = '\0';
        ZeroMemory(&d3dpp, sizeof(d3dpp));
    }

    void dump() {
        jessu_printf(THREAD_GL, "---- adaptor: %u", iAdapter);
        jessu_printf(THREAD_GL, "---- monitor: %u", iMonitor);
        jessu_printf(THREAD_GL, "---- device stats: %s", strDeviceStats);
        jessu_printf(THREAD_GL, "---- frame stats: %s", strFrameStats);
    }
};

struct RenderUnits {
    RenderUnit      m_RenderUnits[MAX_DISPLAYS];
    DWORD           m_dwNumRenderUnits;

    RenderUnits() {
        m_dwNumRenderUnits = 0;
    }

    RenderUnit *nextFreeRenderUnit() {
        if (m_dwNumRenderUnits == MAX_DISPLAYS) {
            return NULL;
        }

        return &m_RenderUnits[m_dwNumRenderUnits++];
    }

    void trashLastRenderUnit() {
        if (m_dwNumRenderUnits > 0) {
            m_dwNumRenderUnits--;
        }
    }

    void dump() {
        jessu_printf(THREAD_GL, "-- number of render units: %lu",
                m_dwNumRenderUnits);
        for (DWORD i = 0; i < m_dwNumRenderUnits; i++) {
            m_RenderUnits[i].dump();
        }
    }
};

//-----------------------------------------------------------------------------
// Name: EnumMonitors()
// Desc: Determine HMONITOR, desktop rect, and other info for each monitor.  
//       Note that EnumDisplayDevices enumerates monitors in the order 
//       indicated on the Settings page of the Display control panel, which 
//       is the order we want to list monitors in, as opposed to the order 
//       used by D3D's GetAdapterInfo.
//-----------------------------------------------------------------------------
static void EnumMonitors(MonitorsInfo *monitors)
{

    // Use the following structure rather than DISPLAY_DEVICE, since some old 
    // versions of DISPLAY_DEVICE are missing the last two fields and this can
    // cause problems with EnumDisplayDevices on Windows 2000.
    struct DISPLAY_DEVICE_FULL {
        DWORD  cb;
        TCHAR  DeviceName[32];
        TCHAR  DeviceString[128];
        DWORD  StateFlags;
        TCHAR  DeviceID[128];
        TCHAR  DeviceKey[128];
    };

    DWORD iDevice = 0;
    DISPLAY_DEVICE_FULL dispdev;
    DISPLAY_DEVICE_FULL dispdev2;
    DEVMODE devmode;
    dispdev.cb = sizeof(dispdev);
    dispdev2.cb = sizeof(dispdev2);
    devmode.dmSize = sizeof(devmode);
    devmode.dmDriverExtra = 0;
    MonitorInfo monitorInfoNew;

    while (EnumDisplayDevices(NULL, iDevice, (DISPLAY_DEVICE*)&dispdev, 0)) {
        // Ignore NetMeeting's mirrored displays
        if ((dispdev.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) == 0) {
            // To get monitor info for a display device, call
            // EnumDisplayDevices a second time, passing dispdev.DeviceName
            // (from the first call) as the first parameter.
            EnumDisplayDevices(dispdev.DeviceName,
                    0, (DISPLAY_DEVICE*)&dispdev2, 0);

            ZeroMemory(&monitorInfoNew, sizeof(MonitorInfo));
            lstrcpy(monitorInfoNew.strDeviceName, dispdev.DeviceString);
            lstrcpy(monitorInfoNew.strMonitorName, dispdev2.DeviceString);
            monitorInfoNew.iAdapter = NO_ADAPTER;
            
            if (dispdev.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) {
                EnumDisplaySettings(dispdev.DeviceName,
                        ENUM_CURRENT_SETTINGS, &devmode);
                if (dispdev.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) {
                    // For some reason devmode.dmPosition is not always (0, 0)
                    // for the primary display, so force it.
                    monitorInfoNew.rcScreen.left = 0;
                    monitorInfoNew.rcScreen.top = 0;
                } else {
                    monitorInfoNew.rcScreen.left = devmode.dmPosition.x;
                    monitorInfoNew.rcScreen.top = devmode.dmPosition.y;
                }

                monitorInfoNew.rcScreen.right =
                    monitorInfoNew.rcScreen.left + devmode.dmPelsWidth;
                monitorInfoNew.rcScreen.bottom =
                    monitorInfoNew.rcScreen.top + devmode.dmPelsHeight;
                monitorInfoNew.hMonitor =
                    MonitorFromRect(&monitorInfoNew.rcScreen,
                            MONITOR_DEFAULTTONULL);

                if (!monitors->addMonitor(&monitorInfoNew)) {
                    break;
                }
            }
        }

        iDevice++;
    }
}

static HRESULT ConfirmDevice(D3DCAPS8* /*pCaps*/, DWORD /*dwBehavior*/,
        D3DFORMAT /*fmtBackBuffer*/)
{
    // can filter out devices here
    return S_OK;
}

static HRESULT ConfirmMode(LPDIRECT3DDEVICE8 /*pd3dDev*/)
{
    // can filter out modes here
    return S_OK;
}

HRESULT InitDeviceObjects()
{
    // not sure what to do here
    return S_OK;
}

HRESULT RestoreDeviceObjects()
{
    // not sure what to do here
    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: FindDepthStencilFormat()
// Desc: Finds a depth/stencil format for the given device that is compatible
//       with the render target format and meets the needs of the app.
//-----------------------------------------------------------------------------
static BOOL FindDepthStencilFormat(UINT iAdapter, D3DDEVTYPE DeviceType,
    D3DFORMAT TargetFormat, D3DFORMAT* pDepthStencilFormat)
{
    if (g_dwMinDepthBits <= 16 && g_dwMinStencilBits == 0) {
        if (SUCCEEDED(g_pD3D->CheckDeviceFormat(iAdapter, DeviceType,
                        TargetFormat, D3DUSAGE_DEPTHSTENCIL,
                        D3DRTYPE_SURFACE, D3DFMT_D16))) {

            if (SUCCEEDED(g_pD3D->CheckDepthStencilMatch(iAdapter, DeviceType,
                            TargetFormat, TargetFormat, D3DFMT_D16))) {

                *pDepthStencilFormat = D3DFMT_D16;
                return TRUE;
            }
        }
    }

    if (g_dwMinDepthBits <= 15 && g_dwMinStencilBits <= 1) {
        if (SUCCEEDED(g_pD3D->CheckDeviceFormat(iAdapter, DeviceType,
                        TargetFormat, D3DUSAGE_DEPTHSTENCIL,
                        D3DRTYPE_SURFACE, D3DFMT_D15S1))) {

            if (SUCCEEDED(g_pD3D->CheckDepthStencilMatch(iAdapter, DeviceType,
                            TargetFormat, TargetFormat, D3DFMT_D15S1))) {

                *pDepthStencilFormat = D3DFMT_D15S1;
                return TRUE;
            }
        }
    }

    if (g_dwMinDepthBits <= 24 && g_dwMinStencilBits == 0) {
        if (SUCCEEDED(g_pD3D->CheckDeviceFormat(iAdapter, DeviceType,
                        TargetFormat, D3DUSAGE_DEPTHSTENCIL,
                        D3DRTYPE_SURFACE, D3DFMT_D24X8))) {

            if (SUCCEEDED(g_pD3D->CheckDepthStencilMatch(iAdapter, DeviceType,
                            TargetFormat, TargetFormat, D3DFMT_D24X8))) {

                *pDepthStencilFormat = D3DFMT_D24X8;
                return TRUE;
            }
        }
    }

    if (g_dwMinDepthBits <= 24 && g_dwMinStencilBits <= 8) {
        if (SUCCEEDED(g_pD3D->CheckDeviceFormat(iAdapter, DeviceType,
                        TargetFormat, D3DUSAGE_DEPTHSTENCIL,
                        D3DRTYPE_SURFACE, D3DFMT_D24S8))) {

            if (SUCCEEDED(g_pD3D->CheckDepthStencilMatch(iAdapter, DeviceType,
                            TargetFormat, TargetFormat, D3DFMT_D24S8))) {

                *pDepthStencilFormat = D3DFMT_D24S8;
                return TRUE;
            }
        }
    }

    if (g_dwMinDepthBits <= 24 && g_dwMinStencilBits <= 4) {
        if (SUCCEEDED(g_pD3D->CheckDeviceFormat(iAdapter, DeviceType,
                        TargetFormat, D3DUSAGE_DEPTHSTENCIL,
                        D3DRTYPE_SURFACE, D3DFMT_D24X4S4))) {

            if (SUCCEEDED(g_pD3D->CheckDepthStencilMatch(iAdapter, DeviceType,
                            TargetFormat, TargetFormat, D3DFMT_D24X4S4))) {

                *pDepthStencilFormat = D3DFMT_D24X4S4;
                return TRUE;
            }
        }
    }

    if (g_dwMinDepthBits <= 32 && g_dwMinStencilBits == 0) {
        if (SUCCEEDED(g_pD3D->CheckDeviceFormat(iAdapter, DeviceType,
                        TargetFormat, D3DUSAGE_DEPTHSTENCIL,
                        D3DRTYPE_SURFACE, D3DFMT_D32))) {

            if (SUCCEEDED(g_pD3D->CheckDepthStencilMatch(iAdapter, DeviceType,
                            TargetFormat, TargetFormat, D3DFMT_D32))) {

                *pDepthStencilFormat = D3DFMT_D32;
                return TRUE;
            }
        }
    }

    return FALSE;
}

//-----------------------------------------------------------------------------
// Name: SortModesCallback()
// Desc: Callback function for sorting display modes (used by BuildDeviceList).
//-----------------------------------------------------------------------------
static int SortModesCallback(const VOID* arg1, const VOID* arg2)
{
    D3DDISPLAYMODE* p1 = (D3DDISPLAYMODE*)arg1;
    D3DDISPLAYMODE* p2 = (D3DDISPLAYMODE*)arg2;

    if (p1->Width  < p2->Width)     return -1;
    if (p1->Width  > p2->Width)     return +1;
    if (p1->Height < p2->Height)    return -1;
    if (p1->Height > p2->Height)    return +1;
    if (p1->Format > p2->Format)    return -1;
    if (p1->Format < p2->Format)    return +1;

    return 0;
}

//-----------------------------------------------------------------------------
// Name: BuildDeviceList()
// Desc: Builds a list of all available adapters, devices, and modes.
//-----------------------------------------------------------------------------
static HRESULT BuildDeviceList(D3DAdaptersInfo *adapters,
        MonitorsInfo *monitors)
{
    static const TCHAR *strDeviceDescs[] = {
        TEXT("HAL"), TEXT("SW"), TEXT("REF")
    };
    static const D3DDEVTYPE DeviceTypes[] = {
        D3DDEVTYPE_HAL, D3DDEVTYPE_SW, D3DDEVTYPE_REF
    };
    DWORD dwNumDeviceTypes = g_bAllowRef ? 3 : 2;
    HMONITOR hMonitor = NULL;
    BOOL bHALExists = FALSE;
    BOOL bHALIsWindowedCompatible = FALSE;
    BOOL bHALIsDesktopCompatible = FALSE;
    BOOL bHALIsSampleCompatible = FALSE;

    // Loop through all the adapters on the system (usually, there's just one
    // unless more than one graphics card is present).
    for (UINT iAdapter = 0; iAdapter < g_pD3D->GetAdapterCount(); iAdapter++) {
        // Fill in adapter info
        D3DAdapterInfo* pAdapter = adapters->nextFreeAdapter();
        if (pAdapter == NULL) {
            // might also have run out of adapters in array
            return E_OUTOFMEMORY;
        }

        g_pD3D->GetAdapterIdentifier(iAdapter, D3DENUM_NO_WHQL_LEVEL,
                &pAdapter->d3dAdapterIdentifier);
        g_pD3D->GetAdapterDisplayMode(iAdapter, &pAdapter->d3ddmDesktop);
        pAdapter->dwNumDevices    = 0;
        pAdapter->dwCurrentDevice = 0;
        pAdapter->bLeaveBlack = FALSE;
        pAdapter->iMonitor = NO_MONITOR;

        // Find the MonitorInfo that corresponds to this adapter.  If the
        // monitor is disabled, the adapter has a NULL HMONITOR and we cannot
        // find the corresponding MonitorInfo.  (Well, if one monitor was
        // disabled, we could link the one MonitorInfo with a NULL HMONITOR to
        // the one D3DAdapterInfo with a NULL HMONITOR, but if there are more
        // than one, we can't link them, so it's safer not to ever try.)
        hMonitor = g_pD3D->GetAdapterMonitor(iAdapter);
        if (hMonitor != NULL) {
            for (DWORD iMonitor = 0;
                    iMonitor < monitors->m_dwNumMonitors; iMonitor++) {

                MonitorInfo* pMonitorInfo = &monitors->m_Monitors[iMonitor];

                if (pMonitorInfo->hMonitor == hMonitor) {
                    pAdapter->iMonitor = iMonitor;
                    pMonitorInfo->iAdapter = iAdapter;
                    break;
                }
            }
        }

        // Enumerate all display modes on this adapter
        D3DDISPLAYMODE modes[100];
        D3DFORMAT formats[20];
        DWORD dwNumFormats      = 0;
        DWORD dwNumModes        = 0;
        DWORD dwNumAdapterModes = g_pD3D->GetAdapterModeCount(iAdapter);

        // Add the adapter's current desktop format to the list of formats
        formats[dwNumFormats++] = pAdapter->d3ddmDesktop.Format;

        for (UINT iMode = 0; iMode < dwNumAdapterModes; iMode++) {
            // Get the display mode attributes
            D3DDISPLAYMODE DisplayMode;
            g_pD3D->EnumAdapterModes(iAdapter, iMode, &DisplayMode);

            // Filter out low-resolution modes
            if (DisplayMode.Width  < 640 || DisplayMode.Height < 400) {
                continue;
            }

            // Check if the mode already exists (to filter out refresh rates)
            for (DWORD m = 0L; m < dwNumModes; m++) {
                if ((modes[m].Width  == DisplayMode.Width) &&
                    (modes[m].Height == DisplayMode.Height) &&
                    (modes[m].Format == DisplayMode.Format)) {

                    break;
                }
            }

            // If we found a new mode, add it to the list of modes
            if (m == dwNumModes) {
                modes[dwNumModes].Width       = DisplayMode.Width;
                modes[dwNumModes].Height      = DisplayMode.Height;
                modes[dwNumModes].Format      = DisplayMode.Format;
                modes[dwNumModes].RefreshRate = 0;
                dwNumModes++;

                // Check if the mode's format already exists
                for (DWORD f=0; f<dwNumFormats; f++) {
                    if (DisplayMode.Format == formats[f]) {
                        break;
                    }
                }

                // If the format is new, add it to the list
                if (f== dwNumFormats) {
                    formats[dwNumFormats++] = DisplayMode.Format;
                }
            }
        }

        // Sort the list of display modes (by format, then width, then height)
        qsort(modes, dwNumModes, sizeof(D3DDISPLAYMODE), SortModesCallback);

        // Add devices to adapter
        for (UINT iDevice = 0; iDevice < dwNumDeviceTypes; iDevice++) {
            // Fill in device info
            D3DDeviceInfo* pDevice = &pAdapter->devices[pAdapter->dwNumDevices];
            pDevice->DeviceType     = DeviceTypes[iDevice];
            g_pD3D->GetDeviceCaps(iAdapter,
                    DeviceTypes[iDevice], &pDevice->d3dCaps);
            pDevice->strDesc        = strDeviceDescs[iDevice];
            pDevice->dwNumModes     = 0;
            pDevice->dwCurrentMode  = 0;
            pDevice->bCanDoWindowed = FALSE;
            pDevice->bWindowed      = FALSE;
            pDevice->MultiSampleType = D3DMULTISAMPLE_NONE;

            // Examine each format supported by the adapter to see if it will
            // work with this device and meets the needs of the application.
            BOOL bFormatConfirmed[20];
            DWORD dwBehavior[20];
            D3DFORMAT fmtDepthStencil[20];

            for (DWORD f = 0; f < dwNumFormats; f++) {
                bFormatConfirmed[f] = FALSE;
                fmtDepthStencil[f] = D3DFMT_UNKNOWN;

                // Skip formats that cannot be used as render targets on this
                // device
                if (FAILED(g_pD3D->CheckDeviceType(iAdapter,
                                pDevice->DeviceType, formats[f],
                                formats[f], FALSE))) {
                    continue;
                }

                if (pDevice->DeviceType == D3DDEVTYPE_SW) {
                    // This system has a SW device
                    pAdapter->bHasSW = TRUE;
                }

                if (pDevice->DeviceType == D3DDEVTYPE_HAL) {
                    // This system has a HAL device
                    bHALExists = TRUE;
                    pAdapter->bHasHAL = TRUE;

                    if (pDevice->d3dCaps.Caps2 & D3DCAPS2_CANRENDERWINDOWED) {
                        // HAL can run in a window for some mode
                        bHALIsWindowedCompatible = TRUE;

                        if (f == 0) {
                            // HAL can run in a window for the current desktop
                            // mode
                            bHALIsDesktopCompatible = TRUE;
                        }
                    }
                }

                // Confirm the device/format for HW vertex processing
                if (pDevice->d3dCaps.DevCaps&D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
                    if (pDevice->d3dCaps.DevCaps&D3DDEVCAPS_PUREDEVICE) {
                        dwBehavior[f] = D3DCREATE_HARDWARE_VERTEXPROCESSING |
                                        D3DCREATE_PUREDEVICE;

                        if (SUCCEEDED(ConfirmDevice(&pDevice->d3dCaps,
                                        dwBehavior[f], formats[f]))) {

                            bFormatConfirmed[f] = TRUE;
                        }
                    }

                    if (!bFormatConfirmed[f]) {
                        dwBehavior[f] = D3DCREATE_HARDWARE_VERTEXPROCESSING;

                        if (SUCCEEDED(ConfirmDevice(&pDevice->d3dCaps,
                                        dwBehavior[f], formats[f]))) {

                            bFormatConfirmed[f] = TRUE;
                        }
                    }

                    if (!bFormatConfirmed[f]) {
                        dwBehavior[f] = D3DCREATE_MIXED_VERTEXPROCESSING;

                        if (SUCCEEDED(ConfirmDevice(&pDevice->d3dCaps,
                                        dwBehavior[f], formats[f]))) {

                            bFormatConfirmed[f] = TRUE;
                        }
                    }
                }

                // Confirm the device/format for SW vertex processing
                if (!bFormatConfirmed[f]) {
                    dwBehavior[f] = D3DCREATE_SOFTWARE_VERTEXPROCESSING;

                    if (SUCCEEDED(ConfirmDevice(&pDevice->d3dCaps,
                                    dwBehavior[f], formats[f]))) {

                        bFormatConfirmed[f] = TRUE;
                    }
                }

                if (bFormatConfirmed[f] && g_bMultithreaded) {
                    dwBehavior[f] |= D3DCREATE_MULTITHREADED;
                }

                // Find a suitable depth/stencil buffer format for this
                // device/format
                if (bFormatConfirmed[f] && g_bUseDepthBuffer) {
                    if (!FindDepthStencilFormat(iAdapter, pDevice->DeviceType,
                                formats[f], &fmtDepthStencil[f])) {

                        bFormatConfirmed[f] = FALSE;
                    }
                }
            }

            // Add all enumerated display modes with confirmed formats to the
            // device's list of valid modes
            for (DWORD m = 0L; m < dwNumModes; m++) {
                for (DWORD f = 0; f < dwNumFormats; f++) {
                    if (modes[m].Format == formats[f]) {
                        if (bFormatConfirmed[f]) {
                            // Add this mode to the device's list of valid
                            // modes
                            DWORD i = pDevice->dwNumModes++;

                            pDevice->modes[i].Width = modes[m].Width;
                            pDevice->modes[i].Height = modes[m].Height;
                            pDevice->modes[i].Format = modes[m].Format;
                            pDevice->modes[i].dwBehavior = dwBehavior[f];
                            pDevice->modes[i].DepthStencilFormat =
                                fmtDepthStencil[f];

                            if (pDevice->DeviceType == D3DDEVTYPE_HAL) {
                                bHALIsSampleCompatible = TRUE;
                            }
                        }
                    }
                }
            }

            // Select any 640x480 mode for default (but prefer a 16-bit mode)
            for (m = 0; m < pDevice->dwNumModes; m++) {
                if (pDevice->modes[m].Width == 640 &&
                        pDevice->modes[m].Height == 480) {

                    pDevice->dwCurrentMode = m;
                    if (pDevice->modes[m].Format == D3DFMT_R5G6B5 ||
                        pDevice->modes[m].Format == D3DFMT_X1R5G5B5 ||
                        pDevice->modes[m].Format == D3DFMT_A1R5G5B5) {

                        break;
                    }
                }
            }

            // Check if the device is compatible with the desktop display mode
            // (which was added initially as formats[0])
            if (bFormatConfirmed[0] &&
                    (pDevice->d3dCaps.Caps2 & D3DCAPS2_CANRENDERWINDOWED)) {

                pDevice->bCanDoWindowed = TRUE;
                pDevice->bWindowed      = TRUE;
            }

            // If valid modes were found, keep this device
            if (pDevice->dwNumModes > 0) {
                pAdapter->dwNumDevices++;
                if (pDevice->DeviceType == D3DDEVTYPE_SW) {
                    pAdapter->bHasAppCompatSW = TRUE;
                } else if (pDevice->DeviceType == D3DDEVTYPE_HAL) {
                    pAdapter->bHasAppCompatHAL = TRUE;
                }
            }
        }

        // If valid devices were found, keep this adapter
// Count adapters even if no devices, so we can throw up blank windows on them
//        if (pAdapter->dwNumDevices > 0)
        adapters->acceptAdapter();
    }

#if 0
    // Return an error if no compatible devices were found
    if(0L == m_dwNumAdapters)
        return D3DAPPERR_NOCOMPATIBLEDEVICES;

    // Pick a default device that can render into a window
    // (This code assumes that the HAL device comes before the REF
    // device in the device array).
    for(DWORD a=0; a<m_dwNumAdapters; a++)
    {
        for(DWORD d=0; d < adapters->m_Adapters[a]->dwNumDevices; d++)
        {
            if(adapters->m_Adapters[a]->devices[d].bWindowed)
            {
                adapters->m_Adapters[a]->dwCurrentDevice = d;
                m_dwAdapter = a;
                m_bWindowed = TRUE;

                // Display a warning message
                if(adapters->m_Adapters[a]->devices[d].DeviceType == D3DDEVTYPE_REF)
                {
                    if(!bHALExists)
                        DisplayErrorMsg(D3DAPPERR_NOHARDWAREDEVICE, MSGWARN_SWITCHEDTOREF);
                    else if(!bHALIsSampleCompatible)
                        DisplayErrorMsg(D3DAPPERR_HALNOTCOMPATIBLE, MSGWARN_SWITCHEDTOREF);
                    else if(!bHALIsWindowedCompatible)
                        DisplayErrorMsg(D3DAPPERR_NOWINDOWEDHAL, MSGWARN_SWITCHEDTOREF);
                    else if(!bHALIsDesktopCompatible)
                        DisplayErrorMsg(D3DAPPERR_NODESKTOPHAL, MSGWARN_SWITCHEDTOREF);
                    else // HAL is desktop compatible, but not sample compatible
                        DisplayErrorMsg(D3DAPPERR_NOHALTHISMODE, MSGWARN_SWITCHEDTOREF);
                }

                return S_OK;
            }
        }
    }
    return D3DAPPERR_NOWINDOWABLEDEVICES;
#endif

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: GetBestAdapter()
// Desc: To decide which adapter to use, loop through monitors until you find
//       one whose adapter has a compatible HAL.  If none, use the first 
//       monitor that has an compatible SW device.
//-----------------------------------------------------------------------------
static DWORD GetBestAdapter(D3DAdaptersInfo *adapters, MonitorsInfo *monitors)
{
    DWORD iAdapterBest = NO_ADAPTER;
    DWORD iMonitor;

    for (iMonitor = 0; iMonitor < monitors->m_dwNumMonitors; iMonitor++) {
        MonitorInfo *pMonitorInfo = &monitors->m_Monitors[iMonitor];

        DWORD iAdapter = pMonitorInfo->iAdapter;
        if (iAdapter != NO_ADAPTER) {
            D3DAdapterInfo *pD3DAdapterInfo = adapters->m_Adapters[iAdapter];
            if (pD3DAdapterInfo->bHasAppCompatHAL) {
                iAdapterBest = iAdapter;
                break;
            }

            if (pD3DAdapterInfo->bHasAppCompatSW) {
                iAdapterBest = iAdapter;
                // but keep looking...
            }
        }
    }

    return iAdapterBest;
}

//-----------------------------------------------------------------------------
// Name: FindNextLowerMode()
// Desc: 
//-----------------------------------------------------------------------------
static BOOL FindNextLowerMode(D3DDeviceInfo* pD3DDeviceInfo)
{
    DWORD iModeCur = pD3DDeviceInfo->dwCurrentMode;
    D3DModeInfo *pD3DModeInfoCur = &pD3DDeviceInfo->modes[iModeCur];
    DWORD dwWidthCur = pD3DModeInfoCur->Width;
    DWORD dwHeightCur = pD3DModeInfoCur->Height;
    DWORD dwNumPixelsCur = dwWidthCur * dwHeightCur;
    D3DFORMAT d3dfmtCur = pD3DModeInfoCur->Format;
    BOOL b32BitCur = (d3dfmtCur == D3DFMT_A8R8G8B8 ||
                      d3dfmtCur == D3DFMT_X8R8G8B8);
    DWORD iModeNew;
    D3DModeInfo *pD3DModeInfoNew;
    DWORD dwWidthNew;
    DWORD dwHeightNew;
    DWORD dwNumPixelsNew;
    D3DFORMAT d3dfmtNew = D3DFMT_UNKNOWN;
    BOOL b32BitNew;

    DWORD dwWidthBest = 0;
    DWORD dwHeightBest = 0;
    DWORD dwNumPixelsBest = 0;
    BOOL b32BitBest = FALSE;
    DWORD iModeBest = 0xffff;

    for (iModeNew = 0; iModeNew < pD3DDeviceInfo->dwNumModes; iModeNew++) {
        // Don't pick the same mode we currently have
        if (iModeNew == iModeCur) {
            continue;
        }

        // Get info about new mode
        pD3DModeInfoNew = &pD3DDeviceInfo->modes[iModeNew];
        dwWidthNew = pD3DModeInfoNew->Width;
        dwHeightNew = pD3DModeInfoNew->Height;
        dwNumPixelsNew = dwWidthNew * dwHeightNew;
        d3dfmtNew = pD3DModeInfoNew->Format;
        b32BitNew = (d3dfmtNew == D3DFMT_A8R8G8B8 ||
                     d3dfmtNew == D3DFMT_X8R8G8B8);

        // If we're currently 32-bit and new mode is same width/height and
        // 16-bit, take it
        if (b32BitCur && !b32BitNew &&
            pD3DModeInfoNew->Width == dwWidthCur &&
            pD3DModeInfoNew->Height == dwHeightCur) {

            pD3DDeviceInfo->dwCurrentMode = iModeNew;
            return TRUE;
        }

        // If new mode is smaller than current mode, see if it's our best so far
        if (dwNumPixelsNew < dwNumPixelsCur) {
            // If current best is 32-bit, new mode needs to be bigger to be best
            if (b32BitBest && (dwNumPixelsNew < dwNumPixelsBest)) {
                continue;
            }

            // If new mode is bigger or equal to best, make it the best
            if ((dwNumPixelsNew > dwNumPixelsBest) || 
                (!b32BitBest && b32BitNew)) {

                dwWidthBest = dwWidthNew;
                dwHeightBest = dwHeightNew;
                dwNumPixelsBest = dwNumPixelsNew;
                iModeBest = iModeNew;
                b32BitBest = b32BitNew;
            }
        }
    }

    if (iModeBest == 0xffff) {
        // no smaller mode found
        return FALSE;
    }

    pD3DDeviceInfo->dwCurrentMode = iModeBest;

    return TRUE;
}

//-----------------------------------------------------------------------------
// Name: CreateFullscreenRenderUnit()
// Desc: 
//-----------------------------------------------------------------------------
static HRESULT CreateFullscreenRenderUnit(HWND hWnd, RenderUnit *pRenderUnit,
        D3DAdaptersInfo *adapters)
{
    HRESULT hr;
    UINT iAdapter = pRenderUnit->iAdapter;
    D3DAdapterInfo *pD3DAdapterInfo = adapters->m_Adapters[iAdapter];
    DWORD iMonitor = pD3DAdapterInfo->iMonitor;
    D3DDeviceInfo *pD3DDeviceInfo;
    D3DModeInfo *pD3DModeInfo;
    DWORD dwCurrentDevice;
    D3DDEVTYPE curType;

    if (iAdapter >= adapters->m_dwNumAdapters) {
        return E_FAIL;
    }

    if (pD3DAdapterInfo->dwNumDevices == 0) {
        return E_FAIL;
    }

    // Find the best device for the adapter.  Use HAL
    // if it's there, otherwise SW, otherwise REF.
    dwCurrentDevice = 0xffff;
    curType = D3DDEVTYPE_FORCE_DWORD;
    for (DWORD iDevice = 0; iDevice < pD3DAdapterInfo->dwNumDevices;
            iDevice++) {

        pD3DDeviceInfo = &pD3DAdapterInfo->devices[iDevice];

        if (pD3DDeviceInfo->DeviceType == D3DDEVTYPE_HAL &&
                !pD3DAdapterInfo->bDisableHW) {

            dwCurrentDevice = iDevice;
            curType = D3DDEVTYPE_HAL;
            break; // stop looking
        } else if (pD3DDeviceInfo->DeviceType == D3DDEVTYPE_SW) {
            dwCurrentDevice = iDevice;
            curType = D3DDEVTYPE_SW;
            // but keep looking
        } else if (pD3DDeviceInfo->DeviceType == D3DDEVTYPE_REF &&
                g_bAllowRef && curType != D3DDEVTYPE_SW) {

            dwCurrentDevice = iDevice;
            curType = D3DDEVTYPE_REF;
            // but keep looking
        }
    }

    if (dwCurrentDevice == 0xffff) {
        return D3DAPPERR_NOHARDWAREDEVICE;
    }

    pD3DDeviceInfo = &pD3DAdapterInfo->devices[dwCurrentDevice];

    pD3DDeviceInfo->dwCurrentMode = 0xffff;
    if (pD3DAdapterInfo->dwUserPrefWidth != 0) {
        // Try to find mode that matches user preference
        for (DWORD iMode = 0; iMode < pD3DDeviceInfo->dwNumModes; iMode++) {
            pD3DModeInfo = &pD3DDeviceInfo->modes[iMode];

            if (pD3DModeInfo->Width == pD3DAdapterInfo->dwUserPrefWidth &&
                pD3DModeInfo->Height == pD3DAdapterInfo->dwUserPrefHeight &&
                pD3DModeInfo->Format == pD3DAdapterInfo->d3dfmtUserPrefFormat) {

                pD3DDeviceInfo->dwCurrentMode = iMode;
                break;
            }
        }
    }

    // If user-preferred mode is not specified or not found,
    // use "Automatic" technique: 
    if (pD3DDeviceInfo->dwCurrentMode == 0xffff) {
        if (pD3DDeviceInfo->DeviceType == D3DDEVTYPE_SW ||
            pD3DDeviceInfo->DeviceType == D3DDEVTYPE_REF) {

            // If using a SW or REF rast then try to find a low resolution and
            // 16-bpp.
            BOOL bFound16BitMode = FALSE;            
            DWORD dwSmallestHeight = 0xffff;
            pD3DDeviceInfo->dwCurrentMode = 0;// unless we find something better

            for (DWORD iMode = 0; iMode < pD3DDeviceInfo->dwNumModes; iMode++) {
                pD3DModeInfo = &pD3DDeviceInfo->modes[iMode];

                // Skip 640x400 because 640x480 is better
                if (pD3DModeInfo->Height == 400) {
                    continue; 
                }

                if (pD3DModeInfo->Height < dwSmallestHeight || 
                    (pD3DModeInfo->Height == dwSmallestHeight &&
                     !bFound16BitMode)) {

                    dwSmallestHeight = pD3DModeInfo->Height;
                    pD3DDeviceInfo->dwCurrentMode = iMode;
                    bFound16BitMode = FALSE;

                    if (pD3DModeInfo->Format == D3DFMT_R5G6B5 ||
                        pD3DModeInfo->Format == D3DFMT_X1R5G5B5 || 
                        pD3DModeInfo->Format == D3DFMT_A1R5G5B5 || 
                        pD3DModeInfo->Format == D3DFMT_A4R4G4B4 || 
                        pD3DModeInfo->Format == D3DFMT_X4R4G4B4) {

                        bFound16BitMode = TRUE;
                    }
                }
            }

            pD3DModeInfo =
                &pD3DDeviceInfo->modes[pD3DDeviceInfo->dwCurrentMode];
            jessu_printf(THREAD_GL, "Software rendering, dropping to %dx%d%s",
                    pD3DModeInfo->Width, pD3DModeInfo->Height,
                    bFound16BitMode ? " (16-bit)" : "");

        } else {
            // Try to find mode matching desktop resolution and 32-bpp.
            BOOL bMatchedSize = FALSE;
            BOOL bGot32Bit = FALSE;
            pD3DDeviceInfo->dwCurrentMode = 0;// unless we find something better
            for (DWORD iMode = 0; iMode < pD3DDeviceInfo->dwNumModes; iMode++) {
                pD3DModeInfo = &pD3DDeviceInfo->modes[iMode];
                if (pD3DModeInfo->Width ==
                        pD3DAdapterInfo->d3ddmDesktop.Width &&
                    pD3DModeInfo->Height ==
                        pD3DAdapterInfo->d3ddmDesktop.Height) {

                    if (!bMatchedSize) {
                        pD3DDeviceInfo->dwCurrentMode = iMode;
                    }

                    bMatchedSize = TRUE;
                    if (!bGot32Bit &&
                        (pD3DModeInfo->Format == D3DFMT_X8R8G8B8 ||
                         pD3DModeInfo->Format == D3DFMT_A8R8G8B8)) {

                        pD3DDeviceInfo->dwCurrentMode = iMode;
                        bGot32Bit = TRUE;
                        break;
                    }
                }
            }
        }

        pD3DModeInfo =
            &pD3DDeviceInfo->modes[pD3DDeviceInfo->dwCurrentMode];
        jessu_printf(THREAD_GL, "Hardware rendering, using %dx%d",
                pD3DModeInfo->Width, pD3DModeInfo->Height);
    }

    // If desktop mode not found, pick highest mode available
    if (pD3DDeviceInfo->dwCurrentMode == 0xffff) {
        DWORD dwWidthMax = 0;
        DWORD dwHeightMax = 0;
        DWORD dwBppMax = 0;
        DWORD dwWidthCur = 0;
        DWORD dwHeightCur = 0;
        DWORD dwBppCur = 0;

        for (DWORD iMode = 0; iMode < pD3DDeviceInfo->dwNumModes; iMode++) {
            pD3DModeInfo = &pD3DDeviceInfo->modes[iMode];
            dwWidthCur = pD3DModeInfo->Width;
            dwHeightCur = pD3DModeInfo->Height;

            if (pD3DModeInfo->Format == D3DFMT_X8R8G8B8 ||
                pD3DModeInfo->Format == D3DFMT_A8R8G8B8) {

                dwBppCur = 32;
            } else {
                dwBppCur = 16;
            }

            if (dwWidthCur > dwWidthMax ||
                    dwHeightCur > dwHeightMax ||
                    (dwWidthCur == dwWidthMax &&
                     dwHeightCur == dwHeightMax &&
                     dwBppCur > dwBppMax)) {

                dwWidthMax = dwWidthCur;
                dwHeightMax = dwHeightCur;
                dwBppMax = dwBppCur;
                pD3DDeviceInfo->dwCurrentMode = iMode;
            }
        }
    }

    // Try to create the D3D device, falling back to lower-res modes if it fails
    BOOL bAtLeastOneFailure = FALSE;
    for (;;) {
        pD3DModeInfo = &pD3DDeviceInfo->modes[pD3DDeviceInfo->dwCurrentMode];
        pRenderUnit->DeviceType = pD3DDeviceInfo->DeviceType;
        pRenderUnit->dwBehavior = pD3DModeInfo->dwBehavior;
        pRenderUnit->iMonitor = iMonitor;
        pRenderUnit->d3dpp.BackBufferFormat = pD3DModeInfo->Format;
        pRenderUnit->d3dpp.BackBufferWidth = pD3DModeInfo->Width;
        pRenderUnit->d3dpp.BackBufferHeight = pD3DModeInfo->Height;
        pRenderUnit->d3dpp.Windowed = FALSE;
        pRenderUnit->d3dpp.FullScreen_RefreshRateInHz =
            D3DPRESENT_RATE_DEFAULT;
        pRenderUnit->d3dpp.FullScreen_PresentationInterval =
            D3DPRESENT_INTERVAL_ONE;
        pRenderUnit->d3dpp.AutoDepthStencilFormat =
            pD3DModeInfo->DepthStencilFormat;
        pRenderUnit->d3dpp.BackBufferCount = 1;
        pRenderUnit->d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
        pRenderUnit->d3dpp.SwapEffect = m_SwapEffectFullscreen;
        pRenderUnit->d3dpp.hDeviceWindow = pD3DAdapterInfo->hWndDevice;
        pRenderUnit->d3dpp.EnableAutoDepthStencil = g_bUseDepthBuffer;
        pRenderUnit->d3dpp.Flags = 0;

        // Create device
        hr = g_pD3D->CreateDevice(iAdapter, pRenderUnit->DeviceType, 
                hWnd, // (this is the focus window)
                pRenderUnit->dwBehavior, &pRenderUnit->d3dpp, 
                &pRenderUnit->pd3dDevice);
        if (SUCCEEDED(hr)) {
            // Give the client app an opportunity to reject this mode
            // due to not enough video memory, or any other reason
            if (SUCCEEDED(hr = ConfirmMode(pRenderUnit->pd3dDevice))) {
                break;
            } else {
                if (pRenderUnit->pd3dDevice != NULL) {
                    pRenderUnit->pd3dDevice->Release();
                    pRenderUnit->pd3dDevice = NULL;
                }
            }
        }

        // If we get here, remember that CreateDevice or ConfirmMode failed, so
        // we can change the default mode next time
        bAtLeastOneFailure = TRUE;

        if (!FindNextLowerMode(pD3DDeviceInfo)) {
            break;
        }
    }

    return hr;
}

//-----------------------------------------------------------------------------
// Name: CheckWindowedFormat()
// Desc: 
//-----------------------------------------------------------------------------
static HRESULT CheckWindowedFormat(UINT iAdapter,
        D3DWindowedModeInfo* pD3DWindowedModeInfo, D3DAdaptersInfo *adapters)
{
    HRESULT hr;
    D3DAdapterInfo *pD3DAdapterInfo = adapters->m_Adapters[iAdapter];
    D3DDeviceInfo *pD3DDeviceInfo =
        &pD3DAdapterInfo->devices[pD3DAdapterInfo->dwCurrentDevice];
    BOOL bFormatConfirmed = FALSE;

    if (FAILED(hr = g_pD3D->CheckDeviceType(iAdapter,
                    pD3DDeviceInfo->DeviceType,
                    pD3DAdapterInfo->d3ddmDesktop.Format,
                    pD3DWindowedModeInfo->BackBufferFormat, TRUE))) {

        return hr;
    }

    // Confirm the device/format for HW vertex processing
    if (pD3DDeviceInfo->d3dCaps.DevCaps&D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
        if (pD3DDeviceInfo->d3dCaps.DevCaps&D3DDEVCAPS_PUREDEVICE) {
            pD3DWindowedModeInfo->dwBehavior =
                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE;

            if (SUCCEEDED(ConfirmDevice(&pD3DDeviceInfo->d3dCaps,
                            pD3DWindowedModeInfo->dwBehavior,
                            pD3DWindowedModeInfo->BackBufferFormat))) {

                bFormatConfirmed = TRUE;
            }
        }

        if (!bFormatConfirmed) {
            pD3DWindowedModeInfo->dwBehavior =
                D3DCREATE_HARDWARE_VERTEXPROCESSING;

            if (SUCCEEDED(ConfirmDevice(&pD3DDeviceInfo->d3dCaps,
                            pD3DWindowedModeInfo->dwBehavior,
                            pD3DWindowedModeInfo->BackBufferFormat))) {

                bFormatConfirmed = TRUE;
            }
        }

        if (!bFormatConfirmed) {
            pD3DWindowedModeInfo->dwBehavior = D3DCREATE_MIXED_VERTEXPROCESSING;

            if (SUCCEEDED(ConfirmDevice(&pD3DDeviceInfo->d3dCaps,
                            pD3DWindowedModeInfo->dwBehavior,
                            pD3DWindowedModeInfo->BackBufferFormat))) {

                bFormatConfirmed = TRUE;
            }
        }
    }

    // Confirm the device/format for SW vertex processing
    if (!bFormatConfirmed) {
        pD3DWindowedModeInfo->dwBehavior =
            D3DCREATE_SOFTWARE_VERTEXPROCESSING;

        if (SUCCEEDED(ConfirmDevice(&pD3DDeviceInfo->d3dCaps,
                        pD3DWindowedModeInfo->dwBehavior,
                        pD3DWindowedModeInfo->BackBufferFormat))) {

            bFormatConfirmed = TRUE;
        }
    }

    if (bFormatConfirmed && g_bMultithreaded) {
        pD3DWindowedModeInfo->dwBehavior |= D3DCREATE_MULTITHREADED;
    }

    // Find a suitable depth/stencil buffer format for this device/format
    if (bFormatConfirmed && g_bUseDepthBuffer) {
        if (!FindDepthStencilFormat(iAdapter, pD3DDeviceInfo->DeviceType,
                    pD3DWindowedModeInfo->BackBufferFormat,
                    &pD3DWindowedModeInfo->DepthStencilFormat)) {

            bFormatConfirmed = FALSE;
        }
    }

    if (!bFormatConfirmed) {
        return E_FAIL;
    }

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: CreateWindowedRenderUnit()
// Desc: 
//-----------------------------------------------------------------------------
static HRESULT CreateWindowedRenderUnit(HWND hWnd, RenderUnit* pRenderUnit,
        D3DAdaptersInfo *adapters)
{
    HRESULT hr;
    UINT iAdapter = pRenderUnit->iAdapter;
    D3DAdapterInfo* pD3DAdapterInfo = adapters->m_Adapters[iAdapter];
    DWORD iMonitor = pD3DAdapterInfo->iMonitor;
    D3DDeviceInfo* pD3DDeviceInfo;
    D3DDEVTYPE curType;

    // Find the best device for the primary adapter.  Use HAL
    // if it's there, otherwise SW, otherwise REF.
    pD3DAdapterInfo->dwCurrentDevice = 0xffff;

    curType = D3DDEVTYPE_FORCE_DWORD;
    for (DWORD iDevice = 0;
            iDevice < pD3DAdapterInfo->dwNumDevices; iDevice++) {

        pD3DDeviceInfo = &pD3DAdapterInfo->devices[iDevice];
        if (pD3DDeviceInfo->DeviceType == D3DDEVTYPE_HAL &&
                !pD3DAdapterInfo->bDisableHW &&
                pD3DDeviceInfo->bCanDoWindowed) {

            pD3DAdapterInfo->dwCurrentDevice = iDevice;
            curType = D3DDEVTYPE_HAL;
            break;
        } else if (pD3DDeviceInfo->DeviceType == D3DDEVTYPE_SW &&
                pD3DDeviceInfo->bCanDoWindowed) {

            pD3DAdapterInfo->dwCurrentDevice = iDevice;
            curType = D3DDEVTYPE_SW;
            // but keep looking
        } else if (pD3DDeviceInfo->DeviceType == D3DDEVTYPE_REF &&
                g_bAllowRef && curType != D3DDEVTYPE_SW) {

            pD3DAdapterInfo->dwCurrentDevice = iDevice;
            curType = D3DDEVTYPE_REF;
            // but keep looking
        }
    }

    if (pD3DAdapterInfo->dwCurrentDevice == 0xffff) {
        return D3DAPPERR_NOHARDWAREDEVICE;
    }

    pD3DDeviceInfo =
        &pD3DAdapterInfo->devices[pD3DAdapterInfo->dwCurrentDevice];

    D3DWindowedModeInfo D3DWindowedModeInfo;

    D3DWindowedModeInfo.DisplayFormat = pD3DAdapterInfo->d3ddmDesktop.Format;
    D3DWindowedModeInfo.BackBufferFormat = pD3DAdapterInfo->d3ddmDesktop.Format;

    if (FAILED(CheckWindowedFormat(iAdapter, &D3DWindowedModeInfo, adapters))) {
        D3DWindowedModeInfo.BackBufferFormat = D3DFMT_A8R8G8B8;

        if (FAILED(CheckWindowedFormat(iAdapter, &D3DWindowedModeInfo,
                        adapters))) {

            D3DWindowedModeInfo.BackBufferFormat = D3DFMT_X8R8G8B8;
            if (FAILED( CheckWindowedFormat(iAdapter,
                            &D3DWindowedModeInfo, adapters))) {

                D3DWindowedModeInfo.BackBufferFormat = D3DFMT_A1R5G5B5;
                if (FAILED(CheckWindowedFormat(iAdapter,
                                &D3DWindowedModeInfo, adapters))) {

                    D3DWindowedModeInfo.BackBufferFormat = D3DFMT_R5G6B5;
                    if (FAILED(CheckWindowedFormat(iAdapter,
                                    &D3DWindowedModeInfo, adapters))) {

                        return E_FAIL;
                    }
                }
            }
        }
    }

    pRenderUnit->DeviceType = pD3DDeviceInfo->DeviceType;
    pRenderUnit->dwBehavior = D3DWindowedModeInfo.dwBehavior;
    pRenderUnit->iMonitor = iMonitor;
    pRenderUnit->d3dpp.BackBufferWidth = 0;
    pRenderUnit->d3dpp.BackBufferHeight = 0;
    pRenderUnit->d3dpp.Windowed = TRUE;
    pRenderUnit->d3dpp.FullScreen_RefreshRateInHz = 0;
    pRenderUnit->d3dpp.FullScreen_PresentationInterval = 0;
    pRenderUnit->d3dpp.BackBufferFormat =
        D3DWindowedModeInfo.BackBufferFormat;
    pRenderUnit->d3dpp.AutoDepthStencilFormat =
        D3DWindowedModeInfo.DepthStencilFormat;
    pRenderUnit->d3dpp.BackBufferCount = 1;
    pRenderUnit->d3dpp.MultiSampleType = D3DMULTISAMPLE_NONE;
    pRenderUnit->d3dpp.SwapEffect = m_SwapEffectWindowed;
    pRenderUnit->d3dpp.hDeviceWindow = pD3DAdapterInfo->hWndDevice;
    pRenderUnit->d3dpp.EnableAutoDepthStencil = g_bUseDepthBuffer;
    pRenderUnit->d3dpp.Flags = 0;

    // Create device
    hr = g_pD3D->CreateDevice(iAdapter, pRenderUnit->DeviceType, hWnd,
            pRenderUnit->dwBehavior, &pRenderUnit->d3dpp,
            &pRenderUnit->pd3dDevice);
    if (FAILED(hr)) {
        return hr;
    }

    return S_OK;
}

//-----------------------------------------------------------------------------
// Name: DXUtil_ConvertAnsiStringToGeneric()
// Desc: This is a UNICODE conversion utility to convert a CHAR string into a
//       TCHAR string. cchDestChar defaults -1 which means it 
//       assumes strDest is large enough to store strSource
//-----------------------------------------------------------------------------
static VOID DXUtil_ConvertAnsiStringToGeneric(TCHAR* tstrDestination,
        const CHAR* strSource, int cchDestChar)
{
    if (tstrDestination==NULL || strSource==NULL || cchDestChar == 0) {
        return;
    }
        
#ifdef _UNICODE
    DXUtil_ConvertAnsiStringToWide(tstrDestination, strSource, cchDestChar);
#else
    if (cchDestChar == -1) {
        strcpy(tstrDestination, strSource);
    } else {
        strncpy(tstrDestination, strSource, cchDestChar);
        tstrDestination[cchDestChar - 1] = '\0';
    }
#endif
}

//-----------------------------------------------------------------------------
// Name: UpdateDeviceStats()
// Desc: Store device description
//-----------------------------------------------------------------------------
static VOID UpdateDeviceStats(RenderUnits *renderUnits,
        D3DAdaptersInfo *adapters)
{
    DWORD iRenderUnit;
    RenderUnit* pRenderUnit; 

    for (iRenderUnit = 0; iRenderUnit < renderUnits->m_dwNumRenderUnits;
            iRenderUnit++) {

        pRenderUnit = &renderUnits->m_RenderUnits[iRenderUnit];

        if (pRenderUnit->DeviceType == D3DDEVTYPE_REF) {
            lstrcpy(pRenderUnit->strDeviceStats, TEXT("REF"));
        } else if (pRenderUnit->DeviceType == D3DDEVTYPE_HAL) {
            lstrcpy(pRenderUnit->strDeviceStats, TEXT("HAL"));
        } else if( pRenderUnit->DeviceType == D3DDEVTYPE_SW) {
            lstrcpy(pRenderUnit->strDeviceStats, TEXT("SW"));
        }

        if (pRenderUnit->dwBehavior & D3DCREATE_HARDWARE_VERTEXPROCESSING &&
            pRenderUnit->dwBehavior & D3DCREATE_PUREDEVICE) {

            if (pRenderUnit->DeviceType == D3DDEVTYPE_HAL) {
                lstrcat(pRenderUnit->strDeviceStats, TEXT(" (pure hw vp)"));
            } else {
                lstrcat(pRenderUnit->strDeviceStats,
                        TEXT(" (simulated pure hw vp)"));
            }
        } else if (pRenderUnit->dwBehavior &
                D3DCREATE_HARDWARE_VERTEXPROCESSING) {

            if (pRenderUnit->DeviceType == D3DDEVTYPE_HAL) {
                lstrcat(pRenderUnit->strDeviceStats, TEXT(" (hw vp)"));
            } else {
                lstrcat(pRenderUnit->strDeviceStats,
                        TEXT(" (simulated hw vp)"));
            }
        } else if (pRenderUnit->dwBehavior & D3DCREATE_MIXED_VERTEXPROCESSING) {
            if (pRenderUnit->DeviceType == D3DDEVTYPE_HAL) {
                lstrcat(pRenderUnit->strDeviceStats, TEXT(" (mixed vp)"));
            } else {
                lstrcat(pRenderUnit->strDeviceStats,
                        TEXT(" (simulated mixed vp)"));
            }
        } else if (pRenderUnit->dwBehavior &
                D3DCREATE_SOFTWARE_VERTEXPROCESSING) {

            lstrcat(pRenderUnit->strDeviceStats, TEXT(" (sw vp)"));
        }

        if (pRenderUnit->DeviceType == D3DDEVTYPE_HAL) {
            lstrcat(pRenderUnit->strDeviceStats, TEXT(": "));
            TCHAR szDescription[300];
            DXUtil_ConvertAnsiStringToGeneric(szDescription, 
                    adapters->m_Adapters[pRenderUnit->iAdapter]->
                    d3dAdapterIdentifier.Description, 300);
            lstrcat(pRenderUnit->strDeviceStats, szDescription);
        }
    }
}

//-----------------------------------------------------------------------------
// Name: Initialize3DEnvironment()
// Desc: Set up D3D device(s)
//-----------------------------------------------------------------------------
static bool Initialize3DEnvironment(HWND hWnd, RenderUnits *renderUnits,
        D3DAdaptersInfo *adapters, MonitorsInfo *monitors)
{
    HRESULT hr;
    DWORD iAdapter;
    DWORD iMonitor;
    D3DAdapterInfo *pD3DAdapterInfo;
    MonitorInfo *pMonitorInfo;
    RenderUnit *pRenderUnit;
    MONITORINFO monitorInfo;

    if (in_fullscreen) {
        // Fullscreen mode.  Create a RenderUnit for each monitor (unless 
        // the user wants it black)

        if (g_bOneScreenOnly) {
            // Set things up to only create a RenderUnit on the best device
            for (iAdapter = 0;
                    iAdapter < adapters->m_dwNumAdapters; iAdapter++) {

                pD3DAdapterInfo = adapters->m_Adapters[iAdapter];
                pD3DAdapterInfo->bLeaveBlack = TRUE;
            }

            iAdapter = GetBestAdapter(adapters, monitors);
            if (iAdapter == NO_ADAPTER) {
                set_error_message("No compatible display devices");
                return false;
            } else {
                pD3DAdapterInfo = adapters->m_Adapters[iAdapter];
                pD3DAdapterInfo->bLeaveBlack = FALSE;
            }
        }

        for (iMonitor = 0; iMonitor < monitors->m_dwNumMonitors; iMonitor++) {
            pMonitorInfo = &monitors->m_Monitors[iMonitor];
            iAdapter = pMonitorInfo->iAdapter;
            if (iAdapter == NO_ADAPTER) {
                continue; 
            }

            pD3DAdapterInfo = adapters->m_Adapters[iAdapter];
            if (pD3DAdapterInfo->bDisableHW &&
                    !pD3DAdapterInfo->bHasAppCompatSW &&
                    !g_bAllowRef) {

                pD3DAdapterInfo->bLeaveBlack = TRUE;
            }

            if (!pD3DAdapterInfo->bLeaveBlack &&
                    pD3DAdapterInfo->dwNumDevices > 0) {

                pD3DAdapterInfo->hWndDevice = pMonitorInfo->hWnd;
                pRenderUnit = renderUnits->nextFreeRenderUnit();
                ZeroMemory(pRenderUnit, sizeof(RenderUnit));

                pRenderUnit->iAdapter = iAdapter;
                if (FAILED(hr = CreateFullscreenRenderUnit(hWnd,
                                pRenderUnit, adapters))) {

                    // skip this render unit and leave screen blank
                    renderUnits->trashLastRenderUnit();
                }
            }
        }
    } else {
        // Windowed mode, for test mode or preview window.  Just need one
        // RenderUnit.

#if 0
        GetClientRect(hWnd, &m_rcRenderTotal);
        GetClientRect(hWnd, &m_rcRenderCurDevice);
#endif

        iAdapter = GetBestAdapter(adapters, monitors);
        if (iAdapter == NO_ADAPTER) {
            set_error_message("Cannot find adapter to draw onto");
            return false;
        }

        pD3DAdapterInfo = adapters->m_Adapters[iAdapter];
        pD3DAdapterInfo->hWndDevice = hWnd;

        pRenderUnit = renderUnits->nextFreeRenderUnit();
        ZeroMemory(pRenderUnit, sizeof(RenderUnit));
        pRenderUnit->iAdapter = iAdapter;
        if (FAILED(hr = CreateWindowedRenderUnit(hWnd,
                        pRenderUnit, adapters))) {

            renderUnits->trashLastRenderUnit();
        }
    }

    // Once all mode changes are done, (re-)determine coordinates of all 
    // screens, and make sure windows still cover each screen
    for (iMonitor = 0; iMonitor < monitors->m_dwNumMonitors; iMonitor++) {
        pMonitorInfo = &monitors->m_Monitors[iMonitor];
        monitorInfo.cbSize = sizeof(MONITORINFO);
        GetMonitorInfo(pMonitorInfo->hMonitor, &monitorInfo);
        pMonitorInfo->rcScreen = monitorInfo.rcMonitor;
        if (in_fullscreen) {
            SetWindowPos(pMonitorInfo->hWnd,
                    HWND_TOPMOST, monitorInfo.rcMonitor.left, 
                    monitorInfo.rcMonitor.top,
                    monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
                    monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
                    SWP_NOACTIVATE);
        }
    }

#if 0
    // For fullscreen, determine bounds of the virtual screen containing all 
    // screens that are rendering.  Don't just use SM_XVIRTUALSCREEN, because 
    // we don't want to count screens that are just black
    DWORD iRenderUnit;

    if (in_fullscreen) {
        for (iRenderUnit = 0; iRenderUnit < renderUnits->m_dwNumRenderUnits;
                iRenderUnit++) {

            pRenderUnit = &renderUnits->m_RenderUnits[iRenderUnit];
            pMonitorInfo = &monitors->m_Monitors[pRenderUnit->iMonitor];
            UnionRect(&m_rcRenderTotal,
                    &m_rcRenderTotal, &pMonitorInfo->rcScreen);
        }
    }
#endif

    if (renderUnits->m_dwNumRenderUnits == 0) {
        set_error_message("Cannot find device to draw onto");
        return false;
    }

#if 0
    // Initialize D3D devices for all render units
    for (iRenderUnit = 0; iRenderUnit < renderUnits->m_dwNumRenderUnits;
            iRenderUnit++) {

        pRenderUnit = &renderUnits->m_RenderUnits[iRenderUnit];

        SwitchToRenderUnit(iRenderUnit);
        if (FAILED(hr = InitDeviceObjects())) {
            return false;
        }
        pRenderUnit->bDeviceObjectsInited = TRUE;

        if (FAILED(hr = RestoreDeviceObjects())) {
            return false;
        }
        pRenderUnit->bDeviceObjectsRestored = TRUE;
    }
#endif

    UpdateDeviceStats(renderUnits, adapters); 

    // Make sure all those display changes don't count as user mouse moves
    reset_mouse_motion();

    return true;
}

void setup_d3d_rendering(HWND hWnd)
{
    // get list of monitors
    MonitorsInfo monitors;
    EnumMonitors(&monitors);
    monitors.dump();

    // Create the D3D object.
    g_pD3D = Direct3DCreate8(D3D_SDK_VERSION);
    if (g_pD3D == NULL) {
        jessu_printf(THREAD_GL, "Failed to create Direct3D object");
        cleanup_d3d();
        set_error_message("Cannot create Direct3D object");
        return;
    }

    // get list of adapters, devices, and modes
    D3DAdaptersInfo adapters;
    BuildDeviceList(&adapters, &monitors);
    adapters.dump();

    // Make sure that at least one valid usable D3D device was found
    if (!adapters.compatibleDeviceFound()) {
        cleanup_d3d();
        set_error_message("No 3D graphics device found");
        return;
    }

#if 0
    // register as a screen saver
    // whoa we don't want this, it disables Ctrl-Alt-Delete and task switching.
    // (well, I tried it and it doesn't seem to disable that, but I don't
    // know what it does for us.)
    BOOL bUnused;
    SystemParametersInfo(SPI_SCREENSAVERRUNNING, TRUE, &bUnused, 0);
#endif

    // initialize the 3D device
    RenderUnits renderUnits;
    if (!Initialize3DEnvironment(hWnd, &renderUnits, &adapters, &monitors)) {
        cleanup_d3d();
        return;
    }
    renderUnits.dump();

#if 0
    // Direct3D version 8 can't handle D3DFMT_UNKNOWN for the BackBufferFormat,
    // so we must get the current format explicitly.
    D3DDISPLAYMODE theDisplayMode;
    int status = g_pD3D->GetAdapterDisplayMode(
            D3DADAPTER_DEFAULT, &theDisplayMode);
    if (FAILED(status)) {
        jessu_printf(THREAD_GL, "Cannot get display mode (%d)",
                status & 0xffff);
        cleanup_d3d();
        set_error_message("Cannot get display mode");
        return;
    }

    // Set up the structure used to create the D3DDevice.
    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = theDisplayMode.Format;
    if (in_fullscreen) {
        // here we could do all sorts of things, like figure out if we're
        // running in software or hardware, and degrade the resolution if
        // we're in software.

        d3dpp.Windowed = FALSE;
        d3dpp.BackBufferWidth = 1024;  // XXX
        d3dpp.BackBufferHeight = 768;
    } else {
        d3dpp.Windowed = TRUE;
    }

    // Create the D3DDevice (default card, hardware device (HAL),
    // software vertex processing for compatibility).
    // replace HAL with REF for software rendering.  this might be useful
    // on a client's computer to see if their hardware is slow or if
    // they're actually just in software.
    status = g_pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd,
            D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &g_pd3dDevice);
    if (FAILED(status)) {
        jessu_printf(THREAD_GL, "Failed to create Direct3D device (%d)",
                status & 0xffff);
        cleanup_d3d();
        set_error_message("Cannot create Direct3D device");
        return;
    }
#else
    g_pd3dDevice = renderUnits.m_RenderUnits[0].pd3dDevice;
    // null out other pointer so no one else destroys it
    renderUnits.m_RenderUnits[0].pd3dDevice = NULL;
#endif

    // create our vertex buffer
    if (FAILED(g_pd3dDevice->CreateVertexBuffer(
                    4*sizeof(textured_colored_2d_vertex),
                    0, D3DFVF_CUSTOMVERTEX,
                    D3DPOOL_DEFAULT, &g_pVB))) {

        jessu_printf(THREAD_GL, "Failed to create Direct3D vertex buffer");
        cleanup_d3d();
        set_error_message("Cannot create vertex buffer");
        return;
    }

    // Turn off culling
    g_pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    // Turn off D3D lighting
    g_pd3dDevice->SetRenderState(D3DRS_LIGHTING, FALSE);

    // Turn off the zbuffer
    g_pd3dDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
}

void cleanup_d3d()
{
    if (g_pVB != NULL) {
        g_pVB->Release();
        g_pVB = NULL;
    }
    if (g_pd3dDevice != NULL) {
        g_pd3dDevice->Release();
        g_pd3dDevice = NULL;
    }
    if (g_pD3D != NULL) {
        g_pD3D->Release();
        g_pD3D = NULL;
    }
}

#endif // USE_D3D
