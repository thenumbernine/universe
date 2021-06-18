# https://stackoverflow.com/a/30906085
# NMAKE code here \
!ifndef 0 # \
 # \
CC = cl.exe # \
MV = move # \
RM = del # \
CP = copy # \
# \
INCFLAG = /I # \
DEPS = $** # \
 # \
OBJEXT = .obj # \
BINEXT = .exe # \
 # \
OUTOBJFLAG = /Fo: # \
OUTBINFLAG = /Fe: # \
 # \
CFITSINCDIR=$(USERPROFILE)\include # \
CFITSLIBDIR=$(USERPROFILE)\bin\x64 # \
CFITSLIB=$(CFITSLIBDIR)\cfitsio.lib # \
 # \
CPPFLAGS=/EHsc /c /O2 /std:c++17 /D_USE_MATH_DEFINES # \
LDFLAGS= # \
 # \
!else
# Make code here

CC = g++
MV = mv -f
RM = rm -f
CP = cp -f

INCFLAG = -I
DEPS = $^

OBJEXT = .o
BINEXT =

OUTOBJFLAG = -o
OUTBINFLAG = -o

CFITSINCDIR=../libs
CFITSLIB= -lcfitsio
OPENGLLIB=-lmingw32 -lwinmm -lgdi32 -lSDLmain -lSDL.dll -lopengl32 -lglew32

SDLINCDIR=../SDL-1.2.15/include
SDLIMAGEINCDIR=../SDL_image-1.2.12/include

# release:
CPPFLAGS= -O3 -fPIC -c
LDFLAGS = -fPIC

# debugging:
#CPPFLAGS= -g -O0 -fPIC -c
#LDFLAGS = -g -O0 -fPIC

# \
!endif


ALL =	\
	convert-2mrs$(BINEXT) \
	convert-6dfgs$(BINEXT) \
	convert-sdss$(BINEXT) \
	convert-gaia$(BINEXT) \
	getstats$(BINEXT) \
	gettotalstats$(BINEXT) \
	genvolume$(BINEXT) \
# not building in msvc yet
#	genoctree$(BINEXT) \
#	show-still$(BINEXT) \
#	genisos$(BINEXT) \
#	flatten-clusters$(BINEXT) \
#	mark-clusters$(BINEXT) \
#	convert-2mass$(BINEXT) \
# not done at all:
#	show$(BINEXT) \

all: $(ALL)

convert-2mass$(OBJEXT): convert-2mass.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

convert-2mrs$(OBJEXT): convert-2mrs.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

convert-6dfgs$(OBJEXT): convert-6dfgs.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

convert-sdss$(OBJEXT): convert-sdss.cpp
	$(CC) $(CPPFLAGS) $(INCFLAG)$(CFITSINCDIR) $(DEPS) $(OUTOBJFLAG) $@

convert-gaia$(OBJEXT): convert-gaia.cpp
	$(CC) $(CPPFLAGS) $(INCFLAG)$(CFITSINCDIR) $(DEPS) $(OUTOBJFLAG) $@

util$(OBJEXT): util.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

fits-util$(OBJEXT): fits-util.cpp
	$(CC) $(CPPFLAGS) $(INCFLAG)$(CFITSINCDIR) $(DEPS) $(OUTOBJFLAG) $@

stat$(OBJEXT): stat.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

octree$(OBJEXT): octree.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

writebmp$(OBJEXT): writebmp.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

getstats$(OBJEXT): getstats.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

gettotalstats$(OBJEXT): gettotalstats.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

genvolume$(OBJEXT): genvolume.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

genoctree$(OBJEXT): genoctree.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

genisos$(OBJEXT): genisos.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

mark-clusters$(OBJEXT): mark-clusters.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

flatten-clusters$(OBJEXT): flatten-clusters.cpp
	$(CC) $(CPPFLAGS) $(DEPS) $(OUTOBJFLAG) $@

