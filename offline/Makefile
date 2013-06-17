#CPPFLAGS= -O3 -I../libs
CPPFLAGS= -g -O0 -I../libs

#LDFLAGS = -lpthread
LDFLAGS = -g -O0 -lpthread

all: \
	convert-2mass.exe \
	convert-2mrs.exe \
	convert-6dfgs.exe \
	genvolume.exe \
	getstats.exe \
	gettotalstats.exe \
	genoctree.exe \
	show.exe \
	show-still.exe \
	genisos.exe \
	mark-clusters.exe \
	flatten-clusters.exe

convert-2mass.o: convert-2mass.cpp
	g++ $(CPPFLAGS) -c $^ -o $@ 

convert-2mrs.o: convert-2mrs.cpp
	g++ $(CPPFLAGS) -c $^ -o $@

convert-6dfgs.o: convert-6dfgs.cpp
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
	g++ -m32 $(CPPFLAGS) -I../SDL-1.2.15/include -c $^ -o $@

show-still.o: show-still.cpp
	g++ -m32 $(CPPFLAGS) -I../SDL-1.2.15/include -I../SDL_image-1.2.12/include -c $^ -o $@


convert-2mass.exe: convert-2mass.o util.o
	g++ $^ $(LDFLAGS) -o $@

convert-2mrs.exe : convert-2mrs.o util.o
	g++ $^ $(LDFLAGS) -o $@

convert-6dfgs.exe : convert-6dfgs.o util.o
	g++ $^ $(LDFLAGS) -o $@

genvolume.exe: 	genvolume.o stat.o util.o
	g++ $^ $(LDFLAGS) -o $@

getstats.exe: getstats.o stat.o util.o
	g++ $^ $(LDFLAGS) -o $@

gettotalstats.exe: gettotalstats.o stat.o util.o
	g++ $^ $(LDFLAGS) -o $@

genoctree.exe: genoctree.o octree.o stat.o util.o
	g++ $^ $(LDFLAGS) -o $@

genisos.exe: genisos.o util.o stat.o
	g++ $^ $(LDFLAGS) -o $@

mark-clusters.exe: mark-clusters.o util.o stat.o octree.o
	g++ $^ $(LDFLAGS) -o $@

flatten-clusters.exe: flatten-clusters.o util.o stat.o octree.o
	g++ $^ $(LDFLAGS) -o $@

show.exe: show.o stat.o octree.o util.o
	g++ -m32 -O3 $^ -o $@ $(LDFLAGS) -lmingw32 -lwinmm -lgdi32 -lSDLmain -lSDL.dll -lopengl32 -lglew32

show-still.exe: show-still.o stat.o util.o writebmp.o
	g++ -O3 $^ -o $@ 

clean:
	-rm *.o
	-rm *.exe


