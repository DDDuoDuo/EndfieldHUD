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
    float2 m_10 [[user(locn1)]];
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
    float4 _83 = _17._m0 * float4(in.m_3.xyz, 1.0);
    bool _95 = _21._m0 > 0.0;
    float4 _171;
    float4 _172;
    float4 _173;
    float4 _174;
    if (_95)
    {
        _171 = float4(_23._m8[0][3], _23._m8[1][3], _23._m8[2][3], _23._m8[3][3]);
        _172 = float4(_23._m8[0][2], _23._m8[1][2], _23._m8[2][2], _23._m8[3][2]);
        _173 = float4(_23._m8[0][1], _23._m8[1][1], _23._m8[2][1], _23._m8[3][1]);
        _174 = float4(_23._m8[0][0], _23._m8[1][0], _23._m8[2][0], _23._m8[3][0]);
    }
    else
    {
        _171 = float4(_19._m8[0][3], _19._m8[1][3], _19._m8[2][3], _19._m8[3][3]);
        _172 = float4(_19._m8[0][2], _19._m8[1][2], _19._m8[2][2], _19._m8[3][2]);
        _173 = float4(_19._m8[0][1], _19._m8[1][1], _19._m8[2][1], _19._m8[3][1]);
        _174 = float4(_19._m8[0][0], _19._m8[1][0], _19._m8[2][0], _19._m8[3][0]);
    }
    float4 _180 = float4(float3(_83.xyz) - (_23._m11.xyz * _21._m0), 1.0) * float4x4(_174, _173, _172, _171);
    float4 _197;
    if (_95)
    {
        float4 _189 = _180;
        _189.x = _180.x * (1.0 - (_21._m1 * 2.0));
        _189.y = _180.y * (1.0 - (_21._m2 * 2.0));
        _197 = _189;
    }
    else
    {
        _197 = _180;
    }
    uint _201 = as_type<uint>(in.m_6.x);
    float3 _241;
    if ((_201 & 1073741824u) > 0u)
    {
        float _209 = float((_201 << 22u) >> 22u);
        float _212 = float((_201 << 12u) >> 22u);
        float3 _220 = float3((_209 >= 512.0) ? (_209 - 1024.0) : _209, (_212 >= 512.0) ? (_212 - 1024.0) : _212, 0.0) * 0.001956947147846221923828125;
        float _226 = (1.0 - abs(_220.x)) - abs(_220.y);
        float3 _227 = _220;
        _227.z = _226;
        float2 _238 = select(_227.xy, (float2(1.0) - abs(_227.yx)) * ((step(float2(0.0), _227.xy) * 2.0) - float2(1.0)), bool2(_226 < 0.0));
        _241 = fast::normalize(float3(_238.x, _238.y, _227.z));
    }
    else
    {
        _241 = in.m_6;
    }
    float4 _259 = _197 * 0.5;
    float2 _268 = float2(_259.x, _259.y * _23._m13.x) + float2(_259.w);
    float4 _272 = _197;
    _272.y = -_197.y;
    out.gl_Position = _272;
    out.m_9 = _83;
    out.m_10 = in.m_4;
    out.m_14 = float4(_268.x, _268.y, _197.z, _197.w);
    return out;
}

