CXX = i686-w64-mingw32-g++
CXXFLAGS = -O2 -s -static-libgcc -static-libstdc++
LDFLAGS = -Wl,--enable-stdcall-fixup -ld3dx9 -lwinmm -lpsapi

SRCS = src/d3d9_proxy.c src/utils.c src/cheats.c src/menu.c src/hooks.c src/input.c src/config_loader.c
OBJS = $(SRCS:.c=.o)

all: d3d9.dll

d3d9.dll: $(OBJS) src/d3d9_proxy.def
	$(CXX) -shared $(CXXFLAGS) -o $@ $(OBJS) src/d3d9_proxy.def $(LDFLAGS)

src/%.o: src/%.c
	$(CXX) -c $(CXXFLAGS) -Isrc -o $@ $<

clean:
	rm -f $(OBJS) d3d9.dll bpe.log

install:
	cp d3d9.dll "/mnt/HDD/Games/LEGO Pirates of the Caribbean - The Video Game/"

.PHONY: all clean install
