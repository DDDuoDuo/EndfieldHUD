// Fresh plain-triangle foundation. Textures are decoded and premultiplied once
// on upload so filtering, color math and over blending use linear space. This does not
// substitute for the Mac renderer's original material/shader programs.
cbuffer Camera : register(b0) {
    column_major float4x4 viewProjection;
};
struct PlaneMask {
    column_major float4x4 worldToLocal;
    float4 bounds;
};
cbuffer Object : register(b1) {
    column_major float4x4 world;
    float4 linearTint;
    float opacity;
    uint maskCount;
    float2 objectPadding;
    PlaneMask masks[8];
};
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);

struct VertexInput {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float4 linearColor : COLOR0;
};
struct SceneVertex {
    float4 position : SV_Position;
    float4 worldPosition : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float4 linearColor : COLOR0;
};
SceneVertex SceneVS(VertexInput input) {
    SceneVertex result;
    result.worldPosition = mul(world, float4(input.position, 1));
    result.position = mul(viewProjection, result.worldPosition);
    result.uv = input.uv;
    result.linearColor = input.linearColor;
    return result;
}
float4 ScenePS(SceneVertex input) : SV_Target {
    [loop] for (uint i = 0; i < maskCount; ++i) {
        float4 local = mul(masks[i].worldToLocal, input.worldPosition);
        clip(local.w - 0.0000001);
        float2 localPoint = local.xy / local.w;
        clip(float4(localPoint - masks[i].bounds.xy, masks[i].bounds.zw - localPoint));
    }
    float4 sampled = colorTexture.Sample(colorSampler, input.uv);
    float4 tint = input.linearColor * linearTint;
    float opacityFactor = saturate(tint.a * opacity);
    return float4(sampled.rgb * tint.rgb * opacityFactor, sampled.a * opacityFactor);
}

struct CompositeVertex { float4 position : SV_Position; };
CompositeVertex CompositeVS(uint id : SV_VertexID) {
    CompositeVertex result;
    float2 corner = float2((id << 1) & 2, id & 2);
    result.position = float4(corner * float2(2, -2) + float2(-1, 1), 0, 1);
    return result;
}
float3 encodeSRGB(float3 linearRGB) {
    linearRGB = saturate(linearRGB);
    return float3(
        linearRGB.r <= 0.0031308 ? linearRGB.r * 12.92 : 1.055 * pow(linearRGB.r, 1.0 / 2.4) - 0.055,
        linearRGB.g <= 0.0031308 ? linearRGB.g * 12.92 : 1.055 * pow(linearRGB.g, 1.0 / 2.4) - 0.055,
        linearRGB.b <= 0.0031308 ? linearRGB.b * 12.92 : 1.055 * pow(linearRGB.b, 1.0 / 2.4) - 0.055);
}
float4 CompositePS(CompositeVertex input) : SV_Target {
    float4 linearPremultiplied = colorTexture.Load(int3(int2(input.position.xy), 0));
    float alpha = saturate(linearPremultiplied.a);
    // DirectComposition expects encoded-space premultiplication. Applying an
    // sRGB RTV transfer directly to premultiplied linear RGB would be different.
    if (alpha <= 0) return 0;
    return float4(encodeSRGB(linearPremultiplied.rgb / alpha) * alpha, alpha);
}
