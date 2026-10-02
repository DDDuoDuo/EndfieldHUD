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

struct _24
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
    float _m9;
    float _m10;
    float _m11;
    float4 _m12;
};

constant float4 _76 = {};
constant float _81 = {};

struct main0_out
{
    float4 m_9 [[user(locn0)]];
    float4 m_12 [[user(locn3)]];
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

vertex main0_out main0(main0_in in [[stage_in]], constant _16& _17 [[buffer(0)]], constant _18& _19 [[buffer(1)]], constant _20& _21 [[buffer(2)]], constant _22& _23 [[buffer(3)]], constant _24& _25 [[buffer(4)]])
{
    main0_out out = {};
    float _90 = _25._m9 * 0.001000000047497451305389404296875;
    float _94 = (fmod(in.m_5.y, 2.0) * 2.0) - 1.0;
    float _97 = float((in.m_5.y < 2.0) ? 1 : (-1));
    float _100 = in.m_3.x - (0.00025000001187436282634735107421875 * _94);
    float _103 = in.m_3.y - (0.00025000001187436282634735107421875 * _97);
    float3 _111 = (float3(_100, _103, in.m_3.z) + (float3(0.0, 1.0, 0.0) * (_94 * _90))) + (float3(1.0, 0.0, 0.0) * (_97 * _90));
    float4 _118 = _17._m0 * float4(_111, 1.0);
    float3 _122 = float3(_118.xyz);
    float3 _128 = _23._m11.xyz * _21._m0;
    bool _130 = _21._m0 > 0.0;
    float4 _206;
    float4 _207;
    float4 _208;
    float4 _209;
    if (_130)
    {
        _206 = float4(_23._m8[0][3], _23._m8[1][3], _23._m8[2][3], _23._m8[3][3]);
        _207 = float4(_23._m8[0][2], _23._m8[1][2], _23._m8[2][2], _23._m8[3][2]);
        _208 = float4(_23._m8[0][1], _23._m8[1][1], _23._m8[2][1], _23._m8[3][1]);
        _209 = float4(_23._m8[0][0], _23._m8[1][0], _23._m8[2][0], _23._m8[3][0]);
    }
    else
    {
        _206 = float4(_19._m8[0][3], _19._m8[1][3], _19._m8[2][3], _19._m8[3][3]);
        _207 = float4(_19._m8[0][2], _19._m8[1][2], _19._m8[2][2], _19._m8[3][2]);
        _208 = float4(_19._m8[0][1], _19._m8[1][1], _19._m8[2][1], _19._m8[3][1]);
        _209 = float4(_19._m8[0][0], _19._m8[1][0], _19._m8[2][0], _19._m8[3][0]);
    }
    float4 _215 = float4(_122 - _128, 1.0) * float4x4(_209, _208, _207, _206);
    float4 _232;
    if (_130)
    {
        float4 _224 = _215;
        _224.x = _215.x * (1.0 - (_21._m1 * 2.0));
        _224.y = _215.y * (1.0 - (_21._m2 * 2.0));
        _232 = _224;
    }
    else
    {
        _232 = _215;
    }
    uint _235 = as_type<uint>(in.m_6.x);
    float3 _275;
    if ((_235 & 1073741824u) > 0u)
    {
        float _243 = float((_235 << 22u) >> 22u);
        float _246 = float((_235 << 12u) >> 22u);
        float3 _254 = float3((_243 >= 512.0) ? (_243 - 1024.0) : _243, (_246 >= 512.0) ? (_246 - 1024.0) : _246, 0.0) * 0.001956947147846221923828125;
        float _260 = (1.0 - abs(_254.x)) - abs(_254.y);
        float3 _261 = _254;
        _261.z = _260;
        float2 _272 = select(_261.xy, (float2(1.0) - abs(_261.yx)) * ((step(float2(0.0), _261.xy) * 2.0) - float2(1.0)), bool2(_260 < 0.0));
        _275 = fast::normalize(float3(_272.x, _272.y, _261.z));
    }
    else
    {
        _275 = in.m_6;
    }
    float4 _293 = _232 * 0.5;
    float2 _302 = float2(_293.x, _293.y * _23._m13.x) + float2(_293.w);
    float3 _309 = float3((_17._m0 * float4(_100, _103, in.m_3.z, 1.0)).xyz);
    float3 _310 = _122 - _309;
    float4 _322;
    if (_130)
    {
        _322 = float4(_81, _23._m1[1][0], _81, _81);
    }
    else
    {
        _322 = float4(_81, _19._m7[1][0], _81, _81);
    }
    float4 _333;
    if (_130)
    {
        _333 = float4(_81, _23._m1[1][1], _81, _81);
    }
    else
    {
        _333 = float4(_81, _19._m7[1][1], _81, _81);
    }
    float4 _344;
    if (_130)
    {
        _344 = float4(_81, _23._m1[1][2], _81, _81);
    }
    else
    {
        _344 = float4(_81, _19._m7[1][2], _81, _81);
    }
    float4 _355;
    if (_130)
    {
        _355 = float4(_81, _23._m1[1][3], _81, _81);
    }
    else
    {
        _355 = float4(_81, _19._m7[1][3], _81, _81);
    }
    float4 _369;
    if (_130)
    {
        _369 = float4(_23._m1[0][0], _81, _81, _81);
    }
    else
    {
        _369 = float4(_19._m7[0][0], _81, _81, _81);
    }
    float4 _380;
    if (_130)
    {
        _380 = float4(_23._m1[0][1], _81, _81, _81);
    }
    else
    {
        _380 = float4(_19._m7[0][1], _81, _81, _81);
    }
    float4 _391;
    if (_130)
    {
        _391 = float4(_23._m1[0][2], _81, _81, _81);
    }
    else
    {
        _391 = float4(_19._m7[0][2], _81, _81, _81);
    }
    float4 _402;
    if (_130)
    {
        _402 = float4(_23._m1[0][3], _81, _81, _81);
    }
    else
    {
        _402 = float4(_19._m7[0][3], _81, _81, _81);
    }
    float3 _415 = ((_309 + (fast::normalize(float4(_369.x, _380.x, _391.x, _402.x)).xyz * _310.z)) + (fast::normalize(float4(_322.y, _333.y, _344.y, _355.y)).xyz * _310.y)) + (fast::normalize(_23._m11.xyz - _309) * _310.x);
    float3 _418 = _415.xyz - _128;
    float4 _419 = float4(_418.x, _418.y, _418.z, _118.w);
    _419.w = 1.0;
    float4 _496;
    float4 _497;
    float4 _498;
    float4 _499;
    if (_130)
    {
        _496 = float4(_23._m8[0][3], _23._m8[1][3], _23._m8[2][3], _23._m8[3][3]);
        _497 = float4(_23._m8[0][2], _23._m8[1][2], _23._m8[2][2], _23._m8[3][2]);
        _498 = float4(_23._m8[0][1], _23._m8[1][1], _23._m8[2][1], _23._m8[3][1]);
        _499 = float4(_23._m8[0][0], _23._m8[1][0], _23._m8[2][0], _23._m8[3][0]);
    }
    else
    {
        _496 = float4(_19._m8[0][3], _19._m8[1][3], _19._m8[2][3], _19._m8[3][3]);
        _497 = float4(_19._m8[0][2], _19._m8[1][2], _19._m8[2][2], _19._m8[3][2]);
        _498 = float4(_19._m8[0][1], _19._m8[1][1], _19._m8[2][1], _19._m8[3][1]);
        _499 = float4(_19._m8[0][0], _19._m8[1][0], _19._m8[2][0], _19._m8[3][0]);
    }
    float4 _501 = _419 * float4x4(_499, _498, _497, _496);
    float4 _518;
    if (_130)
    {
        float4 _510 = _501;
        _510.x = _501.x * (1.0 - (_21._m1 * 2.0));
        _510.y = _501.y * (1.0 - (_21._m2 * 2.0));
        _518 = _510;
    }
    else
    {
        _518 = _501;
    }
    float4 _521 = _518;
    _521.y = -_518.y;
    out.gl_Position = _521;
    out.m_9 = float4(_415.x, _415.y, _415.z, _118.w);
    out.m_12 = float4(_111.x, _111.y, _111.z, _76.w);
    out.m_14 = float4(_302.x, _302.y, _232.z, _232.w);
    return out;
}

