// Media Assembly GPU processor programs (native/media_assembly_gpu.cpp): the
// MediaAssemblyEngine.apply colour chain and geometry. One fullscreen
// triangle; the processing pass reads source texels by exact integer address
// (no sampler), so results do not depend on filtering. Compiled at build time
// by native/media_assembly_gpu.shaders.cmake (fxc /Gis /O3, vs_4_0 / ps_4_0),
// or at run time from the same text with the same flags without fxc.
Texture2D<float4> Source : register(t0);
Texture3D<float4> Cube : register(t1);
Texture2D<float4> SourceCurves : register(t2); // 256x1: linear r, g, b at each RGB8 code
cbuffer Edit : register(b0) {
    int4 Map0;      // origin.xy, column.xy
    int4 Map1;      // row.xy, source size
    uint4 Mode;     // steps, flags, resample target size
    float4 Controls;// exposure gain, brightness, contrast, saturation
    float4 Levels;  // gain, bias, gamma power
    float4 Matrix0; float4 Matrix1; float4 Matrix2;
    float4 Curve[4];// spline interval j: y, b, c, d
    float4 Source0; float4 Source1; float4 Source2; // embedded profile -> linear sRGB rows
};
// Bit 8 (Highlights/Shadows) is never set: that step applies no change on
// Windows (documented gap, modules/media_assembly_processor.hpp).
static const uint StepExposure=1u, StepControls=2u, StepTemperature=4u, StepTone=16u, StepLevels=32u, StepGamma=64u, StepLookup=128u;
static const uint FlagSourcePremultiplied=1u, FlagLinear=2u, FlagOutputPremultiplied=4u, FlagOutputUnclamped=8u, FlagSourceColour=16u;

