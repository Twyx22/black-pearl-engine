CXX = i686-w64-mingw32-g++
CXXFLAGS = -O2 -s -static-libgcc -static-libstdc++
LDFLAGS = -ld3d9 -ld3dx9 -lwinmm

all: d3d9.dll

d3d9.dll: src/d3d9_proxy.c
	$(CXX) -shared $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f d3d9.dll bpe.log

install:
	cp d3d9.dll "/mnt/HDD/Games/LEGO Pirates of the Caribbean - The Video Game/"

.PHONY: all clean install
