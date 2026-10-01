#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _10
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _9
{
    _10 _m0[1];
};

struct _12
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
};

struct main0_out
{
    float4 m_6 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn3)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _9& _11 [[buffer(0)]], constant _12& _13 [[buffer(1)]], texture2d<float> _15 [[texture(0)]], sampler _16 [[sampler(0)]])
{
    main0_out out = {};
    float4 _57 = in.m_3;
    _57.w = rint(_57.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _68 = _15.sample(_16, in.m_4);
    _68.w = (_13._m12 != 0.0) ? 1.0 : _68.w;
    float4 _78 = _57 * (_68 + _11._m0[0]._m2);
    float2 _89 = fast::clamp(((_11._m0[0]._m3.zw - _11._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _95 = fast::clamp(_11._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _100 = ((in.m_5.xy + _95.xy) + _95.zw) * 0.5;
    float _101 = _100.x;
    float _106 = _100.y;
    float _126 = (_78.w * (_89.x * _89.y)) * (((smoothstep(0.0, _11._m0[0]._m4.x, _101 - _95.x) * smoothstep(0.0, _11._m0[0]._m4.z, _95.z - _101)) * smoothstep(0.0, _11._m0[0]._m4.y, _106 - _95.y)) * smoothstep(0.0, _11._m0[0]._m4.w, _95.w - _106));
    float3 _128 = _78.xyz * _126;
    float4 _129 = float4(_128.x, _128.y, _128.z, _78.w);
    _129.w = mix(_126, 0.0, _13._m8);
    out.m_6 = _129;
    return out;
}