float4 FullScreen(uint id : SV_VertexID) : SV_Position {
    float2 t = float2((id << 1) & 2, id & 2);
    return float4(t * float2(2, -2) + float2(-1, 1), 0, 1);
}
float Encode(float x) { return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(max(x, 0.0031308), 1.0 / 2.4) - 0.055; }
float Decode(float x) { return x <= 0.04045 ? x / 12.92 : pow((max(x, 0.04045) + 0.055) / 1.055, 2.4); }
float SignedDecode(float v) { return v < 0 ? -Decode(-v) : Decode(v); }
float SignedEncode(float v) { return v < 0 ? -Encode(-v) : Encode(v); }
float3 ToLinear(float3 v) { return float3(SignedDecode(v.r), SignedDecode(v.g), SignedDecode(v.b)); }
float3 ToEncoded(float3 v) { return float3(SignedEncode(v.r), SignedEncode(v.g), SignedEncode(v.b)); }
float Power(float x, float e) {
    if (e == 1) return x;
    if (e == 0) return 1;
    if (x <= 0) return 0;
    return exp2(e * log2(x));
}
float Tone(float x) {
    float e = Encode(saturate(x));
    uint j = min(3u, (uint)floor(e * 4));
    float dx = e - (float)j * 0.25;
    float4 k = Curve[j];
    float y = k.x + k.y * dx + k.z * dx * dx + k.w * dx * dx * dx;
    return Decode(saturate(y));
}
float3 Lookup(float3 v) {
    float3 at = saturate(v) * 31;
    uint3 lo = (uint3)at;
    uint3 hi = min(uint3(31, 31, 31), lo + 1);
    float3 f = at - (float3)lo;
    float3 o = 0;
    [unroll] for (uint z = 0; z < 2; ++z) [unroll] for (uint y = 0; y < 2; ++y) [unroll] for (uint x = 0; x < 2; ++x) {
        uint3 c = uint3(x ? hi.x : lo.x, y ? hi.y : lo.y, z ? hi.z : lo.z);
        float w = (x ? f.x : 1 - f.x) * (y ? f.y : 1 - f.y) * (z ? f.z : 1 - f.z);
        o += w * Cube.Load(int4(c, 0)).rgb;
    }
    return o;
}
float4 Edit(float4 c) {
    float a = c.a;
    if (a <= 0) return 0;
    uint steps = Mode.x, flags = Mode.y;
    float3 v = (flags & FlagSourcePremultiplied) ? c.rgb / a : c.rgb;
    bool linearSpace = (flags & FlagLinear) != 0;
    if (steps == 0) return float4(v, a);
    if (linearSpace) v = ToLinear(v);
    if (steps & StepExposure) v *= Controls.x;
    if (steps & StepControls) {
        float l = v.r * 0.2125 + v.g * 0.7154 + v.b * 0.0721;
        v = l + (v - l) * Controls.w;
        v = (v - 0.5) * Controls.z + 0.5;
        v += Controls.y;
    }
    if (steps & StepTemperature) v = float3(Matrix0.x * v.r + Matrix0.y * v.g + Matrix0.z * v.b,
                                           Matrix1.x * v.r + Matrix1.y * v.g + Matrix1.z * v.b,
                                           Matrix2.x * v.r + Matrix2.y * v.g + Matrix2.z * v.b);
    if (steps & StepTone) v = float3(Tone(v.r), Tone(v.g), Tone(v.b));
    if (steps & StepLevels) v = saturate(v * Levels.x + Levels.y);
    if (steps & StepGamma) v = float3(Power(max(v.r, 0), Levels.z), Power(max(v.g, 0), Levels.z), Power(max(v.b, 0), Levels.z));
    if (steps & StepLookup) {
        v = Lookup(saturate(linearSpace ? ToEncoded(v) : v));
        if (linearSpace) v = ToLinear(v);
    }
    if (linearSpace) v = ToEncoded(v);
    return float4(v, a);
}
float4 Output(float4 straight) {
    // Float export bands: associated and unclamped, as the CPU chain hands
    // them to the sticker composite.
    if (Mode.y & FlagOutputUnclamped) return float4(straight.rgb * straight.a, straight.a);
    float a = saturate(straight.a);
    float3 p = min(straight.rgb * straight.a, a);
    if (Mode.y & FlagOutputPremultiplied) return float4(p, a);
    return a <= 0 ? float4(0, 0, 0, 0) : float4(p / a, a);
}
// MediaAssemblySourceColour::convert: a straight source pixel described by
// an embedded profile into the working space (extended encoded sRGB).
float4 SourceColour(float4 c) {
    uint3 k = (uint3)round(saturate(c.rgb) * 255);
    float3 l = float3(SourceCurves.Load(int3(k.r, 0, 0)).r, SourceCurves.Load(int3(k.g, 0, 0)).g, SourceCurves.Load(int3(k.b, 0, 0)).b);
    float3 v = float3(Source0.x * l.r + Source0.y * l.g + Source0.z * l.b,
                      Source1.x * l.r + Source1.y * l.g + Source1.z * l.b,
                      Source2.x * l.r + Source2.y * l.g + Source2.z * l.b);
    return float4(ToEncoded(v), c.a);
}
float4 Process(float4 position : SV_Position) : SV_Target {
    int2 o = int2(position.xy);
    int2 s = Map0.xy + o.x * Map0.zw + o.y * Map1.xy;
    float4 c = float4(0, 0, 0, 0);
    if (s.x >= 0 && s.y >= 0 && s.x < Map1.z && s.y < Map1.w) {
        c = Source.Load(int3(s, 0));
        if (Mode.y & FlagSourceColour) c = SourceColour(c);
    }
    return Output(Edit(c));
}
// mediaAssemblyResample on associated pixels (no renormalisation at edges).
float4 Resample(float4 position : SV_Position) : SV_Target {
    float2 o = floor(position.xy);
    float2 scale = float2(Map1.zw) / float2(Mode.zw);
    float2 uv = (o + 0.5) * scale - 0.5;
    float2 f = floor(uv);
    int2 i0 = int2(f);
    float2 w = uv - f;
    float4 acc = float4(0, 0, 0, 0);
    [unroll] for (int dj = 0; dj < 2; ++dj) [unroll] for (int di = 0; di < 2; ++di) {
        int2 i = i0 + int2(di, dj);
        if (i.x >= 0 && i.y >= 0 && i.x < Map1.z && i.y < Map1.w)
            acc += ((di ? w.x : 1 - w.x) * (dj ? w.y : 1 - w.y)) * Source.Load(int3(i, 0));
    }
    float a = saturate(acc.a);
    return float4(min(acc.rgb, a), a);
}
