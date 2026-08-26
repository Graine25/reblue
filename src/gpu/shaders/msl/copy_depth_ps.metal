
#include "copy_common.metali"

struct DepthOutput {
    float depth [[depth(any)]];
};

[[fragment]]
DepthOutput shaderMain(FullscreenVaryings input [[stage_in]],
                        constant Texture2DDescriptorHeap* textureHeap [[buffer(0)]],
                        constant CopyPushConstants& push [[buffer(8)]]) {
    DepthOutput output{};
    texture2d<float> texture = textureHeap[push.resourceDescriptorIndex].tex;
    const uint2 dimensions{texture.get_width(), texture.get_height()};
    const uint2 pixel = min(uint2(input.texCoord * float2(dimensions)),
                            dimensions - uint2(1));
    output.depth = texture.read(pixel, 0).x;
    return output;
}
