#include "EffectsStubPanel.h"
#include "imgui.h"

void EffectsStubPanel::drawStubHeader(const char* name, bool& enabled, bool implemented) {
    ImGui::Checkbox(name, &enabled);
    ImGui::SameLine();
    if (implemented) ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1), "(active)");
    else ImGui::TextDisabled("(not implemented yet)");
}

void EffectsStubPanel::Draw() {
    ImGui::Begin("Effects");
    ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1), "Post-Processing & Shading Pipeline");
    ImGui::Separator();

    // ---- Tone Mapping & Output ----
    if (ImGui::CollapsingHeader("Tone Mapping & Output", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent();
        const char* toneModes[] = { "ACES Filmic (Cinematic)", "Reinhard", "Linear (Unclamped)" };
        ImGui::Combo("Operator", &composite_.tonemapMode, toneModes, 3);
        ImGui::SliderFloat("Display Gamma", &composite_.gamma, 1.0f, 3.0f, "%.2f");
        if (ImGui::SmallButton("Reset to ACES (2.2)")) {
            composite_.tonemapMode = 0;
            composite_.gamma = 2.2f;
        }
        ImGui::TextDisabled("Prevents clipping and produces rich, photographic color.");
        ImGui::Unindent();
    }
    ImGui::Spacing();

    // ---- Shadow Map ----
    drawStubHeader("Shadow Map", effects_.shadowMap.enabled, true);
    if (effects_.shadowMap.enabled) {
        ImGui::Indent();
        int res = effects_.shadowMap.resolution;
        const char* resLabels[] = { "512", "1024", "2048", "4096" };
        const int resValues[] = { 512, 1024, 2048, 4096 };
        int current = 2;
        for (int i = 0; i < 4; i++) if (resValues[i] == res) current = i;
        if (ImGui::Combo("Resolution", &current, resLabels, 4)) {
            effects_.shadowMap.resolution = resValues[current];
            effects_.shadowMap.Resize(device_, physicalDevice_);
        }
        ImGui::SliderFloat("Depth Bias", &effects_.shadowMap.bias, 0.0001f, 0.01f, "%.4f");
        ImGui::TextDisabled("Sampled with 3x3 Soft PCF and slope-scaled bias from Primary Light.");
        ImGui::Unindent();
    }
    ImGui::Spacing();

    // ---- Bloom ----
    drawStubHeader("Bloom", effects_.bloom.enabled, true);
    if (effects_.bloom.enabled) {
        ImGui::Indent();
        ImGui::SliderFloat("Threshold", &effects_.bloom.threshold, 0.0f, 3.0f);
        ImGui::SliderFloat("Intensity", &effects_.bloom.intensity, 0.0f, 3.0f);
        ImGui::SliderInt("Mip Levels", &effects_.bloom.mipLevels, 2, Bloom::kMaxMips);
        ImGui::TextDisabled("13-tap Jimenez downsample + soft knee dual filter.");
        ImGui::Unindent();
    }
    ImGui::Spacing();

    // ---- Depth of Field ----
    drawStubHeader("Depth of Field", effects_.depthOfField.enabled, true);
    if (effects_.depthOfField.enabled) {
        ImGui::Indent();
        ImGui::SliderFloat("Focus Distance", &effects_.depthOfField.focusDistance, 0.1f, 50.0f);
        ImGui::SliderFloat("Focus Range", &effects_.depthOfField.focusRange, 0.1f, 20.0f);
        ImGui::SliderFloat("Bokeh Strength", &effects_.depthOfField.bokehStrength, 0.0f, 5.0f);
        ImGui::TextDisabled("24-tap Vogel spiral golden-angle bokeh disc.");
        ImGui::Unindent();
    }
    ImGui::Spacing();

    // ---- Color Correction ----
    drawStubHeader("Color Correction", effects_.colorCorrect.enabled, true);
    if (effects_.colorCorrect.enabled) {
        ImGui::Indent();
        ImGui::SliderFloat("Exposure", &effects_.colorCorrect.exposure, -4.0f, 4.0f);
        ImGui::SliderFloat("Contrast", &effects_.colorCorrect.contrast, 0.0f, 2.0f);
        ImGui::SliderFloat("Saturation", &effects_.colorCorrect.saturation, 0.0f, 2.0f);
        ImGui::ColorEdit3("Color Filter", effects_.colorCorrect.colorFilter);
        ImGui::TextDisabled("Grading stage applied before Tone Mapping.");
        ImGui::Unindent();
    }
    ImGui::Spacing();

    // ---- God Rays ----
    drawStubHeader("God Rays", effects_.godRays.enabled, true);
    if (effects_.godRays.enabled) {
        ImGui::Indent();
        ImGui::SliderFloat("Density", &effects_.godRays.density, 0.0f, 2.0f);
        ImGui::SliderFloat("Decay", &effects_.godRays.decay, 0.5f, 1.0f);
        ImGui::SliderFloat("Weight", &effects_.godRays.weight, 0.0f, 2.0f);
        ImGui::SliderInt("Samples", &effects_.godRays.samples, 8, GodRays::kMaxSamples);
        ImGui::TextDisabled("Screen-space radial rays with border fade and distance decay.");
        ImGui::Unindent();
    }

    ImGui::End();
}

