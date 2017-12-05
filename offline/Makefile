#CPPFLAGS= -O3 -I../libs -fPIC
CPPFLAGS= -g -O0 -I../libs -fPIC

#LDFLAGS = -lpthread -fPIC
LDFLAGS = -g -O0 -lpthread -fPIC

ALL =	\
	convert-2mass \
	convert-2mrs \
	convert-6dfgs \
	convert-sdss3 \
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

convert-2mass.o: convert-2mass.cpp
	g++ $(CPPFLAGS) -c $^ -o $@ 

convert-2mrs.o: convert-2mrs.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

convert-6dfgs.o: convert-6dfgs.cpp
	g++ $(CPPFLAGS) -c $^ -o $@ 

convert-sdss3.o: convert-sdss3.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

convert-gaia.o: convert-gaia.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

util.o: util.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

stat.o: stat.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

octree.o: octree.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

writebmp.o: writebmp.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

genvolume.o: genvolume.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

genoctree.o: genoctree.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

genisos.o: genisos.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

mark-clusters.o: mark-clusters.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

flatten-clusters.o: flatten-clusters.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

show.o: show.cpp
	g++  $(CPPFLAGS) -I../SDL-1.2.15/include -c $^ -o $@

show-still.o: show-still.cpp
	g++  $(CPPFLAGS) -I../SDL-1.2.15/include -I../SDL_image-1.2.12/include -c $^ -o $@


convert-2mass: convert-2mass.o util.o
	g++ $^ $(LDFLAGS) -o $@

convert-2mrs: convert-2mrs.o util.o stat.o
	g++ $^ $(LDFLAGS) -o $@

convert-6dfgs: convert-6dfgs.o util.o
	g++ $^ $(LDFLAGS) -o $@

convert-sdss3: convert-sdss3.o util.o
	g++ $^ $(LDFLAGS) -lcfitsio -o $@

convert-gaia: convert-gaia.o stat.o util.o
	g++ $^ $(LDFLAGS) -lcfitsio -o $@

genvolume: 	genvolume.o stat.o util.o
	g++ $^ $(LDFLAGS) -o $@

getstats: getstats.o stat.o util.o
	g++ $^ $(LDFLAGS) -o $@

gettotalstats: gettotalstats.o stat.o util.o
	g++ $^ $(LDFLAGS) -o $@

genoctree: genoctree.o octree.o stat.o util.o
	g++ $^ $(LDFLAGS) -o $@

genisos: genisos.o util.o stat.o
	g++ $^ $(LDFLAGS) -o $@

mark-clusters: mark-clusters.o util.o stat.o octree.o
	g++ $^ $(LDFLAGS) -o $@

flatten-clusters: flatten-clusters.o util.o stat.o octree.o
	g++ $^ $(LDFLAGS) -o $@

show: show.o stat.o octree.o util.o
	g++  -O3 $^ -o $@ $(LDFLAGS) -lmingw32 -lwinmm -lgdi32 -lSDLmain -lSDL.dll -lopengl32 -lglew32

show-still: show-still.o stat.o util.o writebmp.o
	g++ -O3 $^ -o $@ 

clean:
	-rm *.o
	-rm $(ALL)

install:
	$(foreach FILE, $(ALL), $(shell cp $(FILE) /data/local/bin/))

