
#include <math.h>

#include "text.hpp"

#if USE_D3D

#if TV_VIEWING
#define FONT_HEIGHT     39
#define FONT_NAME       "Verdana"
#else
#define FONT_HEIGHT     19
#define FONT_NAME       "Verdana"
#endif

struct FONT2DVERTEX {
    D3DXVECTOR4 p;
    DWORD color;
    FLOAT tu, tv;
};

#define D3DFVF_FONT2DVERTEX (D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1)

FONT2DVERTEX InitFont2DVertex(const D3DXVECTOR4 &p, D3DCOLOR color,
        FLOAT tu, FLOAT tv)
{
    FONT2DVERTEX v;

    v.p = p;
    v.color = color;
    v.tu = tu;
    v.tv = tv;

    return v;
}

Filename_notice::Filename_notice()
{
    pd3dDevice = NULL;

    pTexture = NULL;
    pVB = NULL;

    dwSavedStateBlock = 0;
    dwDrawTextStateBlock = 0;
}

Filename_notice::~Filename_notice()
{
    if (dwSavedStateBlock != 0) {
        pd3dDevice->DeleteStateBlock(dwSavedStateBlock);
    }
    if (dwDrawTextStateBlock != 0) {
        pd3dDevice->DeleteStateBlock(dwDrawTextStateBlock);
    }
    if (pTexture != NULL) {
        pTexture->Release();
    }
    if (pVB != NULL) {
        pVB->Release();
    }
}

static void apply_render_state(LPDIRECT3DDEVICE8 pd3dDevice)
{
    // we don't use a zbuffer
    pd3dDevice->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);

    // blend polygon based on vertex alpha
#if 1
    pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    pd3dDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    pd3dDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
#else
    pd3dDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
#endif

#if 1
    // only blend if alpha >= 8
    pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    pd3dDevice->SetRenderState(D3DRS_ALPHAREF, 0x08);
    pd3dDevice->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
#else
    pd3dDevice->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
#endif

    // various crap
    pd3dDevice->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    pd3dDevice->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    pd3dDevice->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    pd3dDevice->SetRenderState(D3DRS_CLIPPING, TRUE);
    pd3dDevice->SetRenderState(D3DRS_EDGEANTIALIAS, FALSE);
    pd3dDevice->SetRenderState(D3DRS_CLIPPLANEENABLE, FALSE);
    pd3dDevice->SetRenderState(D3DRS_VERTEXBLEND, FALSE);
    pd3dDevice->SetRenderState(D3DRS_INDEXEDVERTEXBLENDENABLE, FALSE);
    pd3dDevice->SetRenderState(D3DRS_FOGENABLE, FALSE);

    // for each pixel, blend the color and alpha based on alpha
    pd3dDevice->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    pd3dDevice->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    pd3dDevice->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
#if 1
    pd3dDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_POINT);
    pd3dDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_POINT);
#else // looks blurry
    pd3dDevice->SetTextureStageState(0, D3DTSS_MINFILTER, D3DTEXF_LINEAR);
    pd3dDevice->SetTextureStageState(0, D3DTSS_MAGFILTER, D3DTEXF_LINEAR);
#endif
    pd3dDevice->SetTextureStageState(0, D3DTSS_MIPFILTER, D3DTEXF_NONE);
    pd3dDevice->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    pd3dDevice->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS,
            D3DTTFF_DISABLE);
    pd3dDevice->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    pd3dDevice->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
}

