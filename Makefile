CXX = i686-w64-mingw32-g++
CXXFLAGS = -O2 -s -static-libgcc -static-libstdc++
LDFLAGS = -Wl,--enable-stdcall-fixup -ld3dx9 -lwinmm

all: d3d9.dll

d3d9.dll: src/d3d9_proxy.c src/d3d9_proxy.def
	$(CXX) -shared $(CXXFLAGS) -o $@ $< src/d3d9_proxy.def $(LDFLAGS)

clean:
	rm -f d3d9.dll bpe.log

install:
	cp d3d9.dll "/mnt/HDD/Games/LEGO Pirates of the Caribbean - The Video Game/"

.PHONY: all clean install
