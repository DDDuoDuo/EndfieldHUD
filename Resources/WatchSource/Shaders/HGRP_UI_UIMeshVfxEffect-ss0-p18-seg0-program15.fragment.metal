#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _8
{
    float4x4 _m0;
    float4x4 _m1;
    float4x4 _m2;
    float4x4 _m3;
    float4x4 _m4;
    float4x4 _m5;
    float4x4 _m6;
    float4x4 _m7;
    float4x4 _m8;
    float4x4 _m9;
    float4x4 _m10;
    float4 _m11;
    float4 _m12;
    float4 _m13;
    float4 _m14;
    float4 _m15;
};

struct _10
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
    float _m4;
    float _m5;
    float _m6;
    float _m7;
    float4 _m8;
    float4 _m9;
    float4 _m10;
    float4 _m11;
    float _m12;
    float _m13;
    float _m14;
    float4 _m15;
    float4 _m16;
    float4 _m17;
    float _m18;
    float _m19;
    float _m20;
    float _m21;
    float _m22;
    float _m23;
    float _m24;
    float _m25;
    float _m26;
    float _m27;
    float4 _m28;
    float4 _m29;
    float4 _m30;
    float4 _m31;
    float4 _m32;
    float4 _m33;
    float4 _m34;
};

struct main0_out
{
    float4 m_5 [[color(0)]];
};

struct main0_in
{
    float2 m_3 [[user(locn0)]];
    float4 m_4 [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _9 [[buffer(0)]], constant _10& _11 [[buffer(1)]], texture2d<float> _13 [[texture(0)]], texture2d<float> _15 [[texture(1)]], texture2d<float> _17 [[texture(2)]], sampler _14 [[sampler(0)]], sampler _16 [[sampler(1)]], sampler _18 [[sampler(2)]])
{
    main0_out out = {};
    float4 _91 = _11._m11 * select(in.m_4, float4(1.0), bool4(_11._m0 != 0.0));
    float3 _95 = _91.xyz * _11._m1;
    float4 _96 = float4(_95.x, _95.y, _95.z, _91.w);
    _96.w = _91.w * _11._m2;
    float _113 = fmod(_9._m12.y, 1024.0);
    float4 _137 = _13.sample(_14, ((((float2x2(float2(_11._m10.xy), float2(_11._m10.zw)) * ((mix(in.m_3, in.m_3, float2(_11._m5)) + (_11._m9.xy * _113)) - float2(0.5))) + float2(0.5)) * _11._m8.xy) + _11._m8.zw));
    float4 _175 = _15.sample(_16, ((((float2x2(float2(_11._m17.xy), float2(_11._m17.zw)) * ((mix(in.m_3, in.m_3, float2(_11._m14)) + (_11._m16.xy * _113)) - float2(0.5))) + float2(0.5)) * _11._m15.xy) + _11._m15.zw));
    float4 _180 = (_96 * mix(_137, float4(1.0, 1.0, 1.0, _137.x), float4(_11._m4))) * mix(_175, float4(1.0, 1.0, 1.0, _175.x), float4(_11._m13));
    float2 _207 = (((float2x2(float2(_11._m30.xy), float2(_11._m30.zw)) * ((mix(in.m_3, in.m_3, float2(_11._m18)) + (_11._m29.xy * _113)) - float2(0.5))) + float2(0.5)) * _11._m28.xy) + _11._m28.zw;
    float4 _235 = _17.sample(_18, _207);
    float _249 = (_235.x - _11._m21) + mix(0.0, ((_11._m27 - 1.0) != 0.0) ? length(_207 - _11._m34.xy) : dot(fast::normalize(_11._m33.xy), _207), fast::clamp(_11._m27, 0.0, 1.0));
    if (_249 < 0.0)
    {
        discard_fragment();
    }
    float _256 = smoothstep(_11._m23, _11._m23 + _11._m24, 1.0 - fast::clamp(_249, 0.0, 1.0));
    float _257 = 1.0 - _11._m25;
    float3 _273 = float3(mix(_180, mix(_11._m31, _11._m32, float4(smoothstep(_257, _257 + _11._m26, _256))) * _11._m11.x, float4(_256)).xyz).xyz;
    float3 _284 = fast::clamp((_273 + (precise::max(_273 - float3(_11._m6), float3(0.0)) * _11._m7)).xyz, float3(0.0), float3(500.0));
    float4 _285 = float4(_284.x, _284.y, _284.z, _180.w);
    _285.w = fast::clamp(_180.w * mix(1.0, 1.0 - _256, _11._m22), 0.0, 1.0);
    out.m_5 = _285;
    return out;
}

