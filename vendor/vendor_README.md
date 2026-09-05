# Third-party dependencies

The build expects these exact folders (not included — pull them in yourself,
since license/redistribution terms belong to each project):

```
vendor/
├── imgui/                     <- Dear ImGui v1.90.9 (docking branch)
│   ├── imgui.h / imgui.cpp
│   ├── imgui_draw.cpp / imgui_tables.cpp / imgui_widgets.cpp / imgui_demo.cpp
│   ├── imgui_internal.h, imstb_*.h
│   └── backends/
│       ├── imgui_impl_glfw.h / .cpp
│       └── imgui_impl_vulkan.h / .cpp
├── glfw/                      <- GLFW 3.4, mingw-w64 pre-built package
│   ├── include/GLFW/glfw3.h, glfw3native.h
│   └── lib-mingw-w64/libglfw3.a
├── tinyobjloader/              <- single header, tag v2.0.0rc13
│   └── tiny_obj_loader.h
└── stb/                        <- single headers
    ├── stb_image.h
    └── stb_image_write.h
```

Get them here:
- ImGui (docking branch): https://github.com/ocornut/imgui/tree/docking
- GLFW mingw-w64 package: https://www.glfw.org/download.html
- tinyobjloader: https://github.com/tinyobjloader/tinyobjloader
- stb: https://github.com/nothings/stb

Vulkan SDK 1.3.290.0 installs separately (not vendored) — the Makefile reads
it from `VULKAN_SDK` (defaults to `C:/VulkanSDK/1.3.290.0`).

## Why these versions
- **ImGui 1.90.9 docking** — the `docking` branch is required for the
  dockspace / IDE-style layout (`ImGui::DockSpaceOverViewport`). The
  non-docking `master` branch does NOT have this API.
- **GLFW 3.4** — stable Vulkan-surface creation helpers
  (`glfwCreateWindowSurface`, `glfwGetRequiredInstanceExtensions`).
- **tinyobjloader** — used for the OBJ + MTL parser (Core Feature 1).
- **stb_image / stb_image_write** — texture pixel loading + PNG export
  (Core Features 1 and 5).
