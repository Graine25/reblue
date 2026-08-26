#include <metal_stdlib>

using namespace metal;

[[fragment]]
float4 shaderMain(device atomic_uint* counter [[buffer(4)]]) {
    atomic_fetch_add_explicit(counter, 1u, memory_order_relaxed);
    return float4(0.0);
}
