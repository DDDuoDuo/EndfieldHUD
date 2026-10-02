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
    float _m13;
    float _m14;
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
    float4 _48 = in.m_3;
    _48.w = rint(_48.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _58 = _14.sample(_15, in.m_4);
    _58.w = (_12._m12 != 0.0) ? 1.0 : _58.w;
    float4 _68 = _48 * (_58 + _10._m0[0]._m2);
    float _69 = _68.w;
    if ((_69 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float _76 = _69 * _12._m13;
    float3 _78 = _68.xyz * _76;
    float4 _79 = float4(_78.x, _78.y, _78.z, _68.w);
    _79.w = mix(_76, 0.0, _12._m8);
    out.m_5 = _79;
    return out;
}

