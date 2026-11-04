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
float3 encodeSRGB(float3 linearRGB);
float3 decodeSRGB(float3 encodedRGB) {
    encodedRGB=saturate(encodedRGB);
    return float3(encodedRGB.r<=.04045?encodedRGB.r/12.92:pow((encodedRGB.r+.055)/1.055,2.4),
        encodedRGB.g<=.04045?encodedRGB.g/12.92:pow((encodedRGB.g+.055)/1.055,2.4),
        encodedRGB.b<=.04045?encodedRGB.b/12.92:pow((encodedRGB.b+.055)/1.055,2.4));
}
// alphaInput: an alpha-only texture (DXGI A8, stored where every covered
// texel is white) samples as (0,0,0,a); premultiplied white is (a,a,a,a) in
// every color space, so it reads as that linear-premultiplied texel.
float4 sceneColor(SceneVertex input,bool encodedInput,bool encodedOutput,bool alphaInput) {
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
    if (alphaInput) sampled = sampled.aaaa;
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
    // This constant-folded default branch preserves the original shader math.
    if(!encodedInput&&!encodedOutput)
        return float4(sampled.rgb * tint.rgb * opacityFactor, sampled.a * opacityFactor);
    if(sampled.a<=0)return 0;
    float3 color=sampled.rgb/sampled.a;
    if(encodedInput&&encodedOutput)color*=encodeSRGB(tint.rgb);
    else if(encodedInput)color=decodeSRGB(color)*tint.rgb;
    else color=encodeSRGB(color*tint.rgb);
    return float4(color*sampled.a*opacityFactor,sampled.a*opacityFactor);
}
float4 ScenePS(SceneVertex input):SV_Target{return sceneColor(input,false,false,false);}
float4 SceneEncodedInputPS(SceneVertex input):SV_Target{return sceneColor(input,true,false,false);}
float4 SceneEncodedOutputPS(SceneVertex input):SV_Target{return sceneColor(input,false,true,false);}
float4 SceneEncodedBothPS(SceneVertex input):SV_Target{return sceneColor(input,true,true,false);}
float4 SceneAlphaPS(SceneVertex input):SV_Target{return sceneColor(input,false,false,true);}
float4 SceneAlphaEncodedOutputPS(SceneVertex input):SV_Target{return sceneColor(input,false,true,true);}

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
// Renderer::setContentFilter: the confirm bar's dim of the module content
// (Sources/HUDDesignOverlay.swift, CIGaussianBlur + CIExposureAdjust). One
// separable Gaussian pass per axis over the offscreen linear-premultiplied
// content: weights exp(-i^2 / (2 sigma^2)) to 3 sigma, normalized over the taps
// (premultiplied, so transparent texels add nothing and edges soften into
// transparency). Texels outside the source read as transparent (Load returns
// 0 out of range). The second pass multiplies RGB by 2^EV (exposure in linear
// light; alpha unchanged) and is composited over the root target.
cbuffer Filter : register(b2) {
    float4 filterStep;   // xy: tap step (1,0) or (0,1); zw: destination pixel of source texel (0,0)
    float4 filterParams; // x: 1 / (2 sigma^2); y: taps per side; z: linear gain
};
float4 blurAt(int2 p) {
    float4 sum = colorTexture.Load(int3(p, 0));
    float total = 1;
    int2 step = int2(filterStep.xy);
    [loop] for (int i = 1; i <= (int)filterParams.y; ++i) {
        float weight = exp(-float(i * i) * filterParams.x);
        sum += weight * (colorTexture.Load(int3(p + step * i, 0)) + colorTexture.Load(int3(p - step * i, 0)));
        total += 2 * weight;
    }
    return sum / total;
}
float4 ContentBlurPS(CompositeVertex input) : SV_Target {
    return blurAt(int2(input.position.xy) - int2(filterStep.zw));
}
float4 ContentDimPS(CompositeVertex input) : SV_Target {
    float4 blurred = blurAt(int2(input.position.xy) - int2(filterStep.zw));
    return float4(blurred.rgb * filterParams.z, blurred.a);
}
