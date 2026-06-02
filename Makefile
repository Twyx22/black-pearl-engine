CXX = i686-w64-mingw32-g++
CXXFLAGS = -O2 -s -static-libgcc -static-libstdc++
LDFLAGS = -Wl,--enable-stdcall-fixup -ld3dx9 -lwinmm -lpsapi
INCLUDES = -Isrc -Ilib/minhook/include

SRCS = src/d3d9_proxy.c src/utils.c src/cheats.c src/menu.c src/hooks.c src/input.c src/config_loader.c
MINHOOK_SRCS = lib/minhook/src/buffer.c lib/minhook/src/hook.c lib/minhook/src/trampoline.c lib/minhook/src/hde/hde32.c
ALL_SRCS = $(SRCS) $(MINHOOK_SRCS)
OBJS = $(ALL_SRCS:.c=.o)

all: d3d9.dll

d3d9.dll: $(OBJS) src/d3d9_proxy.def
	$(CXX) -shared $(CXXFLAGS) -o $@ $(OBJS) src/d3d9_proxy.def $(LDFLAGS)

src/%.o: src/%.c
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

lib/minhook/src/%.o: lib/minhook/src/%.c
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

lib/minhook/src/hde/%.o: lib/minhook/src/hde/%.c
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

clean:
	rm -f $(OBJS) d3d9.dll bpe.log

install:
	cp d3d9.dll "/mnt/HDD/Games/LEGO Pirates of the Caribbean - The Video Game/"

.PHONY: all clean install
