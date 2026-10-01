#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _9
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _8
{
    _9 _m0[1];
};

struct _11
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
    float4 m_5 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _10 [[buffer(0)]], constant _11& _12 [[buffer(1)]], texture2d<float> _14 [[texture(0)]], sampler _15 [[sampler(0)]])
{
    main0_out out = {};
    float4 _46 = in.m_3;
    _46.w = rint(_46.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _56 = _14.sample(_15, in.m_4);
    _56.w = (_12._m12 != 0.0) ? 1.0 : _56.w;
    float4 _66 = _46 * (_56 + _10._m0[0]._m2);
    float _67 = _66.w;
    float3 _69 = _66.xyz * _67;
    float4 _70 = float4(_69.x, _69.y, _69.z, _66.w);
    _70.w = mix(_67, 0.0, _12._m8);
    out.m_5 = _70;
    return out;
}

