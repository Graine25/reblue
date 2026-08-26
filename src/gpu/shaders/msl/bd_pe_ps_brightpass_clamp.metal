// bd_pe_ps_brightpass (0xFFDBD782126EB6E8) from the XenosRecomp shader cache,
// with the bright-mask export clamp reblue swaps in at shader link time.
// Hand-maintained MSL port of hlsl/bd_pe_ps_brightpass_clamp.hlsl - the two
// must stay behaviourally identical, but neither generates the other.

#include "bd_pe_ps_common.metali"

#define g_vParam BUFFER_LOAD_FLOAT4(g_PushConstants.PixelShaderConstants + 432)

[[fragment]] PixelShaderOutput shaderMain(BD_PE_PS_PARAMS) {
    const float4 c254 = asfloat(uint4(0x3E800000, 0x40800000, 0x3E59999A, 0x0));
    const float4 c255 = asfloat(uint4(0x3D93A92A, 0x3F372474, 0x3F800000, 0x0));

    BD_PE_PS_REGISTERS;

    r0.xyz = tfetch2D(ScreenTexture_Texture2DDescriptorIndex,
                      ScreenTexture_SamplerDescriptorIndex, r0.xy,
                      float2(0, 0))
                 .xyz;
    r1.xyz = r0.xyz + -g_vParam.xxx;
    r0.xzw = min(r0.xyz, c254.xxx);
    ps = max(g_vParam.y, g_vParam.y);
    r1.yzw = max(r1.xyz, c255.www);
    ps = c254.z * ps;
    r0.y = ps;
    r2.z = r0.y * r1.y;
    ps = c254.y * r0.x;
    r1.x = ps;
    r2.xy = r1.zw * g_vParam.yy;
    ps = c254.y * r0.z;
    r1.y = ps;
    r0.y = dot(r2.yxz, c255.xyz);
    ps = c254.y * r0.w;
    r1.z = ps;
    shaderOutput.oC0.xyz = r1.xyz * r0.yyy;
    shaderOutput.oC0.w = 1.0;
    // X360 saturates this bright-mask on the EDRAM export before the bloom
    // blur, and unclamped FP16 lets BriMulti-scaled masks (up to ~59 on wc01)
    // blur into giant white blobs.
    shaderOutput.oC0.xyz = min(shaderOutput.oC0.xyz, 1.0);
    if (g_SpecConstants() & SPEC_CONSTANT_ALPHA_TEST) {
        clip(shaderOutput.oC0.w - g_AlphaThreshold);
    }
    return shaderOutput;
}
