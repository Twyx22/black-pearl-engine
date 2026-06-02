CXX = i686-w64-mingw32-g++
CXXFLAGS = -O2 -s -static-libgcc -static-libstdc++ -DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD
LDFLAGS = -Wl,--enable-stdcall-fixup -static -ld3dx9 -lwinmm -lpsapi -ld3d9 -ldinput8 -ldxguid -lgdi32 -ldwmapi
INCLUDES = -Isrc -Ilib/minhook/include -Ilib/imgui -Ilib/imgui/backends

SRCS = src/d3d9_proxy.c src/utils.c src/cheats.c src/menu.c src/hooks.c src/input.c src/config_loader.c
MINHOOK_SRCS = lib/minhook/src/buffer.c lib/minhook/src/hook.c lib/minhook/src/trampoline.c lib/minhook/src/hde/hde32.c
IMGUI_SRCS = lib/imgui/imgui.cpp lib/imgui/imgui_draw.cpp lib/imgui/imgui_tables.cpp lib/imgui/imgui_widgets.cpp lib/imgui/backends/imgui_impl_win32.cpp lib/imgui/backends/imgui_impl_dx9.cpp
ALL_SRCS = $(SRCS) $(MINHOOK_SRCS) $(IMGUI_SRCS)
OBJS = $(ALL_SRCS:.c=.o)
OBJS := $(OBJS:.cpp=.o)

all: d3d9.dll

d3d9.dll: $(OBJS) src/d3d9_proxy.def
	$(CXX) -shared $(CXXFLAGS) -o $@ $(OBJS) src/d3d9_proxy.def $(LDFLAGS)

src/%.o: src/%.c
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

lib/minhook/src/%.o: lib/minhook/src/%.c
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

lib/minhook/src/hde/%.o: lib/minhook/src/hde/%.c
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

lib/imgui/%.o: lib/imgui/%.cpp
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

lib/imgui/backends/%.o: lib/imgui/backends/%.cpp
	$(CXX) -c $(CXXFLAGS) $(INCLUDES) -o $@ $<

clean:
	rm -f $(OBJS) d3d9.dll bpe.log

GAME_DIR = /c/GOG Games/LEGO Pirates of the Caribbean - The Video Game

install: d3d9.dll
	cp d3d9.dll "$(GAME_DIR)/"

run: install
	"$(GAME_DIR)/LEGOPirates.exe"

.PHONY: all clean install run
