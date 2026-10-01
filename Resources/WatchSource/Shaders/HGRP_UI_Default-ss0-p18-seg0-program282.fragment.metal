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
    float4 _47 = in.m_3;
    _47.w = rint(_47.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _57 = _14.sample(_15, in.m_4);
    _57.w = (_12._m12 != 0.0) ? 1.0 : _57.w;
    float4 _67 = _47 * (_57 + _10._m0[0]._m2);
    float _68 = _67.w;
    if ((_68 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float3 _74 = _67.xyz * _68;
    float4 _75 = float4(_74.x, _74.y, _74.z, _67.w);
    _75.w = mix(_68, 0.0, _12._m8);
    out.m_5 = _75;
    return out;
}

