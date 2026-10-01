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
    float4 _m0;
    float4x4 _m1;
    float4 _m2;
    float4 _m3;
    float _m4;
    float4 _m5;
    float4 _m6;
    float4 _m7;
    float _m8;
    float _m9;
    float _m10;
    float _m11;
    float _m12;
    float _m13;
    float _m14;
    float _m15;
    float4 _m16;
    float4 _m17;
    float4 _m18;
    float _m19;
    float _m20;
    float _m21;
    float _m22;
    float _m23;
    float _m24;
    float _m25;
    float _m26;
    float _m27;
    float _m28;
    float4 _m29;
    float4 _m30;
    float4 _m31;
    float4 _m32;
    float4 _m33;
    float4 _m34;
    float4 _m35;
    float _m36;
    float _m37;
    float _m38;
    float4 _m39;
    float4 _m40;
    float4 _m41;
};

struct main0_out
{
    float4 m_5 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _9 [[buffer(0)]], constant _10& _11 [[buffer(1)]], texture2d<float> _13 [[texture(0)]], texture2d<float> _15 [[texture(1)]], texture2d<float> _17 [[texture(2)]], sampler _14 [[sampler(0)]], sampler _16 [[sampler(1)]], sampler _18 [[sampler(2)]])
{
    main0_out out = {};
    float4 _78 = in.m_3;
    _78.w = rint(_78.w * 255.0) * 0.0039215688593685626983642578125;
    float _96 = fmod(_9._m12.y, 1024.0);
    float4 _120 = _13.sample(_14, ((((float2x2(float2(_11._m17.xy), float2(_11._m17.zw)) * ((mix(in.m_4, in.m_4, float2(_11._m15)) + (_11._m16.xy * _96)) - float2(0.5))) + float2(0.5)) * _11._m18.xy) + _11._m18.zw));
    float4 _158 = _17.sample(_18, ((((float2x2(float2(_11._m41.xy), float2(_11._m41.zw)) * ((mix(in.m_4, in.m_4, float2(_11._m38)) + (_11._m40.xy * _96)) - float2(0.5))) + float2(0.5)) * _11._m39.xy) + _11._m39.zw));
    float4 _163 = (_78 * mix(_120, float4(1.0, 1.0, 1.0, _120.x), float4(_11._m14))) * mix(_158, float4(1.0, 1.0, 1.0, _158.x), float4(_11._m37));
    float2 _190 = (((float2x2(float2(_11._m31.xy), float2(_11._m31.zw)) * ((mix(in.m_4, in.m_4, float2(_11._m19)) + (_11._m30.xy * _96)) - float2(0.5))) + float2(0.5)) * _11._m29.xy) + _11._m29.zw;
    float4 _218 = _15.sample(_16, _190);
    float _232 = (_218.x - _11._m22) + mix(0.0, ((_11._m28 - 1.0) != 0.0) ? length(_190 - _11._m35.xy) : dot(fast::normalize(_11._m34.xy), _190), fast::clamp(_11._m28, 0.0, 1.0));
    if (_232 < 0.0)
    {
        discard_fragment();
    }
    float _239 = smoothstep(_11._m24, _11._m24 + _11._m25, 1.0 - fast::clamp(_232, 0.0, 1.0));
    float _240 = 1.0 - _11._m26;
    float _255 = _163.w * mix(1.0, 1.0 - _239, _11._m23);
    if ((_255 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float3 _261 = float3(mix(_163, mix(_11._m32, _11._m33, float4(smoothstep(_240, _240 + _11._m27, _239))) * _11._m5.x, float4(_239)).xyz).xyz * _255;
    float4 _262 = float4(_261.x, _261.y, _261.z, _163.w);
    _262.w = mix(_255, 0.0, _11._m8);
    out.m_5 = _262;
    return out;
}

