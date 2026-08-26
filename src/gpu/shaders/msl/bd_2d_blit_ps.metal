// Host substitute for the engine's 2D blit pixel shader: samples Tex0 through
// the bindless heap (via SharedConstants), like every recompiled shader.

#include <metal_stdlib>

using namespace metal;

struct BlitVaryings {
    float4 position [[position]];
    float4 color [[user(COLOR0)]];
    float2 uv [[user(TEXCOORD0)]];
};

struct Texture2DDescriptorHeap {
    texture2d<float> tex;
};

struct SamplerDescriptorHeap {
    sampler samp;
};

struct PushConstants {
    ulong VertexShaderConstants;
    ulong PixelShaderConstants;
    ulong SharedConstants;
};

// texture2DIndices[0] at +0, samplerIndices[0] at +192, matching the HLSL.
#define Tex0_ResourceDescriptorIndex \
    (*reinterpret_cast<device const uint*>(push.SharedConstants + 0))
#define Tex0_SamplerDescriptorIndex \
    (*reinterpret_cast<device const uint*>(push.SharedConstants + 192))

[[fragment]]
float4 shaderMain(BlitVaryings input [[stage_in]],
                  constant Texture2DDescriptorHeap* g_Texture2DDescriptorHeap [[buffer(0)]],
                  constant SamplerDescriptorHeap* g_SamplerDescriptorHeap [[buffer(3)]],
                  constant PushConstants& push [[buffer(8)]]) {
    texture2d<float> tex = g_Texture2DDescriptorHeap[Tex0_ResourceDescriptorIndex].tex;
    // bdBeginTextBatch asks for LINEAR, bdRenderDebugTextBegin for NEAREST, so a
    // hardcoded slot gets one of the two wrong.
    sampler samp = g_SamplerDescriptorHeap[Tex0_SamplerDescriptorIndex].samp;
    return tex.sample(samp, input.uv) * input.color;
}
