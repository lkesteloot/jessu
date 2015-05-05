##!nmake

!include <win32.mak>

libcmtd = libcmtd.lib oldnames.lib
guilibsmtd  = $(libcmtd) $(winlibs)

OPENGL = glu32.lib opengl32.lib
GLUT = "c:/PROGRA~1/devstudio/vc/include/GL"
D3D = c:\dxsdk
D3D_INCLUDE = $(D3D)\include
D3D_LIBS = /libpath:$(D3D)\lib d3d8.lib d3dx8.lib

CFILES	=	
CPPFILES  =	jessu.cpp fileread.cpp loaddir.cpp scaletile.cpp config.cpp \
		geteventname.cpp key.cpp text.cpp graphics.cpp
		# benchmark.cpp
TARGET	=	SSJessu.scr
JESSU_LIMIT = 	jessu_limit.jpg

OBJECTS	= $(CPPFILES:.cpp=.obj) $(CFILES:.c=.obj) jessu.res
# LCFLAGS	= /nologo $(cflags) $(cdebug) -I../util -I$(GLUT) -I$(D3D_INCLUDE) -DWIN32
# LLDLIBS = $(D3D_LIBS) libjpeg.lib $(lflags) \
# $(ldebug) $(OPENGL) $(guilibsmtd) winmm.lib comctl32.lib shell32.lib
#
LLDLIBS	= $(D3D_LIBS) libjpeg.lib $(lflags) $(ldebug) $(guilibsmtd) \
	winmm.lib comctl32.lib shell32.lib

COMMONCPPFLAGS = -GX /nologo /I$(D3D_INCLUDE) /W4

# CPPFLAGS = /Zi /DDEBUG=1 $(COMMONCPPFLAGS)
CPPFLAGS = /Ox /DDEBUG=0 $(COMMONCPPFLAGS)

# comment this out for release:
ldebug = 

default	: $(TARGET)

clean	:
	@del *.obj

clobber	: clean
	@del *.exe

$(TARGET): $(OBJECTS) Makefile
	$(link) -out:$@ $(OBJECTS) $(LLDLIBS)

exe: $(OBJECTS) Makefile
	$(link) -out:SSJessu.exe $(OBJECTS) $(LLDLIBS)

install: $(TARGET)
	copy /Y $(TARGET) c:\windows\system32
	copy /Y $(JESSU_LIMIT) "C:\Program Files\HeadCode\Jessu"

jessu.res: jessu.rc resource.h
	rc jessu.rc

make_key.exe: make_key.obj key.obj
	cl make_key.obj key.obj /Femake_key

checkx.exe: checkx.obj
	$(link) -out:$@ checkx.obj $(lflags) $(ldebug) libcmtd.lib $(winlibs)

.c.obj	: 
	$(CC) $(LCFLAGS) $<

fileread.obj: scaletile.h jessu.h fileread.h

scaletile.obj: scaletile.h jessu.h

jessu.obj: resource.h fileread.h loaddir.h scaletile.h config.h \
	benchmark.h jessu.h text.hpp

text.obj: text.hpp

benchmark.obj: benchmark.h

config.obj: config.h jessu.h resource.h geteventname.h

geteventname.obj: geteventname.h

loaddir.obj: loaddir.h jessu.h config.h

make_key.obj: key.h

key.obj: key.h

text.hpp: jessu.h
