// ImGui overlay pixel shader (bindless). The drawer's own pipeline layout binds
// descriptor set 0 = textures, set 1 = samplers, and push-constant range 1 =
// the per-draw slots, which plume places at buffer(8 + 1).

#include <metal_stdlib>

using namespace metal;

struct ImGuiVaryings {
    float4 pos [[position]];
    float4 col [[user(COLOR0)]];
    float2 uv [[user(TEXCOORD0)]];
};

struct TextureDescriptorHeap {
    texture2d<float> tex;
};

struct SamplerDescriptorHeap {
    sampler samp;
};

struct SlotsData {
    uint uTexSlot;
    uint uSampSlot;
};

[[fragment]]
float4 shaderMain(ImGuiVaryings input [[stage_in]],
                  constant TextureDescriptorHeap* gTextures [[buffer(0)]],
                  constant SamplerDescriptorHeap* gSamplers [[buffer(1)]],
                  constant SlotsData& slots [[buffer(9)]]) {
    // uTexSlot/uSampSlot are uniform per draw (push constant).
    // LOD 0: ImGui textures are single-mip.
    const float4 texel = gTextures[slots.uTexSlot].tex.sample(
        gSamplers[slots.uSampSlot].samp, input.uv, level(0.0));
    return texel * input.col;
}
