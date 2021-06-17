#CPPFLAGS= -O3 -I../libs -fPIC
CPPFLAGS= -g -O0 -I../libs -fPIC -c

#LDFLAGS = -lpthread -fPIC
LDFLAGS = -g -O0 -lpthread -fPIC

CC = g++
RM = rm

OBJ = .o
BIN = 

OUTOBJFLAG = -o
OUTBINFLAG = -o

ALL =	\
	convert-2mass \
	convert-2mrs \
	convert-6dfgs \
	convert-sdss \
	convert-gaia \
	genvolume \
	getstats \
	gettotalstats \
	genoctree \
	show-still \
	genisos	\
	flatten-clusters	\
	mark-clusters
#	show

all: $(ALL)

convert-2mass$(OBJ): convert-2mass.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@ 

convert-2mrs$(OBJ): convert-2mrs.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

convert-6dfgs$(OBJ): convert-6dfgs.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@ 

convert-sdss$(OBJ): convert-sdss.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

convert-gaia$(OBJ): convert-gaia.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

util$(OBJ): util.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

stat$(OBJ): stat.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

octree$(OBJ): octree.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

writebmp$(OBJ): writebmp.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

genvolume$(OBJ): genvolume.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

genoctree$(OBJ): genoctree.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

genisos$(OBJ): genisos.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

mark-clusters$(OBJ): mark-clusters.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

flatten-clusters$(OBJ): flatten-clusters.cpp
	$(CC) $(CPPFLAGS) $^ $(OUTOBJFLAG) $@

show$(OBJ): show.cpp
	$(CC)  $(CPPFLAGS) -I../SDL-1.2.15/include $^ $(OUTOBJFLAG) $@

show-still$(OBJ): show-still.cpp
	$(CC)  $(CPPFLAGS) -I../SDL-1.2.15/include -I../SDL_image-1.2.12/include $^ $(OUTOBJFLAG) $@


convert-2mass: convert-2mass$(OBJ) util$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

convert-2mrs: convert-2mrs$(OBJ) util$(OBJ) stat$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

convert-6dfgs: convert-6dfgs$(OBJ) util$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

convert-sdss: convert-sdss$(OBJ) stat$(OBJ) util$(OBJ)
	$(CC) $^ $(LDFLAGS) -lcfitsio $(OUTBINFLAG) $@

convert-gaia: convert-gaia$(OBJ) stat$(OBJ) util$(OBJ)
	$(CC) $^ $(LDFLAGS) -lcfitsio $(OUTBINFLAG) $@

genvolume: 	genvolume$(OBJ) stat$(OBJ) util$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

getstats: getstats$(OBJ) stat$(OBJ) util$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

gettotalstats: gettotalstats$(OBJ) stat$(OBJ) util$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

genoctree: genoctree$(OBJ) octree$(OBJ) stat$(OBJ) util$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

genisos: genisos$(OBJ) util$(OBJ) stat$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

mark-clusters: mark-clusters$(OBJ) util$(OBJ) stat$(OBJ) octree$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

flatten-clusters: flatten-clusters$(OBJ) util$(OBJ) stat$(OBJ) octree$(OBJ)
	$(CC) $^ $(LDFLAGS) $(OUTBINFLAG) $@

show: show$(OBJ) stat$(OBJ) octree$(OBJ) util$(OBJ)
	$(CC)  -O3 $^ $(OUTBINFLAG) $@ $(LDFLAGS) -lmingw32 -lwinmm -lgdi32 -lSDLmain -lSDL.dll -lopengl32 -lglew32

show-still: show-still$(OBJ) stat$(OBJ) util$(OBJ) writebmp$(OBJ)
	$(CC) -O3 $^ $(OUTBINFLAG) $@ 

clean:
	-$(RM) *$(OBJ)
	-$(RM) $(ALL)

install:
	$(foreach FILE, $(ALL), $(shell cp $(FILE) /data/local/bin/))