Filename_notice *prepare_filename_notice(LPDIRECT3DDEVICE8 pd3dDevice,
        char const *filename)
{
    HRESULT result;
    Filename_notice *notice = NULL;
    HDC hDC = NULL;
    HFONT hFont = NULL;
    SIZE size;
    int width, height;
    LPDIRECT3DTEXTURE8 pTexture = NULL;
    LPDIRECT3DVERTEXBUFFER8 pVB = NULL;
    DWORD *pBitmapBits = NULL;
    BITMAPINFO bmi;
    HBITMAP hbmBitmap = NULL;
    D3DLOCKED_RECT d3dlr;
    BYTE *pDstRow;
    int x, y;
   
    hDC = CreateCompatibleDC(NULL);

    SetMapMode(hDC, MM_TEXT);

    hFont = CreateFont(
            FONT_HEIGHT,                // height
            0,                          // width (0 = closest)
            0,                          // escapement (0 = none)
            0,                          // orientation (0 = none)
            FW_NORMAL,                  // bold
            FALSE,                      // italic
            FALSE,                      // underline
            FALSE,                      // strikeout
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,         // TrueType (OUT_TT_PRECIS) doesn't help
            CLIP_DEFAULT_PRECIS,
            ANTIALIASED_QUALITY,
            VARIABLE_PITCH,
            FONT_NAME);
    if (hFont == NULL) {
        goto done;
    }

    // Set text properties
    SelectObject(hDC, hFont);
    SetTextColor(hDC, RGB(255,255,255));
    SetBkColor(hDC, 0x00000000);
    SetTextAlign(hDC, TA_TOP);

    GetTextExtentPoint32(hDC, filename, strlen(filename), &size);
    width = size.cx;
    height = size.cy;

    // Create a new texture for the font
    result = pd3dDevice->CreateTexture(width, height, 1, 0, D3DFMT_A4R4G4B4,
            D3DPOOL_MANAGED, &pTexture);
    if (FAILED(result)) {
        goto done;
    }

    // Prepare to create a bitmap
    ZeroMemory(&bmi.bmiHeader, sizeof(BITMAPINFOHEADER));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;       // negative means top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biCompression = BI_RGB;
    bmi.bmiHeader.biBitCount = 32;

    hbmBitmap = CreateDIBSection(hDC, &bmi, DIB_RGB_COLORS,
            (VOID**)&pBitmapBits, NULL, 0);
    SelectObject(hDC, hbmBitmap);

    ExtTextOut(hDC, 0, 0, ETO_OPAQUE, NULL, filename, strlen(filename), NULL);

    // Lock the surface and write the alpha values for the set pixels
    pTexture->LockRect(0, &d3dlr, 0, 0);
    pDstRow = (BYTE*)d3dlr.pBits;

    for (y = 0; y < height; y++) {
        WORD *pDst16 = (WORD *)pDstRow;

        for (x = 0; x < width; x++) {
            BYTE bAlpha = (BYTE)((pBitmapBits[width*y + x] & 0xff) >> 4);

            if (bAlpha > 0) {
                *pDst16++ = (WORD)((bAlpha << 12) | 0x0fff);
            } else {
                *pDst16++ = (WORD)(0x0000);
            }
        }

        pDstRow += d3dlr.Pitch;
    }

    // Done updating texture
    pTexture->UnlockRect(0);

    // Create vertices
    result = pd3dDevice->CreateVertexBuffer(4*sizeof(FONT2DVERTEX),
            D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, 0, D3DPOOL_DEFAULT, &pVB);
    if (FAILED(result)) {
        goto done;
    }

    notice = new Filename_notice;
    notice->width = width;
    notice->height = height;
    notice->pd3dDevice = pd3dDevice;
    notice->pTexture = pTexture;
    pTexture = NULL;
    notice->pVB = pVB;
    pVB = NULL;

    pd3dDevice->BeginStateBlock();
    apply_render_state(pd3dDevice);
    pd3dDevice->SetTexture(0, notice->pTexture);
    pd3dDevice->EndStateBlock(&notice->dwSavedStateBlock);

    pd3dDevice->BeginStateBlock();
    apply_render_state(pd3dDevice);
    pd3dDevice->SetTexture(0, notice->pTexture);
    pd3dDevice->EndStateBlock(&notice->dwDrawTextStateBlock);

done:
    if (pVB != NULL) {
        pVB->Release();
    }
    if (pTexture != NULL) {
        pTexture->Release();
    }
    if (hbmBitmap != NULL) {
        DeleteObject(hbmBitmap);
    }
    if (hFont != NULL) {
        DeleteObject(hFont);
    }
    if (hDC != NULL) {
        DeleteDC(hDC);
    }

    return notice;
}

void draw_filename_notice(int iy1, int iy2, Filename_notice *notice)
{
    if (notice == NULL) {
        return;
    }

    LPDIRECT3DDEVICE8 pd3dDevice = notice->pd3dDevice; // convenience

    D3DVIEWPORT8 vp;
    pd3dDevice->GetViewport(&vp);

    float x1 = (float)(((int)vp.Width - notice->width)/2) - 0.5f;
    float y1 = (float)(iy1 + (iy2 - iy1 - notice->height)/2) - 0.5f;
    float x2 = x1 + notice->width;
    float y2 = y1 + notice->height;

    pd3dDevice->CaptureStateBlock(notice->dwSavedStateBlock);
    pd3dDevice->ApplyStateBlock(notice->dwDrawTextStateBlock);

    pd3dDevice->SetVertexShader(D3DFVF_FONT2DVERTEX);
    pd3dDevice->SetPixelShader(NULL);
    pd3dDevice->SetStreamSource(0, notice->pVB, sizeof(FONT2DVERTEX));

    FONT2DVERTEX* pVertices;

#if 1
    notice->pVB->Lock(0, 0, (BYTE **)&pVertices, D3DLOCK_DISCARD);
    pVertices[0] = InitFont2DVertex(
            D3DXVECTOR4(x1, y1, 0.5, 1), 0xffffffff, 0, 0);
    pVertices[1] = InitFont2DVertex(
            D3DXVECTOR4(x1, y2, 0.5, 1), 0xffffffff, 0, 1);
    pVertices[2] = InitFont2DVertex(
            D3DXVECTOR4(x2, y2, 0.5, 1), 0xffffffff, 1, 1);
    pVertices[3] = InitFont2DVertex(
            D3DXVECTOR4(x2, y1, 0.5, 1), 0xffffffff, 1, 0);
    notice->pVB->Unlock();

    // change this to 100 or 1000 to see drawing problem
    for (int i = 0; i < 1; i++) {
        pd3dDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0, 2);
    }
