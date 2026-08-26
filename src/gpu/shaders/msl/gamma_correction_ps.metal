#include "copy_common.metali"

[[fragment]]
float4 shaderMain(FullscreenVaryings input [[stage_in]],
                  constant Texture2DDescriptorHeap* textureHeap [[buffer(0)]],
                  constant CopyPushConstants& push [[buffer(8)]]) {
    // The Metal reference renderer reads the final target directly rather
    // than sampling through the bindless sampler table. This is an exact
    // texel copy at native resolution and avoids a black composite if that
    // sampler binding is not visible to the host helper pipeline.
    texture2d<float> texture = textureHeap[push.resourceDescriptorIndex].tex;
    const uint2 dimensions{texture.get_width(), texture.get_height()};
    const uint2 pixel = min(uint2(input.texCoord * float2(dimensions)),
                            dimensions - uint2(1));
    float4 sampled = texture.read(pixel, 0);
    const float3 ramp = pow(max(sampled.rgb, 0.0), push.param0);
    const float3 linear = mix(ramp / 12.92,
                              pow((ramp + 0.055) / 1.055, 2.4),
                              step(float3(0.04045), ramp));
    const float3 corrected = mix(linear * 4.5,
                                 1.099 * pow(linear, 0.45) - 0.099,
                                 step(float3(0.018), linear));
    return float4(mix(ramp, corrected, push.param1), 1.0);
}
