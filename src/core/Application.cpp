#include "Application.h"
#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"
#include <GLFW/glfw3.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <cstdio>

namespace {
// The executable is normally placed in <project>/bin while shaders stay in
// <project>/shaders. Explorer starts a double-clicked executable with `bin`
// as the current directory, which previously made every relative shader load
// fail and left the viewport empty. Resolve the project root once at startup
// instead of relying on how the program was launched.
void SetAssetWorkingDirectory() {
    char modulePath[MAX_PATH]{};
    DWORD length = GetModuleFileNameA(nullptr, modulePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return;
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path executable(modulePath);
    fs::path executableDir = executable.parent_path();
    fs::path candidates[] = { fs::current_path(ec), executableDir, executableDir.parent_path() };
    for (const fs::path& candidate : candidates) {
        if (!candidate.empty() && fs::is_directory(candidate / "shaders", ec)) {
            fs::current_path(candidate, ec);
            return;
        }
    }
}

// Dark, IDE-style theme (English UI per spec — no localization strings used).
void ApplyDarkIdeTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    style.WindowRounding = 4.0f;
    style.FrameRounding  = 3.0f;
    style.GrabRounding   = 3.0f;
    style.TabRounding    = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FramePadding = ImVec2(6, 4);

    c[ImGuiCol_WindowBg]        = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_ChildBg]         = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_PopupBg]         = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_MenuBarBg]       = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_Border]          = ImVec4(0.20f, 0.20f, 0.24f, 1.00f);
    c[ImGuiCol_FrameBg]         = ImVec4(0.16f, 0.16f, 0.19f, 1.00f);
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.26f, 0.26f, 0.30f, 1.00f);
    c[ImGuiCol_TitleBg]         = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
    c[ImGuiCol_Tab]             = ImVec4(0.13f, 0.13f, 0.16f, 1.00f);
    c[ImGuiCol_TabHovered]      = ImVec4(0.24f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_TabActive]       = ImVec4(0.18f, 0.32f, 0.50f, 1.00f);
    c[ImGuiCol_Header]          = ImVec4(0.18f, 0.32f, 0.50f, 1.00f);
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.24f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_Button]          = ImVec4(0.18f, 0.32f, 0.50f, 1.00f);
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.24f, 0.45f, 0.70f, 1.00f);
    c[ImGuiCol_CheckMark]       = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    c[ImGuiCol_SliderGrab]      = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    c[ImGuiCol_DockingPreview]  = ImVec4(0.30f, 0.65f, 1.00f, 0.60f);
}
}

