/*
 * benchmark.cpp
 *
 * $Id: benchmark.cpp,v 1.4 2003/08/10 18:24:30 grantham Exp $
 *
 * $Log: benchmark.cpp,v $
 * Revision 1.4  2003/08/10 18:24:30  grantham
 * fix fill rate computation
 *
 * Revision 1.3  2003/01/28 02:46:21  lk
 * Much better logging, horizontal resize on load
 *
 * Revision 1.2  2002/03/19 09:22:41  grantham
 * fix texture download test by using correct filter, fix timing loop with || and not &&
 *
 * Revision 1.1  2002/03/18 10:00:12  grantham
 * rough draft of basic benchmark functionality, run with /b, will take at least 230 seconds
 *
 *
 */

#include <time.h>
#include <string.h>
#include <windows.h>
#include <GL/gl.h>
#include "benchmark.h"

static char *map_GL_enum_to_string(unsigned int num);

void bench_write_os_version(FILE *log)
{
    OSVERSIONINFO vi;

    memset(&vi, 0, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    GetVersionEx(&vi);

    fprintf(log, "Operating System version: %d.%d build %d, "
	    "annotation \"%s\"\n", vi.dwMajorVersion, vi.dwMinorVersion,
	    vi.dwBuildNumber, vi.szCSDVersion);
    fprintf(log, "Platform ID: %d\n", vi.dwPlatformId);
}

void bench_write_sys_info(FILE *log)
{
    SYSTEM_INFO si;

    memset(&si, 0, sizeof(si));
    GetSystemInfo(&si);

    fprintf(log, "Processor %d, level %d, revision %d\n",
	    si.wProcessorArchitecture, si.wProcessorLevel,
	    si.wProcessorRevision);
    fprintf(log, "%d processors, active mask %08X\n", si.dwNumberOfProcessors,
	    si.dwActiveProcessorMask);
}

void bench_write_gfx_info(FILE *log)
{
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    HDC dc;
    int depth;
    
    dc = GetDC(NULL);
    depth = GetDeviceCaps(dc, BITSPIXEL);
    ReleaseDC(NULL, dc);

    fprintf(log, "Default screen is %d by %d pixels with %d bits per pixel.\n",
	    width, height, depth);
    fprintf(log, "OpenGL Vendor: %s\n", glGetString(GL_VENDOR));
    fprintf(log, "OpenGL Version: %s\n", glGetString(GL_VERSION));
    fprintf(log, "OpenGL Renderer: %s\n", glGetString(GL_RENDERER));
    fprintf(log, "OpenGL Extensions: %s\n", glGetString(GL_EXTENSIONS));
}

int bench_probe_max_tex_size_RGBA8(FILE * /* log */)
{
    unsigned int side = 32;
    int error;
    GLuint measured_width;

    error = GL_NO_ERROR;
    do {
	side *= 2;
        glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA8, side, side, 0,
		GL_RGB, GL_UNSIGNED_BYTE, NULL);
	glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,
		(GLint *)&measured_width);
    }while(side < (1u << 31) && side == measured_width);

    return side / 2;
}

//
// "side" >= 128
// An OpenGL context must be active and viewport and matrices must be such
//   that the following triangle will generate fragments:
//   <0, 0, 0>
//   <1, 0, 0>
//   <0, 1, 0>
//
#define MAX_TEXTURE_SIDE (128)
#define MAX_TEXTURE_MEM_PROBE (128 * 1024 * 1024)
#define MAX_TEXTURES_RESIDENT \
    (MAX_TEXTURE_MEM_PROBE / (MAX_TEXTURE_SIDE * MAX_TEXTURE_SIDE * 4))
int bench_probe_max_resident_RGBA8(FILE * /* log */, int side)
{
    static unsigned char *texture_images[MAX_TEXTURES_RESIDENT];
    static GLuint texture_names[MAX_TEXTURES_RESIDENT];
    static GLboolean texture_residency[MAX_TEXTURES_RESIDENT];
    int current;
    int guessed_texture_size;
    int guessed_max_textures;
    bool done = false;
    int i;

    // More like side squared times 4 times 4/3 for MIPmaps, but guess short.
    guessed_texture_size = side * side * 4;
    guessed_max_textures = MAX_TEXTURE_MEM_PROBE / guessed_texture_size;

    glGenTextures(guessed_max_textures, texture_names);

    current = 0;
    glEnable(GL_TEXTURE_2D);
    while(current <= guessed_max_textures && !done) {
        texture_images[current] = new unsigned char[guessed_texture_size];
	glBindTexture(GL_TEXTURE_2D, texture_names[current]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, side, side, 0, GL_RGBA,
		GL_UNSIGNED_BYTE, texture_images[current]);
	// Technically we have to make sure these actually summon the texels,
	//  but I suspect a Begin with the texture bound will cause the
	//  texture to become resident.
	glBegin(GL_TRIANGLES);
	    glTexCoord2f(0, 0);
	    glVertex3f(0, 0, 0);
	    glTexCoord2f(1, 0);
	    glVertex3f(1, 0, 0);
	    glTexCoord2f(0, 1);
	    glVertex3f(0, 1, 0);
	glEnd();
	glFinish();
	done = !glAreTexturesResident(current + 1, texture_names,
		texture_residency);
	current++;
    }

    for(i = 0; i < current; i++)
        delete[] texture_images[i];

    glBindTexture(GL_TEXTURE_2D, 0);
    glDeleteTextures(guessed_max_textures, texture_names);

    return current - 1;
}

void bench_write_tex_memory(FILE *log)
{
    int test_tex_size;
    int max_tex_size;
    GLenum error;

    error = glGetError();
    if(error != GL_NO_ERROR) {
        fprintf(log, "bench_write_tex_memory: Unexpected initial OpenGL "
		"error: %04X\n", error);
    }

    max_tex_size = bench_probe_max_tex_size_RGBA8(log);
    fprintf(log, "Maximum RGBA8 texture size per side: %d\n", max_tex_size);

    test_tex_size = 128;
    while(test_tex_size <= max_tex_size) {
	int results = bench_probe_max_resident_RGBA8(log, test_tex_size);
	fprintf(log, "Maximum %dx%d resident RGBA8 textures: at least %d\n",
		test_tex_size, test_tex_size, results);
	test_tex_size *= 2;
    }
}

class bench_base {
public:
    virtual bool run_one_test(void) = 0;
    double run_benchmark(FILE *log);
};

#define TEST_LENGTH_SECONDS 10
#define MINIMUM_REPS_REQUIRED 10
double bench_base::run_benchmark(FILE * /* log */)
{
    int i;
    DWORD prevTime, curTime;
    int repsPerSecond;
    int repCount;
    double elapsed;

    //
    // Do one second's worth of testing to guess how often to check
    // the clock so we can reduce the overhead of the clock check during
    // the real timing test.
    //
    prevTime = timeGetTime();
    glGetError(); // flush errors
    repCount = 0;
    do {

	if(!run_one_test())
	    return -1;

	repCount ++;

	curTime = timeGetTime();
	if(prevTime > curTime) {
	    // XXX grantham - Probably should test this to make sure it works
	    curTime += (1U << 31);
	    prevTime -= (1U<< 31);
	}
	elapsed = (curTime - prevTime) / 1000.0;

    } while(elapsed < 1.0);

    // Give 10% for clock overhead
    repsPerSecond = (int)(repCount/elapsed*1.1);

    if(repsPerSecond == 0)
	// Oy - this means we only ran one test, so one rep took more than 
	// one second.  We really should increase our test length to get
	// better timing samples but I have forgotten everything I learned
	// about statistics, so I don't know what "better" is.
	repsPerSecond = 1;

    prevTime = timeGetTime();
    repCount = 0; 
    do {

	for(i = 0; i < repsPerSecond; i++)
	    run_one_test();

	repCount += repsPerSecond;

	curTime = timeGetTime();
	if(prevTime > curTime) {
	    // XXX grantham - Probably should test this to make sure it works
	    curTime += (1U << 31);
	    prevTime -= (1U << 31);
	}
	elapsed = (curTime - prevTime) / 1000.0;

    // Really, this should be "} while(not statistically significant);"
    } while(repCount < MINIMUM_REPS_REQUIRED || elapsed < TEST_LENGTH_SECONDS);

    return repCount / (float)elapsed;
}

