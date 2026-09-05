# TinyShaderEngine v1.0

C++17 / GCC (MinGW64, Windows x64) / Vulkan 1.3.290.0 / Dear ImGui (docking).

## Build

1. Install the Vulkan SDK **1.3.290.0** (default path `C:\VulkanSDK\1.3.290.0`,
   or set the `VULKAN_SDK` env var / pass `VULKAN_SDK=...` to make).
2. Populate `vendor/` exactly as described in `vendor/README.md` (ImGui
   docking branch, GLFW mingw-w64 package, tinyobjloader, stb, ufbx).
3. From a MinGW64 shell:
   ```
   mingw32-make
   bin\TSE.exe
   ```

`resources/TSE.ico` is a generated placeholder icon — replace it with your
real icon (keep the filename) and the `.rc` file already wires it into the
`.exe` (taskbar + Explorer icon + version info).

## Controls

- **K** — toggle realtime preview.
- Click the **Viewport** first to focus it, then:
  - **WASD + Q/E** — fly-move the camera (forward/back/strafe/up/down).
  - **Arrow keys** or **Right-Drag** — rotate the camera's look direction (yaw/pitch).
- **Left-click / drag node markers** directly in the viewport: blue **C** = camera,
  orange **L** = light, green **M** = a loaded object's origin.
- Fine-grained values (FOV, speeds, light color/intensity/aim/cone) live in
  the **Scene** panel. Per-object rotation (dragging only moves; rotation
  needs numeric input) lives in the **Model Loader** panel, per model.

## What's implemented vs. scaffolded

**Core features (real, working logic):**
1. **Model Loader** — native Win32 file picker, `.obj`/`.mtl` via
   tinyobjloader, `.fbx` via ufbx (see caveat below), texture pixel data
   loaded via stb_image. `src/scene/Model.cpp`.
2. **Default floor** — a shaded ground plane is always present in the GPU
   render and shadow pass. Grid guides are editor-only and do not enter final
   output. `Scene` / `SceneRenderer` / `ShadowMap`.
3. **Draggable Camera/Light/Object nodes** — blue `C` / orange `L` / green
   `M` markers, XYZ drag via mouse; camera additionally supports free-fly
   (WASD/arrows) and per-object rotation via the Model Loader panel.
   `SceneNode` / `ViewportPanel::handleNodeDragging` / `ViewportPanel::handleCameraFlight`.
4. **Realtime toggle + FPS** — `K` key, smoothed FPS counter overlay.
   `Application::handleGlobalInput`.
5. **HQ Render & Export** — resolution picker, scanline-based progress bar,
   PNG export via stb_image_write. `RenderExportPanel`. Note: still uses its
   own original CPU rasterizer (grid + wireframe), independent of the GPU
   pipeline described below — it was never updated to capture the real-time
   GPU composite. Making it read back `PostComposite`'s output instead would
   make preview and export fully consistent; worth doing next if that matters.

**Object transforms (move + rotate):** every loaded `Model` has its own
position (drag the green `M` marker, or type into the Model Loader panel)
and Euler rotation (Model Loader panel sliders). The transform is applied
consistently everywhere geometry is used: the CPU wireframe overlay, the HQ
export rasterizer, the real GPU shading pass, and the shadow-map depth pass.

**Camera & Light (move, rotate/aim, configure):** the camera supports free
flight (WASD move, arrow-key look) in addition to marker-dragging, with FOV
and speed exposed in the Scene panel. The light can be dragged, and — new —
aimed as a spotlight (yaw/pitch + cone angle, Scene panel), with a real soft
cone falloff computed in `scene.frag`; toggling "Spotlight" off reverts to
the original omnidirectional point light.

**FBX loading — the one part of this response that's meaningfully riskier
than everything else:** `Model::LoadFromFBX` (via the `ufbx` library) was
written against ufbx's documented API from reference knowledge, not
compiled or tested against the real library — unlike every other
integration in this project (Vulkan, tinyobjloader, stb, GLFW, ImGui), all
of which follow stable, well-established patterns I have high confidence
in. ufbx's exact struct field names have shifted across versions
historically. If it doesn't compile cleanly, that's the first place to
look — see `vendor/README.md` for specifics on what's most/least likely to
have drifted, and share the exact compiler error if you hit one.

**Important scope note:** actual GPU-shaded rendering of loaded meshes
(vertex/fragment shaders, textured/lit triangles via a real Vulkan graphics
pipeline) **is implemented** — see `src/render/SceneRenderer.*` and
`shaders/scene.{vert,frag}`. It does real-time shading with a Lambertian
model + hardware-PCF shadow sampling from ShadowMap + soft spotlight cone
falloff from the Light node's aim. The full pipeline per frame is:

```
ShadowMap (light-space depth, includes per-object transform)
  -> SceneRenderer (Lambertian shading + shadow + spotlight cone -> color + linear depth)
    -> Bloom (reads scene color)
    -> DepthOfField (reads scene color + linear depth)
      -> ColorCorrect (reads DoF's output, or scene color if DoF is off)
        -> GodRays (reads linear depth, radiates from the Light node's screen position)
        -> PostComposite (base + Bloom + GodRays, additive) -> shown in Viewport via ImGui::Image
```

OBJ/FBX diffuse textures are decoded by stb_image, uploaded to SRGB Vulkan
images, and sampled per submesh in `scene.frag`. Materials without a texture
use an internal white fallback texture and retain their diffuse color.

**All 5 deferred effects have real Vulkan implementations:**
ShadowMap, Bloom, DepthOfField, ColorCorrect, and GodRays. See each one's
own header comment in `src/render/effects/` for its specific technique
(hardware-PCF shadow mapping, dual-filter bloom, CoC-based DoF, standard
exposure/contrast/saturation grading, and screen-space radial light shafts).


## Known Issues / Testing Status

> **Warning:** This project is still in an early development state and has **many known and unknown bugs**.

The program **works and can be built and launched**, and the main functionality described above is implemented, but it has **not been thoroughly tested** across different hardware, drivers, Vulkan SDK configurations, models, textures, resolutions, and usage scenarios.

You should expect things such as:
- crashes or freezes in some situations;
- rendering artifacts or incorrect visual results;
- features behaving inconsistently depending on the scene or imported model;
- problems with certain OBJ/FBX files, materials, or textures;
- Vulkan/driver-specific issues;
- UI or input glitches;
- export results not always matching the realtime viewport;
- untested edge cases and unfinished parts of the application.

In short: **it works, but it is not production-ready.** Use it as a development build and expect bugs. If you encounter an issue, please include the steps to reproduce it, the model/scene that caused it (if applicable), your GPU/driver, Vulkan SDK version, and the exact compiler/runtime error or log output.

### Testing

Testing has currently been limited and mostly focused on verifying that the main features work during development. There has **not** been a comprehensive QA pass or systematic stress testing.

The project should therefore be considered **experimental / work in progress** rather than a stable release.

## Layout

```
TinyShaderEngine/
├── Makefile
├── resources/          TSE.ico, TSE.rc, resource.h
├── vendor/              third-party libs (see vendor/README.md)
└── src/
    ├── main.cpp
    ├── core/            Application, VulkanContext, Math
    ├── scene/           Scene, Node (Camera/Light), Model (OBJ/MTL loader)
    ├── ui/               UIManager + dockspace
    │   └── panels/       ViewportPanel, ModelLoaderPanel,
    │                     RenderExportPanel, EffectsStubPanel
    └── render/effects/   EffectStubs.h (ShadowMap/DoF/ColorCorrect/Bloom/GodRays)
```
