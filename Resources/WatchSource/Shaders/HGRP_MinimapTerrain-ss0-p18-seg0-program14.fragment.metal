#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _7
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4x4 _m5;
    float4x4 _m6;
    float4x4 _m7;
    float4x4 _m8;
    int _m9;
    float4 _m10;
};

struct _11
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
    float _m6;
    float _m7;
    float _m8;
    float4x4 _m9;
};

struct _13
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _15
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

struct _17
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
};

constant float _62 = {};

struct main0_out
{
    float4 m_6 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float3 m_4 [[user(locn4)]];
    float4 m_5 [[user(locn5)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _7& _8 [[buffer(0)]], constant _11& _12 [[buffer(1)]], constant _13& _14 [[buffer(2)]], constant _15& _16 [[buffer(3)]], constant _17& _18 [[buffer(4)]], texture2d<float> _20 [[texture(0)]], sampler _21 [[sampler(0)]])
{
    main0_out out = {};
    bool _69 = _14._m0 > 0.0;
    float4 _79;
    if (_69)
    {
        _79 = float4(_62, _62, _16._m1[2][0], _62);
    }
    else
    {
        _79 = float4(_62, _62, _8._m7[2][0], _62);
    }
    float4 _90;
    if (_69)
    {
        _90 = float4(_62, _62, _16._m1[2][1], _62);
    }
    else
    {
        _90 = float4(_62, _62, _8._m7[2][1], _62);
    }
    float4 _101;
    if (_69)
    {
        _101 = float4(_62, _62, _16._m1[2][2], _62);
    }
    else
    {
        _101 = float4(_62, _62, _8._m7[2][2], _62);
    }
    float _106 = smoothstep(0.0, 1.0, dot(fast::normalize(float3(_79.z, _90.z, _101.z)), in.m_4));
    float2 _111 = in.m_5.xy / float2(in.m_5.w);
    if (isnan(_111.x) || isnan(_111.y))
    {
        discard_fragment();
    }
    float _119 = fwidth(_106);
    float _120 = precise::max(_119, 9.9999997473787516355514526367188e-06);
    float _127 = smoothstep(_18._m3 - _120, _18._m4 + _120, _106);
    float4 _145 = _20.sample(_21, ((_111 * _18._m8.xy) + _18._m8.zw));
    float3 _154 = (mix(_18._m9.xyz, _18._m10.xyz, float3(_127)).xyz * _18._m1).xyz + float3(_127 * _145.x);
    float4 _155 = float4(_154.x, _154.y, _154.z, float4(1.0).w);
    float _173 = (_18._m5 != 0.0) ? 1.0 : smoothstep(_18._m6, _18._m7, distance((_12._m9 * float4(in.m_3.xyz, 1.0)).xz, float2(0.0)));
    if (!(_173 != 0.0))
    {
        discard_fragment();
    }
    float _180 = _173 * _18._m2;
    _155.w = _180;
    float3 _183 = _155.xyz * _180;
    out.m_6 = float4(_183.x, _183.y, _183.z, _155.w);
    return out;
}