#ifndef GL_RGB5_A1
#define GL_RGB5_A1 0x8057
#endif

#ifndef GL_UNSIGNED_INT_8_8_8_8_REV
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#endif

#ifndef GL_UNSIGNED_SHORT_5_5_5_1
#define GL_UNSIGNED_SHORT_5_5_5_1 0x8034
#endif

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

class bench_TexImage2D : public bench_base {
public:
    GLenum m_int_fmt;
    GLenum m_fmt;
    GLenum m_type;
    int m_side;
    unsigned char *m_buffer;
    void set_params(GLenum int_fmt, GLenum fmt, GLenum type, int side, unsigned char *buffer);
    virtual bool run_one_test(void);
};

void bench_TexImage2D::set_params(GLenum int_fmt, GLenum fmt, GLenum type,
	int side, unsigned char *buffer)
{
    m_int_fmt = int_fmt;
    m_fmt = fmt;
    m_type = type;
    m_side = side;
    m_buffer = buffer;
}

bool bench_TexImage2D::run_one_test(void)
{
    unsigned char four_pixels[4 * 4];

    glTexImage2D(GL_TEXTURE_2D, 0, m_int_fmt, m_side, m_side, 0,
	    m_fmt, m_type, m_buffer);
    glBegin(GL_TRIANGLES);
        glTexCoord2f(0, 0);
	glVertex2f(0, 0);
        glTexCoord2f(1, 0);
	glVertex2f(2, 0);
        glTexCoord2f(0, 1);
	glVertex2f(0, 2);
    glEnd();
    glFinish();

    return glGetError() == GL_NO_ERROR;
}

void bench_test_teximage_config(FILE *log, GLenum int_fmt, GLenum fmt,
	GLenum type)
{
    double tps;
    unsigned char *buffer = new unsigned char[128 * 128 * 4];
    bench_TexImage2D bench;

    bench.set_params(int_fmt, fmt, type, 128, buffer);

    tps = 128 * 128 * bench.run_benchmark(log);
    fprintf(log, "\t%s <- 128x128, %s, %s:",
	    map_GL_enum_to_string(int_fmt),
	    map_GL_enum_to_string(fmt),
	    map_GL_enum_to_string(type));

    if(tps == -1)
	fprintf(log, "error encountered\n");
    else
        fprintf(log, " %f Mtexels/sec\n", tps / 1e6);

    delete[] buffer;
}

void bench_write_teximage_speed(FILE *log)
{
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 100, 0, 100, -1, 1);

    fprintf(log, "Texture download:\n");

    bench_test_teximage_config(log, GL_RGBA, GL_RGBA, GL_UNSIGNED_BYTE);
    bench_test_teximage_config(log, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE);
    bench_test_teximage_config(log, GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE);
    bench_test_teximage_config(log, GL_RGBA8, GL_RGBA,
	    GL_UNSIGNED_INT_8_8_8_8_REV);
    bench_test_teximage_config(log, GL_RGBA8, GL_BGRA,
	    GL_UNSIGNED_INT_8_8_8_8_REV);
    bench_test_teximage_config(log, GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_BYTE);
    bench_test_teximage_config(log, GL_RGB5_A1, GL_BGRA, GL_UNSIGNED_BYTE);
    bench_test_teximage_config(log, GL_RGB5_A1, GL_RGBA,
	    GL_UNSIGNED_SHORT_5_5_5_1);
    bench_test_teximage_config(log, GL_RGB5_A1, GL_BGRA,
	    GL_UNSIGNED_SHORT_5_5_5_1);
}

class bench_TwoBlendedQuadsets : public bench_base {
public:
    int m_tiles_across;
    int m_tiles_down;
    GLuint m_tex1;
    GLuint m_tex2;
    int m_ox;
    int m_oy;
    int m_reps;
    double m_pixels_filled;
    void set_params(int width, int height, GLuint tex1, GLuint tex2, int reps);
    virtual bool run_one_test(void);
};

void bench_TwoBlendedQuadsets::set_params(int width, int height, GLuint tex1, GLuint tex2, int reps)
{
    m_tiles_across = (width + 127) / 128;
    m_tiles_down = (height + 127) / 128;
    m_ox = width / 2 - m_tiles_across / 2 * 128;
    m_oy = height / 2 - m_tiles_down / 2 * 128;
    m_tex1 = tex1;
    m_tex2 = tex2;
    m_reps = reps;
    m_pixels_filled = 0;
}

bool bench_TwoBlendedQuadsets::run_one_test(void)
{
    int i, j, k;
    unsigned char four_pixels[4 * 4];

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    for(k = 0; k < m_reps; k++) {
	glBindTexture(GL_TEXTURE_2D, m_tex1);
	glColor4f(1, 1, 1, 0.25);
	glBegin(GL_QUADS);
	for(i = 0; i < m_tiles_across; i++)
	    for(j = 0; j < m_tiles_down; j++) {
		glTexCoord2f(0, 0);
		glVertex2f(m_ox + (i + 0) * 128 - 16, m_oy + (j + 0) * 128 - 8);
		glTexCoord2f(0, 0);
		glVertex2f(m_ox + (i + 1) * 128 - 16, m_oy + (j + 0) * 128 - 8);
		glTexCoord2f(0, 0);
		glVertex2f(m_ox + (i + 1) * 128 - 16, m_oy + (j + 1) * 128 - 8);
		glTexCoord2f(0, 0);
		glVertex2f(m_ox + (i + 0) * 128 - 16, m_oy + (j + 1) * 128 - 8);
		m_pixels_filled += 128 * 128;
	    }
	glEnd();

	glBindTexture(GL_TEXTURE_2D, m_tex2);
	glColor4f(1, 1, 1, 0.75);
	glBegin(GL_QUADS);
	for(i = 0; i < m_tiles_across; i++)
	    for(j = 0; j < m_tiles_down; j++) {
		glTexCoord2f(0, 0);
		glVertex2f(m_ox + (i + 0) * 128 + 8, m_oy + (j + 0) * 128 + 16);
		glTexCoord2f(0, 0);
		glVertex2f(m_ox + (i + 1) * 128 + 8, m_oy + (j + 0) * 128 + 16);
		glTexCoord2f(0, 0);
		glVertex2f(m_ox + (i + 1) * 128 + 8, m_oy + (j + 1) * 128 + 16);
		glTexCoord2f(0, 0);
		glVertex2f(m_ox + (i + 0) * 128 + 8, m_oy + (j + 1) * 128 + 16);
		m_pixels_filled += 128 * 128;
	    }
	glEnd();
    }

    glFinish();

    return true;
}

