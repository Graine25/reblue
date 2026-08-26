#include "copy_common.metali"

[[fragment]]
float4 shaderMain(FullscreenVaryings input [[stage_in]],
                  constant Texture2DDescriptorHeap* textureHeap [[buffer(0)]],
                  constant CopyPushConstants& push [[buffer(8)]]) {
    texture2d<float> texture = textureHeap[push.resourceDescriptorIndex].tex;
    const uint ratio = uint(push.param1 + 0.5);
    if (ratio >= 2u) {
        const int2 base = int2(input.position.xy) * int(ratio);
        float4 sum = 0.0;
        for (uint y = 0; y < ratio; ++y)
            for (uint x = 0; x < ratio; ++x)
                sum += texture.read(uint2(base + int2(x, y)), 0);
        return float4(sum.rgb / float(ratio * ratio),
                      sum.a / float(ratio * ratio));
    }
    // Match the Metal path in UnleashedRecomp: these helpers copy exact source
    // texels. Avoiding a bindless sampler also keeps the final composite
    // independent of the descriptor-set sampler state.
    const uint2 dimensions{texture.get_width(), texture.get_height()};
    const uint2 pixel = min(uint2(input.texCoord * float2(dimensions)),
                            dimensions - uint2(1));
    float4 sample = texture.read(pixel, 0);
    return float4(sample.rgb * push.param0, sample.a);
}
