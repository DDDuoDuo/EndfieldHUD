#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _10
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
};

struct _12
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

struct _16
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

struct _18
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _20
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

struct _22
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
    float _m15;
    float _m16;
    float _m17;
    float _m18;
    float _m19;
    float _m20;
    float _m21;
    float _m22;
    float _m23;
    float _m24;
    float4 _m25;
};

constant float _91 = {};
constant float _94 = {};

struct main0_out
{
    float4 m_9 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
    float2 m_5 [[user(locn2)]];
    float4 m_6 [[user(locn3)]];
    float3 m_7 [[user(locn4)]];
    float4 m_8 [[user(locn5)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _10& _11 [[buffer(0)]], constant _12& _13 [[buffer(1)]], constant _16& _17 [[buffer(2)]], constant _18& _19 [[buffer(3)]], constant _20& _21 [[buffer(4)]], constant _22& _23 [[buffer(5)]], texture2d<float> _25 [[texture(0)]], sampler _26 [[sampler(0)]])
{
    main0_out out = {};
    bool _103 = _19._m0 > 0.0;
    float4 _113;
    if (_103)
    {
        _113 = float4(_94, _94, _21._m1[2][0], _94);
    }
    else
    {
        _113 = float4(_94, _94, _13._m7[2][0], _94);
    }
    float4 _124;
    if (_103)
    {
        _124 = float4(_94, _94, _21._m1[2][1], _94);
    }
    else
    {
        _124 = float4(_94, _94, _13._m7[2][1], _94);
    }
    float4 _135;
    if (_103)
    {
        _135 = float4(_94, _94, _21._m1[2][2], _94);
    }
    else
    {
        _135 = float4(_94, _94, _13._m7[2][2], _94);
    }
    float3 _138 = fast::normalize(float3(_113.z, _124.z, _135.z));
    float2 _145 = in.m_8.xy / float2(in.m_8.w);
    if (isnan(_145.x) || isnan(_145.y))
    {
        discard_fragment();
    }
    float3x3 _166 = float3x3(float4(_11._m0[0][0], _11._m0[1][0], _11._m0[2][0], _11._m0[3][0]).xyz, float4(_11._m0[0][1], _11._m0[1][1], _11._m0[2][1], _11._m0[3][1]).xyz, float4(_11._m0[0][2], _11._m0[1][2], _11._m0[2][2], _11._m0[3][2]).xyz);
    float _199 = dot(in.m_6.xyz, _23._m9.xyz);
    float _203 = fract(_199 * _23._m0);
    float _206 = _23._m13 * 0.100000001490116119384765625;
    float4 _252;
    float4 _253;
    float4 _254;
    if (_103)
    {
        _252 = float4(_21._m8[0][2], _21._m8[1][2], _21._m8[2][2], _91);
        _253 = float4(_21._m8[0][1], _21._m8[1][1], _21._m8[2][1], _91);
        _254 = float4(_21._m8[0][0], _21._m8[1][0], _21._m8[2][0], _91);
    }
    else
    {
        _252 = float4(_13._m8[0][2], _13._m8[1][2], _13._m8[2][2], _91);
        _253 = float4(_13._m8[0][1], _13._m8[1][1], _13._m8[2][1], _91);
        _254 = float4(_13._m8[0][0], _13._m8[1][0], _13._m8[2][0], _91);
    }
    float2 _269 = (fast::normalize((in.m_7 * float3x3(_254.xyz, _253.xyz, _252.xyz)).xy) * float2(_21._m14.y / _21._m14.x, 1.0)) * _206;
    float2 _274 = float2(_23._m14 * 0.300000011920928955078125) * float2(0.000925925909541547298431396484375);
    float _287 = precise::max(_206, length(select(float2(0.0), _274 * float2(int2(sign(_269))), abs(_269) < _274))) * sin(acos(dot(in.m_7, fast::normalize(_23._m9.xyz * _166))));
    float3 _339 = (((_25.sample(_26, ((float2(_199, 0.5) * _23._m8.xy) + _23._m8.zw)).xyz * ((fast::clamp(((_203 * (-1.0)) + _287) * _23._m12, 0.0, 1.0) + fast::clamp(((_203 + _287) - 1.0) * _23._m12, 0.0, 1.0)) + (_23._m15 * smoothstep(_23._m16, _23._m17, smoothstep(0.0, 1.0, dot(_138, in.m_7)))))) * _23._m21) + (mix(_23._m11.xyz * _23._m11.w, _23._m10.xyz * _23._m10.w, float3(smoothstep(_23._m18, _23._m19, fast::clamp(dot(_138, mix(fast::normalize((float3(1.0 / dot(float4(_11._m0[0][0], _11._m0[1][0], _11._m0[2][0], _11._m0[3][0]).xyz, float4(_11._m0[0][0], _11._m0[1][0], _11._m0[2][0], _11._m0[3][0]).xyz), 1.0 / dot(float4(_11._m0[0][1], _11._m0[1][1], _11._m0[2][1], _11._m0[3][1]).xyz, float4(_11._m0[0][1], _11._m0[1][1], _11._m0[2][1], _11._m0[3][1]).xyz), 1.0 / dot(float4(_11._m0[0][2], _11._m0[1][2], _11._m0[2][2], _11._m0[3][2]).xyz, float4(_11._m0[0][2], _11._m0[1][2], _11._m0[2][2], _11._m0[3][2]).xyz)) * float3(in.m_4, in.m_5.x)) * _166), in.m_7, float3(_23._m20))), 0.0, 1.0)))) * _23._m22)).xyz;
    float3 _340 = _339 * _23._m1;
    float4 _341 = float4(_340.x, _340.y, _340.z, float4(1.0).w);
    float2 _348 = fast::clamp(abs((_145 * 2.0) - float2(1.0)) * _23._m23, float2(0.0), float2(1.0));
    float _351 = fast::clamp(1.0 - dot(_348, _348), 0.0, 1.0);
    bool _361 = _23._m5 != 0.0;
    float _378 = _361 ? 1.0 : smoothstep(_23._m6, _23._m7, distance((_17._m9 * float4(in.m_3.xyz, 1.0)).xz, float2(0.0)));
    if (!(_378 != 0.0))
    {
        discard_fragment();
    }
    float _386 = (_361 ? mix(0.0, 1.0, _351 / precise::max(0.001000000047497451305389404296875, ((1.0 - _23._m24) * _351) + _23._m24)) : 1.0) * (_378 * _23._m2);
    _341.w = _386;
    float3 _389 = _341.xyz * _386;
    out.m_9 = float4(_389.x, _389.y, _389.z, _341.w);
    return out;
}

