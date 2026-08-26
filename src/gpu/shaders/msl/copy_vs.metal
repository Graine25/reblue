#include "copy_common.metali"

[[vertex]]
FullscreenVaryings shaderMain(uint vertexId [[vertex_id]]) {
    FullscreenVaryings output{};
    output.texCoord = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(output.texCoord * float2(2.0, -2.0) +
                                 float2(-1.0, 1.0),
                             0.0, 1.0);
    return output;
}
