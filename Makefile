# ==========================================================================
# TinyShaderEngine v1.0 - Makefile (MinGW64 / GCC, Windows x64)
# ==========================================================================
# Requires:
#   - Vulkan SDK 1.3.290.0  (set VULKAN_SDK env var, e.g. C:\VulkanSDK\1.3.290.0)
#   - GLFW3 (mingw64 static/dev libs)  -> vendor/glfw
#   - Dear ImGui 1.90.9 (docking branch) -> vendor/imgui
#   - tinyobjloader (single header)     -> vendor/tinyobjloader
#   - stb_image / stb_image_write       -> vendor/stb
#   - ufbx (ufbx.h + ufbx.c pair)        -> vendor/ufbx  [.fbx loading]
#
# Build:   mingw32-make
# Run:     bin\TSE.exe
# Clean:   mingw32-make clean
# ==========================================================================

APP_NAME    := TSE
BUILD_DIR   := build
BIN_DIR     := bin

# ---- Vulkan SDK -----------------------------------------------------------
VULKAN_SDK  ?= C:/VulkanSDK/1.3.290.0
VK_INC      := $(VULKAN_SDK)/Include
VK_LIB      := $(VULKAN_SDK)/Lib

# ---- Vendor paths -----------------------------------------------------------
VENDOR      := vendor
IMGUI_DIR   := $(VENDOR)/imgui
GLFW_DIR    := $(VENDOR)/glfw
TOBJ_DIR    := $(VENDOR)/tinyobjloader
STB_DIR     := $(VENDOR)/stb
UFBX_DIR    := $(VENDOR)/ufbx

# ---- Compiler / flags -------------------------------------------------------
CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Wno-unused-parameter -mwindows -MMD -MP \
            -Wno-missing-field-initializers -Wno-array-bounds
# -Wno-missing-field-initializers: our Vk*CreateInfo{ TYPE } idiom deliberately
#   sets sType and zero-inits the rest via brace-init; that's correct, not a bug.
# -Wno-array-bounds: silences known false positives from vendored ImGui internals
#   on newer GCC (e.g. DockNodeTreeUpdatePosSize, IO.MouseDown[] indexing) — not
#   our code, not fixable here without patching ImGui itself.
# Deliberately empty: do NOT define IMGUI_IMPL_VULKAN_NO_PROTOTYPES here.
# ImGui's Vulkan backend checks with #ifdef, so even "=0" still counts as
# defined and forces the no-prototypes code path, which then requires an
# explicit ImGui_ImplVulkan_LoadFunctions() call we never make -> the
# "g_FunctionsLoaded" assertion at startup. Leaving it undefined links
# directly against vulkan-1.lib (which we already do via -lvulkan-1),
# matching the normal/default backend path.
DEFINES  :=

INCLUDES := -Isrc \
            -I$(VK_INC) \
            -I$(IMGUI_DIR) \
            -I$(IMGUI_DIR)/backends \
            -I$(GLFW_DIR)/include \
            -I$(TOBJ_DIR) \
            -I$(STB_DIR) \
            -I$(UFBX_DIR)

LIBDIRS  := -L$(VK_LIB) -L$(GLFW_DIR)/lib-mingw-w64
LIBS     := -lglfw3 -lvulkan-1 -lgdi32 -luser32 -lkernel32 -lshell32 -lole32

# ---- Sources -----------------------------------------------------------------
APP_SRC := $(shell find src -name '*.cpp')
IMGUI_SRC := \
    $(IMGUI_DIR)/imgui.cpp \
    $(IMGUI_DIR)/imgui_draw.cpp \
    $(IMGUI_DIR)/imgui_tables.cpp \
    $(IMGUI_DIR)/imgui_widgets.cpp \
    $(IMGUI_DIR)/imgui_demo.cpp \
    $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
    $(IMGUI_DIR)/backends/imgui_impl_vulkan.cpp

ALL_SRC := $(APP_SRC) $(IMGUI_SRC)
OBJS    := $(patsubst %.cpp,$(BUILD_DIR)/%.o,$(ALL_SRC))
DEPS    := $(OBJS:.o=.d)

# ufbx (.fbx loading) ships as a .h/.c pair rather than a single header, so
# it's compiled as its own translation unit (as C, not included into a .cpp
# like tinyobjloader/stb are) and linked in alongside everything else.
UFBX_SRC := $(UFBX_DIR)/ufbx.c
UFBX_OBJ := $(BUILD_DIR)/vendor/ufbx/ufbx.o

RC_FILE  := resources/TSE.rc
RC_OBJ   := $(BUILD_DIR)/TSE_rc.o

# ---- Shaders (Shadow / Scene / Bloom / DoF / ColorCorrect / GodRays / Composite) --
GLSLC      := $(VULKAN_SDK)/Bin/glslc.exe
SHADER_SRCS := shaders/shadow.vert shaders/fullscreen.vert \
               shaders/scene.vert shaders/scene.frag \
               shaders/bloom_downsample.frag shaders/bloom_upsample.frag \
               shaders/dof.frag shaders/colorcorrect.frag \
               shaders/godrays.frag shaders/composite.frag
SHADER_SPVS := $(SHADER_SRCS:%=%.spv)

TARGET   := $(BIN_DIR)/$(APP_NAME).exe

.PHONY: all clean dirs
all: dirs $(SHADER_SPVS) $(TARGET)

dirs:
	@mkdir -p $(BIN_DIR)

%.spv: %
	$(GLSLC) $< -o $@

$(TARGET): $(OBJS) $(UFBX_OBJ) $(RC_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(UFBX_OBJ) $(RC_OBJ) $(LIBDIRS) $(LIBS)
	@echo "Built $(TARGET)"
	@echo "Note: run from the project root (or copy shaders/ next to the exe) so shaders/*.spv resolve at runtime."

$(BUILD_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -c $< -o $@

$(UFBX_OBJ): $(UFBX_SRC)
	@mkdir -p $(dir $@)
	gcc -O2 -c $(UFBX_SRC) -o $(UFBX_OBJ) -I$(UFBX_DIR)

$(RC_OBJ): $(RC_FILE)
	@mkdir -p $(dir $@)
	windres $(RC_FILE) -O coff -o $(RC_OBJ) -Iresources

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR)

# Keep object files ABI-compatible with their headers. This is especially
# important for Scene/Model changes, whose member offsets are used across
# translation units.
-include $(DEPS)