void bench_write_fill_rate(FILE *log)
{
    GLuint tex1, tex2;
    unsigned char *image = new unsigned char[128 * 128 * 4];
    bench_TwoBlendedQuadsets blend;
    double mpixPerSec;
    double repsPerSec;

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, 0, height, -1, 1);

    glGenTextures(1, &tex1);
    glGenTextures(1, &tex2);

    // RGBA8

    glBindTexture(GL_TEXTURE, tex1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0,
	GL_RGBA8, GL_UNSIGNED_BYTE, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE, tex2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0,
	GL_RGBA8, GL_UNSIGNED_BYTE, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    blend.set_params(width, height, tex1, tex2, 20);
    repsPerSec = blend.run_benchmark(log);

    mpixPerSec = 2 * blend.m_tiles_across * blend.m_tiles_down *
	blend.m_reps / 1e6 *
	128 * 128 * repsPerSec;
    fprintf(log, "Fill rate for GL_RGBA8 GL_BLEND without DEPTH_TEST: %f "
	    "Mpixel/sec\n", mpixPerSec);
    fprintf(log, "    Estimated frame rate in this mode: %f frames per sec\n",
	    repsPerSec);

    // RGB5_A1

    glBindTexture(GL_TEXTURE, tex1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0,
	GL_RGBA8, GL_UNSIGNED_BYTE, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE, tex2);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 128, 128, 0,
	GL_RGBA8, GL_UNSIGNED_BYTE, image);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    blend.set_params(width, height, tex1, tex2, 20);
    repsPerSec = blend.run_benchmark(log);

    mpixPerSec = 2 * blend.m_tiles_across * blend.m_tiles_down *
	blend.m_reps / 1e6 *
	128 * 128 * repsPerSec;
    fprintf(log, "Fill rate for GL_RGB5_A1 GL_BLEND without DEPTH_TEST: %f "
	    "Mpixel/sec\n", mpixPerSec);
    fprintf(log, "    Estimated frame rate in this mode: %f frames per sec\n",
	    repsPerSec);

    glDeleteTextures(1, &tex2);
    glDeleteTextures(1, &tex1);

    delete[] image;

#if 0

    How hard is it to kick the screen into different video modes?
	640x480x16
	1024x768x16
	1024x768x32
	1280x1024x16
	1280x1024x32
	1600x1200x32

#endif
}

void bench_do_benchmarks(FILE *log)
{
    bench_write_os_version(log);
    fprintf(log, "\n");
    bench_write_sys_info(log);
    fprintf(log, "\n");
    bench_write_gfx_info(log);
    fprintf(log, "\n");
    bench_write_tex_memory(log);
    fprintf(log, "\n");
    bench_write_teximage_speed(log);
    fprintf(log, "\n");
    bench_write_fill_rate(log);
}

