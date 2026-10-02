#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _16
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
};

struct _18
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

struct _20
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _22
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

constant float4 _68 = {};

struct main0_out
{
    float4 m_9 [[user(locn0)]];
    float4 m_14 [[user(locn5)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float4 m_3 [[attribute(0)]];
    float2 m_4 [[attribute(1)]];
    float2 m_5 [[attribute(2)]];
    float3 m_6 [[attribute(3)]];
    float4 m_7 [[attribute(5)]];
};

vertex main0_out main0(main0_in in [[stage_in]], constant _16& _17 [[buffer(0)]], constant _18& _19 [[buffer(1)]], constant _20& _21 [[buffer(2)]], constant _22& _23 [[buffer(3)]])
{
    main0_out out = {};
    float4 _82 = _17._m0 * float4(in.m_3.xyz, 1.0);
    bool _94 = _21._m0 > 0.0;
    float4 _170;
    float4 _171;
    float4 _172;
    float4 _173;
    if (_94)
    {
        _170 = float4(_23._m8[0][3], _23._m8[1][3], _23._m8[2][3], _23._m8[3][3]);
        _171 = float4(_23._m8[0][2], _23._m8[1][2], _23._m8[2][2], _23._m8[3][2]);
        _172 = float4(_23._m8[0][1], _23._m8[1][1], _23._m8[2][1], _23._m8[3][1]);
        _173 = float4(_23._m8[0][0], _23._m8[1][0], _23._m8[2][0], _23._m8[3][0]);
    }
    else
    {
        _170 = float4(_19._m8[0][3], _19._m8[1][3], _19._m8[2][3], _19._m8[3][3]);
        _171 = float4(_19._m8[0][2], _19._m8[1][2], _19._m8[2][2], _19._m8[3][2]);
        _172 = float4(_19._m8[0][1], _19._m8[1][1], _19._m8[2][1], _19._m8[3][1]);
        _173 = float4(_19._m8[0][0], _19._m8[1][0], _19._m8[2][0], _19._m8[3][0]);
    }
    float4 _179 = float4(float3(_82.xyz) - (_23._m11.xyz * _21._m0), 1.0) * float4x4(_173, _172, _171, _170);
    float4 _196;
    if (_94)
    {
        float4 _188 = _179;
        _188.x = _179.x * (1.0 - (_21._m1 * 2.0));
        _188.y = _179.y * (1.0 - (_21._m2 * 2.0));
        _196 = _188;
    }
    else
    {
        _196 = _179;
    }
    uint _199 = as_type<uint>(in.m_6.x);
    float3 _239;
    if ((_199 & 1073741824u) > 0u)
    {
        float _207 = float((_199 << 22u) >> 22u);
        float _210 = float((_199 << 12u) >> 22u);
        float3 _218 = float3((_207 >= 512.0) ? (_207 - 1024.0) : _207, (_210 >= 512.0) ? (_210 - 1024.0) : _210, 0.0) * 0.001956947147846221923828125;
        float _224 = (1.0 - abs(_218.x)) - abs(_218.y);
        float3 _225 = _218;
        _225.z = _224;
        float2 _236 = select(_225.xy, (float2(1.0) - abs(_225.yx)) * ((step(float2(0.0), _225.xy) * 2.0) - float2(1.0)), bool2(_224 < 0.0));
        _239 = fast::normalize(float3(_236.x, _236.y, _225.z));
    }
    else
    {
        _239 = in.m_6;
    }
    float4 _257 = _196 * 0.5;
    float2 _266 = float2(_257.x, _257.y * _23._m13.x) + float2(_257.w);
    float4 _270 = _196;
    _270.y = -_196.y;
    out.gl_Position = _270;
    out.m_9 = _82;
    out.m_14 = float4(_266.x, _266.y, _196.z, _196.w);
    return out;
}