bool Application::Init() {
    SetAssetWorkingDirectory();
    if (!vulkan_.Init(1600, 950, "TinyShaderEngine v1.0")) return false;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;    // IDE-style dockspace
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = "tse_layout.ini";

    ImGui::StyleColorsDark();
    ApplyDarkIdeTheme();

    if (!vulkan_.InitImGuiBackend()) return false;

    scene_.SetGpuUploadContext(vulkan_.Device(), vulkan_.PhysicalDevice(), vulkan_.CommandPool(), vulkan_.GraphicsQueue());

    if (!effects_.shadowMap.Init(vulkan_.Device(), vulkan_.PhysicalDevice(), vulkan_.CommandPool(), vulkan_.GraphicsQueue())) {
        fprintf(stderr, "TinyShaderEngine: ShadowMap Init failed (missing shaders/shadow.vert.spv?)\n");
    }

    uint32_t rw = (uint32_t)vulkan_.FramebufferWidth(), rh = (uint32_t)vulkan_.FramebufferHeight();

    if (!sceneRenderer_.Init(vulkan_.Device(), vulkan_.PhysicalDevice(), vulkan_.CommandPool(), vulkan_.GraphicsQueue(), rw, rh)) {
        fprintf(stderr, "TinyShaderEngine: SceneRenderer Init failed (missing shaders/scene.*.spv?)\n");
    }
    if (!effects_.bloom.Init(vulkan_.Device(), vulkan_.PhysicalDevice(), vulkan_.CommandPool(), vulkan_.GraphicsQueue(), rw, rh)) {
        fprintf(stderr, "TinyShaderEngine: Bloom Init failed (missing shaders/bloom_*.spv?)\n");
    }
    if (!effects_.depthOfField.Init(vulkan_.Device(), vulkan_.PhysicalDevice(), vulkan_.CommandPool(), vulkan_.GraphicsQueue(), rw, rh)) {
        fprintf(stderr, "TinyShaderEngine: DepthOfField Init failed (missing shaders/dof.frag.spv?)\n");
    }
    if (!effects_.colorCorrect.Init(vulkan_.Device(), vulkan_.PhysicalDevice(), vulkan_.CommandPool(), vulkan_.GraphicsQueue(), rw, rh)) {
        fprintf(stderr, "TinyShaderEngine: ColorCorrect Init failed (missing shaders/colorcorrect.frag.spv?)\n");
    }
    if (!effects_.godRays.Init(vulkan_.Device(), vulkan_.PhysicalDevice(), vulkan_.CommandPool(), vulkan_.GraphicsQueue(), rw, rh)) {
        fprintf(stderr, "TinyShaderEngine: GodRays Init failed (missing shaders/godrays.frag.spv?)\n");
    }
    if (!composite_.Init(vulkan_.Device(), vulkan_.PhysicalDevice(), vulkan_.CommandPool(), vulkan_.GraphicsQueue(), rw, rh)) {
        fprintf(stderr, "TinyShaderEngine: PostComposite Init failed (missing shaders/composite.frag.spv?)\n");
    } else {
        // Registered once: the underlying VkImage/View is reused every
        // frame (contents update, handle doesn't), so one registration
        // stays valid for the app's lifetime.
        compositeImGuiTexture_ = ImGui_ImplVulkan_AddTexture(composite_.GetSampler(), composite_.GetResultView(),
                                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }

    ui_ = std::make_unique<UIManager>(scene_, effects_, composite_, vulkan_.Device(), vulkan_.PhysicalDevice());


    return true;
}

void Application::handleGlobalInput() {
    // Feature 4: 'K' toggles realtime viewport rendering.
    if (ImGui::IsKeyPressed(ImGuiKey_K, false)) {
        realtimePreview_ = !realtimePreview_;
    }
}

void Application::updateFps(float dt) {
    if (dt <= 0.0f) return;
    float instant = 1.0f / dt;
    // light smoothing so the counter doesn't jitter every frame
    fps_ = fps_ <= 0.0f ? instant : (fps_ * 0.9f + instant * 0.1f);
}

void Application::Run() {
    double lastTime = glfwGetTime();

    while (!vulkan_.ShouldClose()) {
        glfwPollEvents();

        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        updateFps(dt);

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        handleGlobalInput();

        ui_->Draw(realtimePreview_, fps_, (ImTextureID)compositeImGuiTexture_);

        ImGui::Render();

        if (vulkan_.AcquireAndBeginRecording()) {
            renderPipeline(vulkan_.CurrentCommandBuffer());

            vulkan_.BeginSwapchainRenderPass();
            ImDrawData* drawData = ImGui::GetDrawData();
            ImGui_ImplVulkan_RenderDrawData(drawData, vulkan_.CurrentCommandBuffer());
            vulkan_.EndFrame();
        }
    }
}

void Application::renderPipeline(VkCommandBuffer cmd) {
    // 1) Shadows: depth-only pass from the light's POV.
    if (effects_.shadowMap.enabled) {
        effects_.shadowMap.RenderPass(cmd, scene_);
    }

    // 2) Scene: real Lambertian shading, samples ShadowMap directly (see
    // shaders/scene.frag). Produces color + linear depth.
    float aspect = sceneRenderer_.Height() > 0 ? (float)sceneRenderer_.Width() / (float)sceneRenderer_.Height() : 1.0f;
    sceneRenderer_.RecordPass(cmd, scene_, effects_.shadowMap, aspect);
    VkImageView sceneColor = sceneRenderer_.GetColorView();
    VkImageView sceneDepth = sceneRenderer_.GetLinearDepthView();

    // 3) Bloom: reads scene color directly (branches from the pre-DoF
    // image, same as most engines, so blur isn't itself blurred by DoF).
    effects_.bloom.RecordPass(cmd, sceneColor);

    // 4) Depth of Field: reads scene color + linear depth.
    VkImageView afterDoF = sceneColor;
    if (effects_.depthOfField.enabled) {
        effects_.depthOfField.RecordPass(cmd, sceneColor, sceneDepth);
        afterDoF = effects_.depthOfField.GetResultView();
    }

    // 5) Color Correction: last grading step before compositing.
    VkImageView afterColorCorrect = afterDoF;
    if (effects_.colorCorrect.enabled) {
        effects_.colorCorrect.RecordPass(cmd, afterDoF);
        afterColorCorrect = effects_.colorCorrect.GetResultView();
    }

    // 6) God Rays: radiates from the Light node's screen position, occluded
    // by the same linear depth DoF uses.
    SceneRenderer::UV lightUV = sceneRenderer_.BuildLightScreenUV(scene_, aspect);
    Vec3 godRayColor = scene_.GetLights().empty() ? Vec3{1.0f, 0.75f, 0.4f} : scene_.PrimaryLight().colorTint;
    const SceneNode* selNode = scene_.SelectedNode();
    for (const auto& l : scene_.GetLights()) {
        if (l.get() == selNode) {
            godRayColor = l->colorTint;
            break;
        }
    }
    effects_.godRays.RecordPass(cmd, sceneDepth, lightUV.x, lightUV.y, lightUV.valid,
                                 godRayColor.x, godRayColor.y, godRayColor.z);

    // 7) Composite: base (post DoF/ColorCorrect) + Bloom + GodRays, with ACES tonemapping + gamma.
    composite_.RecordPass(cmd, afterColorCorrect, effects_.bloom.GetResultView(), effects_.godRays.GetResultView(),
                           effects_.bloom.enabled ? effects_.bloom.intensity : 0.0f,
                           effects_.godRays.enabled ? 1.0f : 0.0f,
                           composite_.tonemapMode, composite_.gamma);

}

void Application::Shutdown() {
    vulkan_.ShutdownImGuiBackend(); // also releases textures registered via ImGui_ImplVulkan_AddTexture
    ImGui::DestroyContext();
    // Must run before VulkanContext tears down the device.
    effects_.shadowMap.Shutdown(vulkan_.Device());
    effects_.bloom.Shutdown(vulkan_.Device());
    effects_.depthOfField.Shutdown(vulkan_.Device());
    effects_.colorCorrect.Shutdown(vulkan_.Device());
    effects_.godRays.Shutdown(vulkan_.Device());
    sceneRenderer_.Shutdown(vulkan_.Device());
    composite_.Shutdown(vulkan_.Device());
    scene_.ReleaseGpuResources();
    vulkan_.Shutdown();
}
