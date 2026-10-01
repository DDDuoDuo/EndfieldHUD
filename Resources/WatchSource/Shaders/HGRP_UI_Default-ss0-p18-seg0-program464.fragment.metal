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
    float4 _58 = in.m_3;
    _58.w = rint(_58.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _69 = _15.sample(_16, in.m_4);
    _69.w = (_13._m12 != 0.0) ? 1.0 : _69.w;
    float4 _79 = _58 * (_69 + _11._m0[0]._m2);
    float2 _90 = fast::clamp(((_11._m0[0]._m3.zw - _11._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _96 = fast::clamp(_11._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _101 = ((in.m_5.xy + _96.xy) + _96.zw) * 0.5;
    float _102 = _101.x;
    float _107 = _101.y;
    float _127 = (_79.w * (_90.x * _90.y)) * (((smoothstep(0.0, _11._m0[0]._m4.x, _102 - _96.x) * smoothstep(0.0, _11._m0[0]._m4.z, _96.z - _102)) * smoothstep(0.0, _11._m0[0]._m4.y, _107 - _96.y)) * smoothstep(0.0, _11._m0[0]._m4.w, _96.w - _107));
    if ((_127 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float3 _133 = _79.xyz * _127;
    float4 _134 = float4(_133.x, _133.y, _133.z, _79.w);
    _134.w = mix(_127, 0.0, _13._m8);
    out.m_6 = _134;
    return out;
}

