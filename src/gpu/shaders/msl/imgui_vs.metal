// ImGui overlay vertex shader: pixel-space -> NDC via the per-frame ortho push
// constant. plume maps push-constant range n to buffer(8 + n), and the drawer
// registers ortho as range 0 (see imgui_overlay_drawer.cpp).

#include <metal_stdlib>

using namespace metal;

struct ImGuiVertexInput {
    float2 pos [[attribute(0)]];
    float2 uv [[attribute(1)]];
    // The IA decodes R8G8B8A8_UNORM -> rgba (IM_COL32 order).
    float4 col [[attribute(2)]];
};

struct ImGuiVaryings {
    float4 pos [[position]];
    float4 col [[user(COLOR0)]];
    float2 uv [[user(TEXCOORD0)]];
};

struct OrthoData {
    float2 uScale;
    float2 uTranslate;
};

[[vertex]]
ImGuiVaryings shaderMain(ImGuiVertexInput input [[stage_in]],
                         constant OrthoData& ortho [[buffer(8)]]) {
    ImGuiVaryings output{};
    output.pos = float4(input.pos * ortho.uScale + ortho.uTranslate, 0.0, 1.0);
    output.col = input.col;
    output.uv = input.uv;
    return output;
}
