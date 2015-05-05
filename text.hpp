
#ifndef __TEXT_H__
#define __TEXT_H__

#include "jessu.h"

#if USE_D3D
#include <d3dx8.h>

struct Filename_notice {
    Filename_notice();
    ~Filename_notice();

    int width, height;

    LPDIRECT3DDEVICE8 pd3dDevice;

    LPDIRECT3DTEXTURE8 pTexture;    // The d3d texture for this string
    LPDIRECT3DVERTEXBUFFER8 pVB;

    DWORD dwSavedStateBlock;
    DWORD dwDrawTextStateBlock;
};

Filename_notice *prepare_filename_notice(LPDIRECT3DDEVICE8 pd3dDevice,
        char const *beautiful_filename);
void draw_filename_notice(int x, int y, Filename_notice *notice);
void draw_filename_background(int y1, int y2, Filename_notice *notice);
#else
struct Filename_notice {
    // nothing
};
#endif

#endif // __TEXT_H__
