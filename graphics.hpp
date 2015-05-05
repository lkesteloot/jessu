
#ifndef __GRAPHICS_HPP__
#define __GRAPHICS_HPP__

#include "jessu.h"

#if USE_D3D
#include <d3dx8.h>

struct textured_colored_2d_vertex {
    float v[3];
    DWORD c;
    float t[2];

    void Vertex2f(float x, float y) {
        v[0] = x;
        v[1] = y;
        v[2] = 0.0f;
    }
    void Color4f(float r, float g, float b, float a) {
        c = (Float_to_char(a) << 24) |
            (Float_to_char(b) << 16) |
            (Float_to_char(g) << 8)  |
            (Float_to_char(r) << 0);
    }
    void TexCoord2f(float u, float v) {
        t[0] = u;
        t[1] = v;
    }

private:
    unsigned char Float_to_char(float x) {
        return (unsigned char)(int)(x*255);
    }
};

#define D3DFVF_CUSTOMVERTEX (D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1)

extern LPDIRECT3D8 g_pD3D;
extern LPDIRECT3DDEVICE8 g_pd3dDevice;
extern LPDIRECT3DVERTEXBUFFER8 g_pVB;

void setup_d3d_rendering(HWND hWnd);
void cleanup_d3d();

#endif // USE_D3D

#endif // __GRAPHICS_HPP__
