#include <metal_stdlib>

using namespace metal;

struct BlitVertexInput {
    float2 position [[attribute(0)]];
    float2 uv [[attribute(1)]];
    float4 color [[attribute(2)]];
};

struct BlitVaryings {
    float4 position [[position]];
    float4 color [[user(COLOR0)]];
    float2 uv [[user(TEXCOORD0)]];
};

[[vertex]]
BlitVaryings shaderMain(BlitVertexInput input [[stage_in]]) {
    BlitVaryings output{};
    output.position = float4(input.position * float2(2.0, -2.0) +
                                 float2(-1.0, 1.0),
                             0.0, 1.0);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}