#else
    static double spread = 1.0;

    for (int i = -250; i <= 250; i++) {
        double opacity = exp(-float(i) * i / (spread*spread))/spread;
        int alpha = (int)(opacity*0xff);
        if (alpha > 0) {
            DWORD color = (alpha << 24) | 0x00ffffff;

            notice->pVB->Lock(0, 0, (BYTE **)&pVertices, D3DLOCK_DISCARD);
            pVertices[0] = InitFont2DVertex(
                    D3DXVECTOR4(x1 + i, y1, 0.5, 1), color, 0, 0);
            pVertices[1] = InitFont2DVertex(
                    D3DXVECTOR4(x1 + i, y2, 0.5, 1), color, 0, 1);
            pVertices[2] = InitFont2DVertex(
                    D3DXVECTOR4(x2 + i, y2, 0.5, 1), color, 1, 1);
            pVertices[3] = InitFont2DVertex(
                    D3DXVECTOR4(x2 + i, y1, 0.5, 1), color, 1, 0);
            notice->pVB->Unlock();

            pd3dDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0, 2);
        }
    }

    spread += 2;
#endif

    pd3dDevice->ApplyStateBlock(notice->dwSavedStateBlock);
}

void
draw_filename_background(int iy1, int iy2, Filename_notice *notice)
{
    if (notice == NULL) {
        return;
    }

    LPDIRECT3DDEVICE8 pd3dDevice = notice->pd3dDevice; // convenience

    D3DVIEWPORT8 vp;
    pd3dDevice->GetViewport(&vp);

    float x1 = 0;
    float y1 = (float)iy1;
    float x2 = (float)vp.Width;
    float y2 = (float)iy2;

    pd3dDevice->CaptureStateBlock(notice->dwSavedStateBlock);
    pd3dDevice->ApplyStateBlock(notice->dwDrawTextStateBlock);

    pd3dDevice->SetTexture(0, NULL);
    pd3dDevice->SetVertexShader(D3DFVF_FONT2DVERTEX);
    pd3dDevice->SetPixelShader(NULL);
    pd3dDevice->SetStreamSource(0, notice->pVB, sizeof(FONT2DVERTEX));

    FONT2DVERTEX* pVertices;

    // dark almost opaque rectangle
    DWORD color = 0xB0000000;

    notice->pVB->Lock(0, 0, (BYTE **)&pVertices, D3DLOCK_DISCARD);
    pVertices[0] = InitFont2DVertex(
            D3DXVECTOR4(x1, y1, 0.5, 1), color, 0, 0);
    pVertices[1] = InitFont2DVertex(
            D3DXVECTOR4(x1, y2, 0.5, 1), color, 0, 1);
    pVertices[2] = InitFont2DVertex(
            D3DXVECTOR4(x2, y2, 0.5, 1), color, 1, 1);
    pVertices[3] = InitFont2DVertex(
            D3DXVECTOR4(x2, y1, 0.5, 1), color, 1, 0);
    notice->pVB->Unlock();

    pd3dDevice->DrawPrimitive(D3DPT_TRIANGLEFAN, 0, 2);

    // semi-transparent thin white lines
    color = 0x80ffffff;

    notice->pVB->Lock(0, 0, (BYTE **)&pVertices, D3DLOCK_DISCARD);
    pVertices[0] = InitFont2DVertex(
            D3DXVECTOR4(x1, y1, 0.5, 1), color, 0, 0);
    pVertices[1] = InitFont2DVertex(
            D3DXVECTOR4(x2, y1, 0.5, 1), color, 0, 1);
    pVertices[2] = InitFont2DVertex(
            D3DXVECTOR4(x1, y2 - 1, 0.5, 1), color, 1, 1);
    pVertices[3] = InitFont2DVertex(
            D3DXVECTOR4(x2, y2 - 1, 0.5, 1), color, 1, 0);
    notice->pVB->Unlock();

    pd3dDevice->DrawPrimitive(D3DPT_LINELIST, 0, 2);

    pd3dDevice->ApplyStateBlock(notice->dwSavedStateBlock);
}
#endif
