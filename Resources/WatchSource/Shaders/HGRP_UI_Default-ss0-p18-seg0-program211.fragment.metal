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

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _9 [[buffer(0)]], constant _10& _11 [[buffer(1)]], texture2d<float> _13 [[texture(0)]], sampler _14 [[sampler(0)]])
{
    main0_out out = {};
    float4 _49 = in.m_3;
    _49.w = rint(_49.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _91 = _13.sample(_14, ((((float2x2(float2(_11._m17.xy), float2(_11._m17.zw)) * ((mix(in.m_4, in.m_4, float2(_11._m15)) + (_11._m16.xy * fmod(_9._m12.y, 1024.0))) - float2(0.5))) + float2(0.5)) * _11._m18.xy) + _11._m18.zw));
    float4 _96 = _49 * mix(_91, float4(1.0, 1.0, 1.0, _91.x), float4(_11._m14));
    float _97 = _96.w;
    float3 _99 = _96.xyz * _97;
    float4 _100 = float4(_99.x, _99.y, _99.z, _96.w);
    _100.w = mix(_97, 0.0, _11._m8);
    out.m_5 = _100;
    return out;
}