show$(OBJEXT): show.cpp
	$(CC) $(CPPFLAGS) $(INCFLAG)$(SDLINCDIR) $(DEPS) $(OUTOBJFLAG) $@

show-still$(OBJEXT): show-still.cpp
	$(CC) $(CPPFLAGS) $(INCFLAG)$(SDLINCDIR) $(INCFLAG)$(SDLIMAGEINCDIR) $(DEPS) $(OUTOBJFLAG) $@


convert-2mass$(BINEXT): convert-2mass$(OBJEXT) util$(OBJEXT)
	$(CC) $(LDFLAGS) $(DEPS) $(OUTBINFLAG) $@

convert-2mrs$(BINEXT): convert-2mrs$(OBJEXT) util$(OBJEXT) stat$(OBJEXT)
	$(CC) $(LDFLAGS) $(DEPS) $(OUTBINFLAG) $@

convert-6dfgs$(BINEXT): convert-6dfgs$(OBJEXT) util$(OBJEXT)
	$(CC) $(LDFLAGS) $(DEPS) $(OUTBINFLAG) $@

convert-sdss$(BINEXT): convert-sdss$(OBJEXT) stat$(OBJEXT) util$(OBJEXT) fits-util$(OBJEXT)
	$(CC) $(LDFLAGS) $(DEPS) $(CFITSLIB) $(OUTBINFLAG) $@

convert-gaia$(BINEXT): convert-gaia$(OBJEXT) stat$(OBJEXT) util$(OBJEXT) fits-util$(OBJEXT)
	$(CC) $(LDFLAGS) $(DEPS) $(CFITSLIB) $(OUTBINFLAG) $@

getstats$(BINEXT): getstats$(OBJEXT) stat$(OBJEXT) util$(OBJEXT)
	$(CC) $(DEPS) $(LDFLAGS) $(OUTBINFLAG) $@

gettotalstats$(BINEXT): gettotalstats$(OBJEXT) stat$(OBJEXT) util$(OBJEXT)
	$(CC) $(DEPS) $(LDFLAGS) $(OUTBINFLAG) $@

genvolume$(BINEXT): genvolume$(OBJEXT) stat$(OBJEXT) util$(OBJEXT)
	$(CC) $(DEPS) $(LDFLAGS) $(OUTBINFLAG) $@

genoctree$(BINEXT): genoctree$(OBJEXT) octree$(OBJEXT) stat$(OBJEXT) util$(OBJEXT)
	$(CC) $(DEPS) $(LDFLAGS) $(OUTBINFLAG) $@

genisos$(BINEXT): genisos$(OBJEXT) util$(OBJEXT) stat$(OBJEXT)
	$(CC) $(DEPS) $(LDFLAGS) $(OUTBINFLAG) $@

mark-clusters$(BINEXT): mark-clusters$(OBJEXT) util$(OBJEXT) stat$(OBJEXT) octree$(OBJEXT)
	$(CC) $(DEPS) $(LDFLAGS) $(OUTBINFLAG) $@

flatten-clusters$(BINEXT): flatten-clusters$(OBJEXT) util$(OBJEXT) stat$(OBJEXT) octree$(OBJEXT)
	$(CC) $(DEPS) $(LDFLAGS) $(OUTBINFLAG) $@

show$(BINEXT): show$(OBJEXT) stat$(OBJEXT) octree$(OBJEXT) util$(OBJEXT)
	$(CC) $(DEPS) $(OUTBINFLAG) $@ $(LDFLAGS) $(OPENGLLIB)

show-still$(BINEXT): show-still$(OBJEXT) stat$(OBJEXT) util$(OBJEXT) writebmp$(OBJEXT)
	$(CC) $(DEPS) $(OUTBINFLAG) $@


.PHONY: clean
clean:
	-$(RM) *$(OBJEXT)
	-$(RM) $(ALL)


# idk what this was for.  did I start this makefile on android?
# doesn't work in nmake anyways
#.PHONY: install
#install:
#	$(foreach FILE, $(ALL), $(shell cp $(FILE) /data/local/bin/))
