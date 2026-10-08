// Fresh plain-triangle foundation. Textures are decoded and premultiplied once
// on upload so filtering, color math and over blending use linear space. This does not
// substitute for the Mac renderer's original material/shader programs.
cbuffer Camera : register(b0) {
    column_major float4x4 viewProjection;
};
struct PlaneMask {
    column_major float4x4 worldToLocal;
    float4 bounds;
    float4 corners;
};
cbuffer Object : register(b1) {
    column_major float4x4 world;
    float4 linearTint;
    float opacity;
    uint maskCount;
    uint shutterEnabled;
    uint shutterStrips;
    PlaneMask masks[8];
    column_major float4x4 shutterWorldToLocal;
    float4 shutterEdges[30];
    column_major float4x4 alphaWorldToLocal;
    float4 alphaBounds;
    float4 alphaControl;
    column_major float4x4 angularWorldToLocal;
    float4 angularCenterControl;
    float4 angularNormals;
};
Texture2D<float4> colorTexture : register(t0);
SamplerState colorSampler : register(s0);
Texture2D<float4> alphaMaskTexture : register(t1);
SamplerState alphaMaskSampler : register(s1);

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
        [branch] if (masks[i].corners.x > 0) {
            float radius = masks[i].corners.x;
            float2 nearestCenter = clamp(localPoint,
                masks[i].bounds.xy + radius, masks[i].bounds.zw - radius);
            // Divide before squaring: valid large logical masks cannot overflow
            // merely because radius*radius exceeds the GPU float range.
            float2 corner = (localPoint - nearestCenter) / radius;
            clip(1 - dot(corner, corner));
        }
    }
    [branch] if (shutterEnabled != 0) {
        float4 local = mul(shutterWorldToLocal, input.worldPosition);
        clip(local.w - 0.0000001);
        float3 localPoint = float3(local.xy / local.w, 1);
        bool covered = false;
        [branch] if (shutterEnabled == 2) {
            // Original HUDSubsectionTransition: four six-vertex strips.
            // Its 24 edges fit the existing 30-edge constant buffer.
            [unroll] for (uint strip = 0; strip < 4; ++strip) {
                if ((shutterStrips & (1u << strip)) != 0) {
                    bool inside = true;
                    [unroll] for (uint edge = 0; edge < 6; ++edge)
                        inside = inside && dot(shutterEdges[strip * 6 + edge].xyz, localPoint) >= 0;
                    covered = covered || inside;
                }
            }
        } else {
            [unroll] for (uint strip = 0; strip < 6; ++strip) {
                if ((shutterStrips & (1u << strip)) != 0) {
                    bool inside = true;
                    [unroll] for (uint edge = 0; edge < 5; ++edge)
                        inside = inside && dot(shutterEdges[strip * 5 + edge].xyz, localPoint) >= 0;
                    covered = covered || inside;
                }
            }
        }
        clip(covered ? 1 : -1);
    }
    float4 sampled = colorTexture.Sample(colorSampler, input.uv);
    float4 tint = input.linearColor * linearTint;
    float opacityFactor = saturate(tint.a * opacity);
    [branch] if (alphaControl.x != 0) {
        float4 local = mul(alphaWorldToLocal, input.worldPosition);
        clip(local.w - 0.0000001);
        float2 alphaPoint = local.xy / local.w;
        clip(float4(alphaPoint - alphaBounds.xy, alphaBounds.zw - alphaPoint));
        float2 uv = (alphaPoint-alphaBounds.xy)/(alphaBounds.zw-alphaBounds.xy);
        opacityFactor *= saturate(alphaMaskTexture.Sample(alphaMaskSampler,uv).a);
    }
    [branch] if (angularCenterControl.z != 0) {
        [branch] if (angularCenterControl.z == 1) {
            opacityFactor = 0;
        } else if (angularCenterControl.z != 4) {
            float4 local = mul(angularWorldToLocal, input.worldPosition);
            clip(local.w - 0.0000001);
            float2 angularPoint = local.xy / local.w - angularCenterControl.xy;
            float2 distance = float2(dot(angularNormals.xy, angularPoint), dot(angularNormals.zw, angularPoint) + angularCenterControl.w);
            float2 coverage = saturate(distance / max(fwidth(distance), 0.0000001) + 0.5);
            // For a narrow arc the two butt-edge pixel coverages may overlap;
            // subtract that overlap instead of leaving a half-visible zero arc.
            bool opposed = dot(angularNormals.xy, angularNormals.zw) < 0;
            float angularCoverage = angularCenterControl.z == 2
                ? (opposed ? saturate(coverage.x + coverage.y - 1) : min(coverage.x, coverage.y))
                : (opposed ? saturate(coverage.x + coverage.y) : max(coverage.x, coverage.y));
            opacityFactor *= angularCoverage;
        }
    }
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
// Frame-server video surfaces are encoded straight BGRA8. Convert one texel
// per output texel before the usual linear-premultiplied scene filtering.
float4 MediaConvertPS(CompositeVertex input) : SV_Target {
    float4 encoded = saturate(colorTexture.Load(int3(int2(input.position.xy), 0)));
    float3 linearRGB = float3(
        encoded.r <= .04045 ? encoded.r / 12.92 : pow((encoded.r + .055) / 1.055, 2.4),
        encoded.g <= .04045 ? encoded.g / 12.92 : pow((encoded.g + .055) / 1.055, 2.4),
        encoded.b <= .04045 ? encoded.b / 12.92 : pow((encoded.b + .055) / 1.055, 2.4));
    return float4(linearRGB * encoded.a, encoded.a);
}