struct enum_mapping {
    unsigned int num;
    char *mapping;
} static enum_mappings[] = {
	{0x0000, "GL_POINTS or GL_ZERO or GL_NONE"},
	{0x0001, "GL_LINES or GL_ONE"},
	{0x0002, "GL_LINE_LOOP"},
	{0x0003, "GL_LINE_STRIP"},
	{0x0004, "GL_TRIANGLES"},
	{0x0005, "GL_TRIANGLE_STRIP"},
	{0x0006, "GL_TRIANGLE_FAN"},
	{0x0007, "GL_QUADS"},
	{0x0008, "GL_QUAD_STRIP"},
	{0x0009, "GL_POLYGON"},
	{0x0100, "GL_ACCUM"},
	{0x0101, "GL_LOAD"},
	{0x0102, "GL_RETURN"},
	{0x0103, "GL_MULT"},
	{0x0104, "GL_ADD"},
	{0x0200, "GL_NEVER"},
	{0x0201, "GL_LESS"},
	{0x0202, "GL_EQUAL"},
	{0x0203, "GL_LEQUAL"},
	{0x0204, "GL_GREATER"},
	{0x0205, "GL_NOTEQUAL"},
	{0x0206, "GL_GEQUAL"},
	{0x0207, "GL_ALWAYS"},
	{0x0300, "GL_SRC_COLOR"},
	{0x0301, "GL_ONE_MINUS_SRC_COLOR"},
	{0x0302, "GL_SRC_ALPHA"},
	{0x0303, "GL_ONE_MINUS_SRC_ALPHA"},
	{0x0304, "GL_DST_ALPHA"},
	{0x0305, "GL_ONE_MINUS_DST_ALPHA"},
	{0x0306, "GL_DST_COLOR"},
	{0x0307, "GL_ONE_MINUS_DST_COLOR"},
	{0x0308, "GL_SRC_ALPHA_SATURATE"},
	{0x0400, "GL_FRONT_LEFT"},
	{0x0401, "GL_FRONT_RIGHT"},
	{0x0402, "GL_BACK_LEFT"},
	{0x0403, "GL_BACK_RIGHT"},
	{0x0404, "GL_FRONT"},
	{0x0405, "GL_BACK"},
	{0x0406, "GL_LEFT"},
	{0x0407, "GL_RIGHT"},
	{0x0408, "GL_FRONT_AND_BACK"},
	{0x0409, "GL_AUX0"},
	{0x040A, "GL_AUX1"},
	{0x040B, "GL_AUX2"},
	{0x040C, "GL_AUX3"},
	{0x0500, "GL_INVALID_ENUM"},
	{0x0501, "GL_INVALID_VALUE"},
	{0x0502, "GL_INVALID_OPERATION"},
	{0x0503, "GL_STACK_OVERFLOW"},
	{0x0504, "GL_STACK_UNDERFLOW"},
	{0x0505, "GL_OUT_OF_MEMORY"},
	{0x0600, "GL_2D"},
	{0x0601, "GL_3D"},
	{0x0602, "GL_3D_COLOR"},
	{0x0603, "GL_3D_COLOR_TEXTURE"},
	{0x0604, "GL_4D_COLOR_TEXTURE"},
	{0x0700, "GL_PASS_THROUGH_TOKEN"},
	{0x0701, "GL_POINT_TOKEN"},
	{0x0702, "GL_LINE_TOKEN"},
	{0x0703, "GL_POLYGON_TOKEN"},
	{0x0704, "GL_BITMAP_TOKEN"},
	{0x0705, "GL_DRAW_PIXEL_TOKEN"},
	{0x0706, "GL_COPY_PIXEL_TOKEN"},
	{0x0707, "GL_LINE_RESET_TOKEN"},
	{0x0800, "GL_EXP"},
	{0x0801, "GL_EXP2"},
	{0x0900, "GL_CW"},
	{0x0901, "GL_CCW"},
	{0x0A00, "GL_COEFF"},
	{0x0A01, "GL_ORDER"},
	{0x0A02, "GL_DOMAIN"},
	{0x0B00, "GL_CURRENT_COLOR"},
	{0x0B01, "GL_CURRENT_INDEX"},
	{0x0B02, "GL_CURRENT_NORMAL"},
	{0x0B03, "GL_CURRENT_TEXTURE_COORDS"},
	{0x0B04, "GL_CURRENT_RASTER_COLOR"},
	{0x0B05, "GL_CURRENT_RASTER_INDEX"},
	{0x0B06, "GL_CURRENT_RASTER_TEXTURE_COORDS"},
	{0x0B07, "GL_CURRENT_RASTER_POSITION"},
	{0x0B08, "GL_CURRENT_RASTER_POSITION_VALID"},
	{0x0B09, "GL_CURRENT_RASTER_DISTANCE"},
	{0x0B10, "GL_POINT_SMOOTH"},
	{0x0B11, "GL_POINT_SIZE"},
	{0x0B12, "GL_POINT_SIZE_RANGE"},
	{0x0B13, "GL_POINT_SIZE_GRANULARITY"},
	{0x0B20, "GL_LINE_SMOOTH"},
	{0x0B21, "GL_LINE_WIDTH"},
	{0x0B22, "GL_LINE_WIDTH_RANGE"},
	{0x0B23, "GL_LINE_WIDTH_GRANULARITY"},
	{0x0B24, "GL_LINE_STIPPLE"},
	{0x0B25, "GL_LINE_STIPPLE_PATTERN"},
	{0x0B26, "GL_LINE_STIPPLE_REPEAT"},
	{0x0B30, "GL_LIST_MODE"},
	{0x0B31, "GL_MAX_LIST_NESTING"},
	{0x0B32, "GL_LIST_BASE"},
	{0x0B33, "GL_LIST_INDEX"},
	{0x0B40, "GL_POLYGON_MODE"},
	{0x0B41, "GL_POLYGON_SMOOTH"},
	{0x0B42, "GL_POLYGON_STIPPLE"},
	{0x0B43, "GL_EDGE_FLAG"},
	{0x0B44, "GL_CULL_FACE"},
	{0x0B45, "GL_CULL_FACE_MODE"},
	{0x0B46, "GL_FRONT_FACE"},
	{0x0B50, "GL_LIGHTING"},
	{0x0B51, "GL_LIGHT_MODEL_LOCAL_VIEWER"},
	{0x0B52, "GL_LIGHT_MODEL_TWO_SIDE"},
	{0x0B53, "GL_LIGHT_MODEL_AMBIENT"},
	{0x0B54, "GL_SHADE_MODEL"},
	{0x0B55, "GL_COLOR_MATERIAL_FACE"},
	{0x0B56, "GL_COLOR_MATERIAL_PARAMETER"},
	{0x0B57, "GL_COLOR_MATERIAL"},
	{0x0B60, "GL_FOG"},
	{0x0B61, "GL_FOG_INDEX"},
	{0x0B62, "GL_FOG_DENSITY"},
	{0x0B63, "GL_FOG_START"},
	{0x0B64, "GL_FOG_END"},
	{0x0B65, "GL_FOG_MODE"},
	{0x0B66, "GL_FOG_COLOR"},
	{0x0B70, "GL_DEPTH_RANGE"},
	{0x0B71, "GL_DEPTH_TEST"},
	{0x0B72, "GL_DEPTH_WRITEMASK"},
	{0x0B73, "GL_DEPTH_CLEAR_VALUE"},
	{0x0B74, "GL_DEPTH_FUNC"},
	{0x0B80, "GL_ACCUM_CLEAR_VALUE"},
	{0x0B90, "GL_STENCIL_TEST"},
	{0x0B91, "GL_STENCIL_CLEAR_VALUE"},
	{0x0B92, "GL_STENCIL_FUNC"},
	{0x0B93, "GL_STENCIL_VALUE_MASK"},
	{0x0B94, "GL_STENCIL_FAIL"},
	{0x0B95, "GL_STENCIL_PASS_DEPTH_FAIL"},
	{0x0B96, "GL_STENCIL_PASS_DEPTH_PASS"},
	{0x0B97, "GL_STENCIL_REF"},
	{0x0B98, "GL_STENCIL_WRITEMASK"},
	{0x0BA0, "GL_MATRIX_MODE"},
	{0x0BA1, "GL_NORMALIZE"},
	{0x0BA2, "GL_VIEWPORT"},
	{0x0BA3, "GL_MODELVIEW_STACK_DEPTH"},
	{0x0BA4, "GL_PROJECTION_STACK_DEPTH"},
	{0x0BA5, "GL_TEXTURE_STACK_DEPTH"},
	{0x0BA6, "GL_MODELVIEW_MATRIX"},
	{0x0BA7, "GL_PROJECTION_MATRIX"},
	{0x0BA8, "GL_TEXTURE_MATRIX"},
	{0x0BB0, "GL_ATTRIB_STACK_DEPTH"},
	{0x0BB1, "GL_CLIENT_ATTRIB_STACK_DEPTH"},
	{0x0BC0, "GL_ALPHA_TEST"},
	{0x0BC1, "GL_ALPHA_TEST_FUNC"},
	{0x0BC2, "GL_ALPHA_TEST_REF"},
	{0x0BD0, "GL_DITHER"},
	{0x0BE0, "GL_BLEND_DST"},
	{0x0BE1, "GL_BLEND_SRC"},
	{0x0BE2, "GL_BLEND"},
	{0x0BF0, "GL_LOGIC_OP_MODE"},
	{0x0BF1, "GL_INDEX_LOGIC_OP"},
	{0x0BF1, "GL_LOGIC_OP"},
	{0x0BF2, "GL_COLOR_LOGIC_OP"},
	{0x0C00, "GL_AUX_BUFFERS"},
	{0x0C01, "GL_DRAW_BUFFER"},
	{0x0C02, "GL_READ_BUFFER"},
	{0x0C10, "GL_SCISSOR_BOX"},
	{0x0C11, "GL_SCISSOR_TEST"},
	{0x0C20, "GL_INDEX_CLEAR_VALUE"},
	{0x0C21, "GL_INDEX_WRITEMASK"},
	{0x0C22, "GL_COLOR_CLEAR_VALUE"},
	{0x0C23, "GL_COLOR_WRITEMASK"},
	{0x0C30, "GL_INDEX_MODE"},
	{0x0C31, "GL_RGBA_MODE"},
	{0x0C32, "GL_DOUBLEBUFFER"},
	{0x0C33, "GL_STEREO"},
	{0x0C40, "GL_RENDER_MODE"},
	{0x0C50, "GL_PERSPECTIVE_CORRECTION_HINT"},
	{0x0C51, "GL_POINT_SMOOTH_HINT"},
	{0x0C52, "GL_LINE_SMOOTH_HINT"},
	{0x0C53, "GL_POLYGON_SMOOTH_HINT"},
	{0x0C54, "GL_FOG_HINT"},
	{0x0C60, "GL_TEXTURE_GEN_S"},
	{0x0C61, "GL_TEXTURE_GEN_T"},
	{0x0C62, "GL_TEXTURE_GEN_R"},
	{0x0C63, "GL_TEXTURE_GEN_Q"},
	{0x0C70, "GL_PIXEL_MAP_I_TO_I"},
	{0x0C71, "GL_PIXEL_MAP_S_TO_S"},
	{0x0C72, "GL_PIXEL_MAP_I_TO_R"},
	{0x0C73, "GL_PIXEL_MAP_I_TO_G"},
	{0x0C74, "GL_PIXEL_MAP_I_TO_B"},
	{0x0C75, "GL_PIXEL_MAP_I_TO_A"},
	{0x0C76, "GL_PIXEL_MAP_R_TO_R"},
	{0x0C77, "GL_PIXEL_MAP_G_TO_G"},
	{0x0C78, "GL_PIXEL_MAP_B_TO_B"},
	{0x0C79, "GL_PIXEL_MAP_A_TO_A"},
	{0x0CB0, "GL_PIXEL_MAP_I_TO_I_SIZE"},
	{0x0CB1, "GL_PIXEL_MAP_S_TO_S_SIZE"},
	{0x0CB2, "GL_PIXEL_MAP_I_TO_R_SIZE"},
	{0x0CB3, "GL_PIXEL_MAP_I_TO_G_SIZE"},
	{0x0CB4, "GL_PIXEL_MAP_I_TO_B_SIZE"},
	{0x0CB5, "GL_PIXEL_MAP_I_TO_A_SIZE"},
	{0x0CB6, "GL_PIXEL_MAP_R_TO_R_SIZE"},
	{0x0CB7, "GL_PIXEL_MAP_G_TO_G_SIZE"},
	{0x0CB8, "GL_PIXEL_MAP_B_TO_B_SIZE"},
	{0x0CB9, "GL_PIXEL_MAP_A_TO_A_SIZE"},
	{0x0CF0, "GL_UNPACK_SWAP_BYTES"},
	{0x0CF1, "GL_UNPACK_LSB_FIRST"},
	{0x0CF2, "GL_UNPACK_ROW_LENGTH"},
	{0x0CF3, "GL_UNPACK_SKIP_ROWS"},
	{0x0CF4, "GL_UNPACK_SKIP_PIXELS"},
	{0x0CF5, "GL_UNPACK_ALIGNMENT"},
	{0x0D00, "GL_PACK_SWAP_BYTES"},
	{0x0D01, "GL_PACK_LSB_FIRST"},
	{0x0D02, "GL_PACK_ROW_LENGTH"},
	{0x0D03, "GL_PACK_SKIP_ROWS"},
	{0x0D04, "GL_PACK_SKIP_PIXELS"},
	{0x0D05, "GL_PACK_ALIGNMENT"},
	{0x0D10, "GL_MAP_COLOR"},
	{0x0D11, "GL_MAP_STENCIL"},
	{0x0D12, "GL_INDEX_SHIFT"},
	{0x0D13, "GL_INDEX_OFFSET"},
	{0x0D14, "GL_RED_SCALE"},
	{0x0D15, "GL_RED_BIAS"},
	{0x0D16, "GL_ZOOM_X"},
	{0x0D17, "GL_ZOOM_Y"},
	{0x0D18, "GL_GREEN_SCALE"},
	{0x0D19, "GL_GREEN_BIAS"},
	{0x0D1A, "GL_BLUE_SCALE"},
	{0x0D1B, "GL_BLUE_BIAS"},
	{0x0D1C, "GL_ALPHA_SCALE"},
	{0x0D1D, "GL_ALPHA_BIAS"},
	{0x0D1E, "GL_DEPTH_SCALE"},
	{0x0D1F, "GL_DEPTH_BIAS"},
	{0x0D30, "GL_MAX_EVAL_ORDER"},
	{0x0D31, "GL_MAX_LIGHTS"},
	{0x0D32, "GL_MAX_CLIP_PLANES"},
	{0x0D33, "GL_MAX_TEXTURE_SIZE"},
	{0x0D34, "GL_MAX_PIXEL_MAP_TABLE"},
	{0x0D35, "GL_MAX_ATTRIB_STACK_DEPTH"},
	{0x0D36, "GL_MAX_MODELVIEW_STACK_DEPTH"},
	{0x0D37, "GL_MAX_NAME_STACK_DEPTH"},
	{0x0D38, "GL_MAX_PROJECTION_STACK_DEPTH"},
	{0x0D39, "GL_MAX_TEXTURE_STACK_DEPTH"},
	{0x0D3A, "GL_MAX_VIEWPORT_DIMS"},
	{0x0D3B, "GL_MAX_CLIENT_ATTRIB_STACK_DEPTH"},
	{0x0D50, "GL_SUBPIXEL_BITS"},
	{0x0D51, "GL_INDEX_BITS"},
	{0x0D52, "GL_RED_BITS"},
	{0x0D53, "GL_GREEN_BITS"},
	{0x0D54, "GL_BLUE_BITS"},
	{0x0D55, "GL_ALPHA_BITS"},
	{0x0D56, "GL_DEPTH_BITS"},
	{0x0D57, "GL_STENCIL_BITS"},
	{0x0D58, "GL_ACCUM_RED_BITS"},
	{0x0D59, "GL_ACCUM_GREEN_BITS"},
	{0x0D5A, "GL_ACCUM_BLUE_BITS"},
	{0x0D5B, "GL_ACCUM_ALPHA_BITS"},
	{0x0D70, "GL_NAME_STACK_DEPTH"},
	{0x0D80, "GL_AUTO_NORMAL"},
	{0x0D90, "GL_MAP1_COLOR_4"},
	{0x0D91, "GL_MAP1_INDEX"},
	{0x0D92, "GL_MAP1_NORMAL"},
	{0x0D93, "GL_MAP1_TEXTURE_COORD_1"},
	{0x0D94, "GL_MAP1_TEXTURE_COORD_2"},
	{0x0D95, "GL_MAP1_TEXTURE_COORD_3"},
	{0x0D96, "GL_MAP1_TEXTURE_COORD_4"},
	{0x0D97, "GL_MAP1_VERTEX_3"},
	{0x0D98, "GL_MAP1_VERTEX_4"},
	{0x0DB0, "GL_MAP2_COLOR_4"},
	{0x0DB1, "GL_MAP2_INDEX"},
	{0x0DB2, "GL_MAP2_NORMAL"},
	{0x0DB3, "GL_MAP2_TEXTURE_COORD_1"},
	{0x0DB4, "GL_MAP2_TEXTURE_COORD_2"},
	{0x0DB5, "GL_MAP2_TEXTURE_COORD_3"},
	{0x0DB6, "GL_MAP2_TEXTURE_COORD_4"},
	{0x0DB7, "GL_MAP2_VERTEX_3"},
	{0x0DB8, "GL_MAP2_VERTEX_4"},
	{0x0DD0, "GL_MAP1_GRID_DOMAIN"},
	{0x0DD1, "GL_MAP1_GRID_SEGMENTS"},
	{0x0DD2, "GL_MAP2_GRID_DOMAIN"},
	{0x0DD3, "GL_MAP2_GRID_SEGMENTS"},
	{0x0DE0, "GL_TEXTURE_1D"},
	{0x0DE1, "GL_TEXTURE_2D"},
	{0x0DF0, "GL_FEEDBACK_BUFFER_POINTER"},
	{0x0DF1, "GL_FEEDBACK_BUFFER_SIZE"},
	{0x0DF2, "GL_FEEDBACK_BUFFER_TYPE"},
	{0x0DF3, "GL_SELECTION_BUFFER_POINTER"},
	{0x0DF4, "GL_SELECTION_BUFFER_SIZE"},
	{0x1000, "GL_TEXTURE_WIDTH"},
	{0x1001, "GL_TEXTURE_HEIGHT"},
	{0x1003, "GL_TEXTURE_COMPONENTS"},
	{0x1003, "GL_TEXTURE_INTERNAL_FORMAT"},
	{0x1004, "GL_TEXTURE_BORDER_COLOR"},
	{0x1005, "GL_TEXTURE_BORDER"},
	{0x1100, "GL_DONT_CARE"},
	{0x1101, "GL_FASTEST"},
	{0x1102, "GL_NICEST"},
	{0x1200, "GL_AMBIENT"},
	{0x1201, "GL_DIFFUSE"},
	{0x1202, "GL_SPECULAR"},
	{0x1203, "GL_POSITION"},
	{0x1204, "GL_SPOT_DIRECTION"},
	{0x1205, "GL_SPOT_EXPONENT"},
	{0x1206, "GL_SPOT_CUTOFF"},
	{0x1207, "GL_CONSTANT_ATTENUATION"},
	{0x1208, "GL_LINEAR_ATTENUATION"},
	{0x1209, "GL_QUADRATIC_ATTENUATION"},
	{0x1300, "GL_COMPILE"},
	{0x1301, "GL_COMPILE_AND_EXECUTE"},
	{0x1400, "GL_BYTE"},
	{0x1401, "GL_UNSIGNED_BYTE"},
	{0x1402, "GL_SHORT"},
	{0x1403, "GL_UNSIGNED_SHORT"},
	{0x1404, "GL_INT"},
	{0x1405, "GL_UNSIGNED_INT"},
	{0x1406, "GL_FLOAT"},
	{0x1407, "GL_2_BYTES"},
	{0x1408, "GL_3_BYTES"},
	{0x1409, "GL_4_BYTES"},
	{0x140A, "GL_DOUBLE"},
	{0x1500, "GL_CLEAR"},
	{0x1501, "GL_AND"},
	{0x1502, "GL_AND_REVERSE"},
	{0x1503, "GL_COPY"},
	{0x1504, "GL_AND_INVERTED"},
	{0x1505, "GL_NOOP"},
	{0x1506, "GL_XOR"},
	{0x1507, "GL_OR"},
	{0x1508, "GL_NOR"},
	{0x1509, "GL_EQUIV"},
	{0x150A, "GL_INVERT"},
	{0x150B, "GL_OR_REVERSE"},
	{0x150C, "GL_COPY_INVERTED"},
	{0x150D, "GL_OR_INVERTED"},
	{0x150E, "GL_NAND"},
	{0x150F, "GL_SET"},
	{0x1600, "GL_EMISSION"},
	{0x1601, "GL_SHININESS"},
	{0x1602, "GL_AMBIENT_AND_DIFFUSE"},
	{0x1603, "GL_COLOR_INDEXES"},
	{0x1700, "GL_MODELVIEW"},
	{0x1701, "GL_PROJECTION"},
	{0x1702, "GL_TEXTURE"},
	{0x1800, "GL_COLOR"},
	{0x1801, "GL_DEPTH"},
	{0x1802, "GL_STENCIL"},
	{0x1900, "GL_COLOR_INDEX"},
	{0x1901, "GL_STENCIL_INDEX"},
	{0x1902, "GL_DEPTH_COMPONENT"},
	{0x1903, "GL_RED"},
	{0x1904, "GL_GREEN"},
	{0x1905, "GL_BLUE"},
	{0x1906, "GL_ALPHA"},
	{0x1907, "GL_RGB"},
	{0x1908, "GL_RGBA"},
	{0x1909, "GL_LUMINANCE"},
	{0x190A, "GL_LUMINANCE_ALPHA"},
	{0x1A00, "GL_BITMAP"},
	{0x1B00, "GL_POINT"},
	{0x1B01, "GL_LINE"},
	{0x1B02, "GL_FILL"},
	{0x1C00, "GL_RENDER"},
	{0x1C01, "GL_FEEDBACK"},
	{0x1C02, "GL_SELECT"},
	{0x1D00, "GL_FLAT"},
	{0x1D01, "GL_SMOOTH"},
	{0x1E00, "GL_KEEP"},
	{0x1E01, "GL_REPLACE"},
	{0x1E02, "GL_INCR"},
	{0x1E03, "GL_DECR"},
	{0x1F00, "GL_VENDOR"},
	{0x1F01, "GL_RENDERER"},
	{0x1F02, "GL_VERSION"},
	{0x1F03, "GL_EXTENSIONS"},
	{0x2000, "GL_S"},
	{0x2001, "GL_T"},
	{0x2002, "GL_R"},
	{0x2003, "GL_Q"},
	{0x2100, "GL_MODULATE"},
	{0x2101, "GL_DECAL"},
	{0x2200, "GL_TEXTURE_ENV_MODE"},
	{0x2201, "GL_TEXTURE_ENV_COLOR"},
	{0x2300, "GL_TEXTURE_ENV"},
	{0x2400, "GL_EYE_LINEAR"},
	{0x2401, "GL_OBJECT_LINEAR"},
	{0x2402, "GL_SPHERE_MAP"},
	{0x2500, "GL_TEXTURE_GEN_MODE"},
	{0x2501, "GL_OBJECT_PLANE"},
	{0x2502, "GL_EYE_PLANE"},
	{0x2600, "GL_NEAREST"},
	{0x2601, "GL_LINEAR"},
	{0x2700, "GL_NEAREST_MIPMAP_NEAREST"},
	{0x2701, "GL_LINEAR_MIPMAP_NEAREST"},
	{0x2702, "GL_NEAREST_MIPMAP_LINEAR"},
	{0x2703, "GL_LINEAR_MIPMAP_LINEAR"},
	{0x2800, "GL_TEXTURE_MAG_FILTER"},
	{0x2801, "GL_TEXTURE_MIN_FILTER"},
	{0x2802, "GL_TEXTURE_WRAP_S"},
	{0x2803, "GL_TEXTURE_WRAP_T"},
	{0x2900, "GL_CLAMP"},
	{0x2901, "GL_REPEAT"},
	{0x2A00, "GL_POLYGON_OFFSET_UNITS"},
	{0x2A01, "GL_POLYGON_OFFSET_POINT"},
	{0x2A02, "GL_POLYGON_OFFSET_LINE"},
	{0x2A10, "GL_R3_G3_B2"},
	{0x2A20, "GL_V2F"},
	{0x2A21, "GL_V3F"},
	{0x2A22, "GL_C4UB_V2F"},
	{0x2A23, "GL_C4UB_V3F"},
	{0x2A24, "GL_C3F_V3F"},
	{0x2A25, "GL_N3F_V3F"},
	{0x2A26, "GL_C4F_N3F_V3F"},
	{0x2A27, "GL_T2F_V3F"},
	{0x2A28, "GL_T4F_V4F"},
	{0x2A29, "GL_T2F_C4UB_V3F"},
	{0x2A2A, "GL_T2F_C3F_V3F"},
	{0x2A2B, "GL_T2F_N3F_V3F"},
	{0x2A2C, "GL_T2F_C4F_N3F_V3F"},
	{0x2A2D, "GL_T4F_C4F_N3F_V4F"},
	{0x3000, "GL_CLIP_PLANE0"},
	{0x3001, "GL_CLIP_PLANE1"},
	{0x3002, "GL_CLIP_PLANE2"},
	{0x3003, "GL_CLIP_PLANE3"},
	{0x3004, "GL_CLIP_PLANE4"},
	{0x3005, "GL_CLIP_PLANE5"},
	{0x4000, "GL_LIGHT0"},
	{0x4001, "GL_LIGHT1"},
	{0x4002, "GL_LIGHT2"},
	{0x4003, "GL_LIGHT3"},
	{0x4004, "GL_LIGHT4"},
	{0x4005, "GL_LIGHT5"},
	{0x4006, "GL_LIGHT6"},
	{0x4007, "GL_LIGHT7"},
	{0x8000, "GL_ABGR_EXT"},
	{0x8001, "GL_CONSTANT_COLOR"},
	{0x8001, "GL_CONSTANT_COLOR_EXT"},
	{0x8002, "GL_ONE_MINUS_CONSTANT_COLOR"},
	{0x8002, "GL_ONE_MINUS_CONSTANT_COLOR_EXT"},
	{0x8003, "GL_CONSTANT_ALPHA"},
	{0x8003, "GL_CONSTANT_ALPHA_EXT"},
	{0x8004, "GL_ONE_MINUS_CONSTANT_ALPHA"},
	{0x8004, "GL_ONE_MINUS_CONSTANT_ALPHA_EXT"},
	{0x8005, "GL_BLEND_COLOR_EXT"},
	{0x8006, "GL_FUNC_ADD_EXT"},
	{0x8007, "GL_MIN_EXT"},
	{0x8008, "GL_MAX_EXT"},
	{0x8009, "GL_BLEND_EQUATION_EXT"},
	{0x800A, "GL_FUNC_SUBTRACT_EXT"},
	{0x800B, "GL_FUNC_REVERSE_SUBTRACT_EXT"},
	{0x8031, "GL_TABLE_TOO_LARGE_EXT"},
	{0x8032, "GL_UNSIGNED_BYTE_3_3_2"},
	{0x8033, "GL_UNSIGNED_SHORT_4_4_4_4"},
	{0x8034, "GL_UNSIGNED_SHORT_5_5_5_1"},
	{0x8035, "GL_UNSIGNED_INT_8_8_8_8"},
	{0x8036, "GL_UNSIGNED_INT_10_10_10_2"},
	{0x8037, "GL_POLYGON_OFFSET_EXT"},
	{0x8037, "GL_POLYGON_OFFSET_FILL"},
	{0x8038, "GL_POLYGON_OFFSET_FACTOR"},
	{0x8038, "GL_POLYGON_OFFSET_FACTOR_EXT"},
	{0x8039, "GL_POLYGON_OFFSET_BIAS_EXT"},
	{0x803A, "GL_RESCALE_NORMAL"},
	{0x803A, "GL_RESCALE_NORMAL_EXT"},
	{0x803B, "GL_ALPHA4"},
	{0x803C, "GL_ALPHA8"},
	{0x803D, "GL_ALPHA12"},
	{0x803E, "GL_ALPHA16"},
	{0x803F, "GL_LUMINANCE4"},
	{0x8040, "GL_LUMINANCE8"},
	{0x8041, "GL_LUMINANCE12"},
	{0x8042, "GL_LUMINANCE16"},
	{0x8043, "GL_LUMINANCE4_ALPHA4"},
	{0x8044, "GL_LUMINANCE6_ALPHA2"},
	{0x8045, "GL_LUMINANCE8_ALPHA8"},
	{0x8046, "GL_LUMINANCE12_ALPHA4"},
	{0x8047, "GL_LUMINANCE12_ALPHA12"},
	{0x8048, "GL_LUMINANCE16_ALPHA16"},
	{0x8049, "GL_INTENSITY"},
	{0x804A, "GL_INTENSITY4"},
	{0x804B, "GL_INTENSITY8"},
	{0x804C, "GL_INTENSITY12"},
	{0x804D, "GL_INTENSITY16"},
	{0x804F, "GL_RGB4"},
	{0x8050, "GL_RGB5"},
	{0x8051, "GL_RGB8"},
	{0x8052, "GL_RGB10"},
	{0x8053, "GL_RGB12"},
	{0x8054, "GL_RGB16"},
	{0x8055, "GL_RGBA2"},
	{0x8056, "GL_RGBA4"},
	{0x8057, "GL_RGB5_A1"},
	{0x8058, "GL_RGBA8"},
	{0x8059, "GL_RGB10_A2"},
	{0x805A, "GL_RGBA12"},
	{0x805B, "GL_RGBA16"},
	{0x805C, "GL_TEXTURE_RED_SIZE"},
	{0x805D, "GL_TEXTURE_GREEN_SIZE"},
	{0x805E, "GL_TEXTURE_BLUE_SIZE"},
	{0x805F, "GL_TEXTURE_ALPHA_SIZE"},
	{0x8060, "GL_TEXTURE_LUMINANCE_SIZE"},
	{0x8061, "GL_TEXTURE_INTENSITY_SIZE"},
	{0x8063, "GL_PROXY_TEXTURE_1D"},
	{0x8064, "GL_PROXY_TEXTURE_2D"},
	{0x8066, "GL_TEXTURE_PRIORITY"},
	{0x8066, "GL_TEXTURE_PRIORITY_EXT"},
	{0x8067, "GL_TEXTURE_RESIDENT"},
	{0x8067, "GL_TEXTURE_RESIDENT_EXT"},
	{0x8068, "GL_TEXTURE_1D_BINDING_EXT"},
	{0x8068, "GL_TEXTURE_BINDING_1D"},
	{0x8069, "GL_TEXTURE_2D_BINDING_EXT"},
	{0x8069, "GL_TEXTURE_BINDING_2D"},
	{0x806A, "GL_TEXTURE_3D_BINDING_EXT"},
	{0x806A, "GL_TEXTURE_BINDING_3D"},
	{0x806B, "GL_PACK_SKIP_IMAGES"},
	{0x806B, "GL_PACK_SKIP_IMAGES_EXT"},
	{0x806C, "GL_PACK_IMAGE_HEIGHT"},
	{0x806C, "GL_PACK_IMAGE_HEIGHT_EXT"},
	{0x806D, "GL_UNPACK_SKIP_IMAGES"},
	{0x806D, "GL_UNPACK_SKIP_IMAGES_EXT"},
	{0x806E, "GL_UNPACK_IMAGE_HEIGHT"},
	{0x806E, "GL_UNPACK_IMAGE_HEIGHT_EXT"},
	{0x806F, "GL_TEXTURE_3D"},
	{0x806F, "GL_TEXTURE_3D_EXT"},
	{0x8070, "GL_PROXY_TEXTURE_3D"},
	{0x8070, "GL_PROXY_TEXTURE_3D_EXT"},
	{0x8071, "GL_TEXTURE_DEPTH"},
	{0x8071, "GL_TEXTURE_DEPTH_EXT"},
	{0x8072, "GL_TEXTURE_WRAP_R"},
	{0x8072, "GL_TEXTURE_WRAP_R_EXT"},
	{0x8073, "GL_MAX_3D_TEXTURE_SIZE"},
	{0x8073, "GL_MAX_3D_TEXTURE_SIZE_EXT"},
	{0x8074, "GL_VERTEX_ARRAY"},
	{0x8074, "GL_VERTEX_ARRAY_EXT"},
	{0x8075, "GL_NORMAL_ARRAY"},
	{0x8075, "GL_NORMAL_ARRAY_EXT"},
	{0x8076, "GL_COLOR_ARRAY"},
	{0x8076, "GL_COLOR_ARRAY_EXT"},
	{0x8077, "GL_INDEX_ARRAY"},
	{0x8077, "GL_INDEX_ARRAY_EXT"},
	{0x8078, "GL_TEXTURE_COORD_ARRAY"},
	{0x8078, "GL_TEXTURE_COORD_ARRAY_EXT"},
	{0x8079, "GL_EDGE_FLAG_ARRAY"},
	{0x8079, "GL_EDGE_FLAG_ARRAY_EXT"},
	{0x807A, "GL_VERTEX_ARRAY_SIZE"},
	{0x807A, "GL_VERTEX_ARRAY_SIZE_EXT"},
	{0x807B, "GL_VERTEX_ARRAY_TYPE"},
	{0x807B, "GL_VERTEX_ARRAY_TYPE_EXT"},
	{0x807C, "GL_VERTEX_ARRAY_STRIDE"},
	{0x807C, "GL_VERTEX_ARRAY_STRIDE_EXT"},
	{0x807D, "GL_VERTEX_ARRAY_COUNT_EXT"},
	{0x807E, "GL_NORMAL_ARRAY_TYPE"},
	{0x807E, "GL_NORMAL_ARRAY_TYPE_EXT"},
	{0x807F, "GL_NORMAL_ARRAY_STRIDE"},
	{0x807F, "GL_NORMAL_ARRAY_STRIDE_EXT"},
	{0x8080, "GL_NORMAL_ARRAY_COUNT_EXT"},
	{0x8081, "GL_COLOR_ARRAY_SIZE"},
	{0x8081, "GL_COLOR_ARRAY_SIZE_EXT"},
	{0x8082, "GL_COLOR_ARRAY_TYPE"},
	{0x8082, "GL_COLOR_ARRAY_TYPE_EXT"},
	{0x8083, "GL_COLOR_ARRAY_STRIDE"},
	{0x8083, "GL_COLOR_ARRAY_STRIDE_EXT"},
	{0x8084, "GL_COLOR_ARRAY_COUNT_EXT"},
	{0x8085, "GL_INDEX_ARRAY_TYPE"},
	{0x8085, "GL_INDEX_ARRAY_TYPE_EXT"},
	{0x8086, "GL_INDEX_ARRAY_STRIDE"},
	{0x8086, "GL_INDEX_ARRAY_STRIDE_EXT"},
	{0x8087, "GL_INDEX_ARRAY_COUNT_EXT"},
	{0x8088, "GL_TEXTURE_COORD_ARRAY_SIZE"},
	{0x8088, "GL_TEXTURE_COORD_ARRAY_SIZE_EXT"},
	{0x8089, "GL_TEXTURE_COORD_ARRAY_TYPE"},
	{0x8089, "GL_TEXTURE_COORD_ARRAY_TYPE_EXT"},
	{0x808A, "GL_TEXTURE_COORD_ARRAY_STRIDE"},
	{0x808A, "GL_TEXTURE_COORD_ARRAY_STRIDE_EXT"},
	{0x808B, "GL_TEXTURE_COORD_ARRAY_COUNT_EXT"},
	{0x808C, "GL_EDGE_FLAG_ARRAY_STRIDE"},
	{0x808C, "GL_EDGE_FLAG_ARRAY_STRIDE_EXT"},
	{0x808D, "GL_EDGE_FLAG_ARRAY_COUNT_EXT"},
	{0x808E, "GL_VERTEX_ARRAY_POINTER"},
	{0x808E, "GL_VERTEX_ARRAY_POINTER_EXT"},
	{0x808F, "GL_NORMAL_ARRAY_POINTER"},
	{0x808F, "GL_NORMAL_ARRAY_POINTER_EXT"},
	{0x8090, "GL_COLOR_ARRAY_POINTER"},
	{0x8090, "GL_COLOR_ARRAY_POINTER_EXT"},
	{0x8091, "GL_INDEX_ARRAY_POINTER"},
	{0x8091, "GL_INDEX_ARRAY_POINTER_EXT"},
	{0x8092, "GL_TEXTURE_COORD_ARRAY_POINTER"},
	{0x8092, "GL_TEXTURE_COORD_ARRAY_POINTER_EXT"},
	{0x8093, "GL_EDGE_FLAG_ARRAY_POINTER"},
	{0x8093, "GL_EDGE_FLAG_ARRAY_POINTER_EXT"},
	{0x80D8, "GL_COLOR_TABLE_FORMAT_EXT"},
	{0x80D9, "GL_COLOR_TABLE_WIDTH_EXT"},
	{0x80DA, "GL_COLOR_TABLE_RED_SIZE_EXT"},
	{0x80DB, "GL_COLOR_TABLE_GREEN_SIZE_EXT"},
	{0x80DC, "GL_COLOR_TABLE_BLUE_SIZE_EXT"},
	{0x80DD, "GL_COLOR_TABLE_ALPHA_SIZE_EXT"},
	{0x80DE, "GL_COLOR_TABLE_LUMINANCE_SIZE_EXT"},
	{0x80DF, "GL_COLOR_TABLE_INTENSITY_SIZE_EXT"},
	{0x80E0, "GL_BGR"},
	{0x80E1, "GL_BGRA"},
	{0x80E2, "GL_COLOR_INDEX1_EXT"},
	{0x80E3, "GL_COLOR_INDEX2_EXT"},
	{0x80E4, "GL_COLOR_INDEX4_EXT"},
	{0x80E5, "GL_COLOR_INDEX8_EXT"},
	{0x80E6, "GL_COLOR_INDEX12_EXT"},
	{0x80E7, "GL_COLOR_INDEX16_EXT"},
	{0x80ED, "GL_TEXTURE_INDEX_SIZE_EXT"},
	{0x8126, "GL_POINT_SIZE_MIN_EXT"},
	{0x8127, "GL_POINT_SIZE_MAX_EXT"},
	{0x8128, "GL_POINT_FADE_THRESHOLD_SIZE_EXT"},
	{0x8129, "GL_DISTANCE_ATTENUATION_EXT"},
	{0x812F, "GL_CLAMP_TO_EDGE"},
	{0x812F, "GL_CLAMP_TO_EDGE_SGIS"},
	{0x813A, "GL_TEXTURE_MIN_LOD"},
	{0x813B, "GL_TEXTURE_MAX_LOD"},
	{0x813C, "GL_TEXTURE_BASE_LEVEL"},
	{0x813D, "GL_TEXTURE_MAX_LEVEL"},
	{0x81F8, "GL_LIGHT_MODEL_COLOR_CONTROL"},
	{0x81F9, "GL_SINGLE_COLOR"},
	{0x81FA, "GL_SEPARATE_SPECULAR_COLOR"},
	{0x81FB, "GL_SHARED_TEXTURE_PALETTE_EXT"},
	{0x835C, "GL_SELECTED_TEXTURE_SGIS"},
	{0x835D, "GL_SELECTED_TEXTURE_COORD_SET_SGIS"},
	{0x835E, "GL_MAX_TEXTURES_SGIS"},
	/* {0x835F, "GL_TEXTURE0_SGIS"}, */
	/* {0x8360, "GL_TEXTURE1_SGIS"}, */
	/* {0x8361, "GL_TEXTURE2_SGIS"}, */
	/* {0x8362, "GL_TEXTURE3_SGIS"}, */
	{0x8362, "GL_UNSIGNED_BYTE_2_3_3_REV"},
	/* {0x8363, "GL_TEXTURE_COORD_SET_SOURCE_SGIS"}, */
	{0x8363, "GL_UNSIGNED_SHORT_5_6_5"},
	{0x8364, "GL_UNSIGNED_SHORT_5_6_5_REV"},
	{0x8365, "GL_UNSIGNED_SHORT_4_4_4_4_REV"},
	{0x8366, "GL_UNSIGNED_SHORT_1_5_5_5_REV"},
	{0x8367, "GL_UNSIGNED_INT_8_8_8_8_REV"},
	{0x8368, "GL_UNSIGNED_INT_2_10_10_10_REV"},
	{0x83C0, "GL_SELECTED_TEXTURE_EXT"},
	{0x83C1, "GL_SELECTED_TEXTURE_COORD_SET_EXT"},
	{0x83C2, "GL_SELECTED_TEXTURE_TRANSFORM_EXT"},
	{0x83C3, "GL_MAX_TEXTURES_EXT"},
	{0x83C4, "GL_MAX_TEXTURE_COORD_SETS_EXT"},
	{0x83C5, "GL_TEXTURE_ENV_COORD_SET_EXT"},
	{0x83C6, "GL_TEXTURE0_EXT"},
	{0x83C7, "GL_TEXTURE1_EXT"},
	{0x83C8, "GL_TEXTURE2_EXT"},
	{0x83C9, "GL_TEXTURE3_EXT"},
	{0xF0E8, "GL_MAX_ELEMENTS_VERTICES"},
	{0xF0E9, "GL_MAX_ELEMENTS_INDICES"},
};

static char *map_GL_enum_to_string(unsigned int num)
{
    int i;
    static char unknown[512];

    for(i = 0; i < sizeof(enum_mappings) / sizeof(enum_mappings[0]); i++)
	if(enum_mappings[i].num == num)
	    return enum_mappings[i].mapping;

    sprintf(unknown, "Unknown GL enum %04X", num);

    return unknown;
}