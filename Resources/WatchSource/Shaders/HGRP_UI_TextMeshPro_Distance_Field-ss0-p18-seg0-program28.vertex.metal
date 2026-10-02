#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _16
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
    float4 _m6;
    float4 _m7;
    float4 _m8;
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

struct _22
{
    float4 _m0;
    float4 _m1;
    float _m2;
    float _m3;
    float _m4;
    float _m5;
    float _m6;
    float _m7;
    float _m8;
    float _m9;
    float _m10;
    float _m11;
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
    float4 _m23;
};

struct _24
{
    float _m0;
    float _m1;
    float4 _m2;
    float _m3;
    float _m4;
    float _m5;
    float _m6;
    float4 _m7;
    float _m8;
    float _m9;
    float _m10;
    float _m11;
    float _m12;
    float _m13;
    float _m14;
    float _m15;
    float4 _m16;
    float4 _m17;
    float3 _m18;
    float4x4 _m19;
    float4 _m20;
    float _m21;
    float _m22;
    float _m23;
    float _m24;
    float _m25;
    float4 _m26;
    float _m27;
    float _m28;
    float _m29;
    float _m30;
    float4 _m31;
    float _m32;
    float _m33;
    float _m34;
    float _m35;
    float _m36;
    float _m37;
    float _m38;
    float _m39;
    float _m40;
    float _m41;
    float _m42;
    float _m43;
    float _m44;
    float4 _m45;
    float _m46;
    float _m47;
    float _m48;
    float _m49;
    float _m50;
    float _m51;
    float _m52;
    float _m53;
    float _m54;
    float4 _m55;
    float4x4 _m56;
    float4 _m57;
    float4 _m58;
};

struct _26
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _28
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

struct _31
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _30
{
    _31 _m0[1];
};

constant float _115 = {};
constant float _118 = {};

struct main0_out
{
    float4 m_9 [[user(locn0)]];
    float2 m_10 [[user(locn1)]];
    float4 m_11 [[user(locn2)]];
    float4 m_13 [[user(locn4)]];
    float4 m_14 [[user(locn5)]];
    float4 m_15 [[user(locn6)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float4 m_3 [[attribute(0)]];
    float3 m_4 [[attribute(1)]];
    float4 m_5 [[attribute(2)]];
    float2 m_6 [[attribute(3)]];
    float2 m_7 [[attribute(4)]];
};

vertex main0_out main0(main0_in in [[stage_in]], constant _16& _17 [[buffer(0)]], constant _18& _19 [[buffer(1)]], constant _22& _23 [[buffer(2)]], constant _24& _25 [[buffer(3)]], constant _26& _27 [[buffer(4)]], constant _28& _29 [[buffer(5)]], constant _30& _32 [[buffer(6)]], texture2d<float> _34 [[texture(0)]], sampler _35 [[sampler(0)]])
{
    main0_out out = {};
    float4 _120 = in.m_3;
    float4 _131 = _120;
    _131.x = _120.x + _25._m42;
    _131.y = _120.y + _25._m43;
    float4 _213;
    float _214;
    if (_23._m2 > 0.5)
    {
        float _152 = fast::clamp((in.m_3.x - _23._m11) / precise::max(9.9999997473787516355514526367188e-05, _23._m12 - _23._m11), 0.0, 1.0);
        float _162 = mix(_23._m9, _23._m10, _152);
        float2 _165 = _120.xy * (_162 * 0.00999999977648258209228515625);
        float4 _189 = _34.sample(_35, float4(((_165 + (in.m_6 * _23._m23.xy)) + _23._m23.zw) + (float2(_23._m3, _23._m4) * _17._m0.y), 0.0, 0.0).xy, level(0.0));
        float _190 = _189.x;
        float2 _198 = _165 + (in.m_6 * _162);
        float _199 = _198.x;
        float _200 = _198.y;
        float2 _211 = _131.xy + (float2(sin(_199 + _200) + (_190 * _23._m5), cos((_199 * 1.37000000476837158203125) - _200) + (_190 * _23._m6)) * mix(_23._m7, _23._m8, _152));
        _213 = float4(_211.x, _211.y, _131.z, _131.w);
        _214 = _152;
    }
    else
    {
        _213 = _131;
        _214 = 0.0;
    }
    bool _233 = _27._m0 > 0.0;
    float4 _309;
    float4 _310;
    float4 _311;
    float4 _312;
    if (_233)
    {
        _309 = float4(_29._m8[0][3], _29._m8[1][3], _29._m8[2][3], _29._m8[3][3]);
        _310 = float4(_29._m8[0][2], _29._m8[1][2], _29._m8[2][2], _29._m8[3][2]);
        _311 = float4(_29._m8[0][1], _29._m8[1][1], _29._m8[2][1], _29._m8[3][1]);
        _312 = float4(_29._m8[0][0], _29._m8[1][0], _29._m8[2][0], _29._m8[3][0]);
    }
    else
    {
        _309 = float4(_19._m8[0][3], _19._m8[1][3], _19._m8[2][3], _19._m8[3][3]);
        _310 = float4(_19._m8[0][2], _19._m8[1][2], _19._m8[2][2], _19._m8[3][2]);
        _311 = float4(_19._m8[0][1], _19._m8[1][1], _19._m8[2][1], _19._m8[3][1]);
        _312 = float4(_19._m8[0][0], _19._m8[1][0], _19._m8[2][0], _19._m8[3][0]);
    }
    float4 _318 = float4(float3((_32._m0[0]._m0 * float4(_213.xyz, 1.0)).xyz) - (_29._m11.xyz * _27._m0), 1.0) * float4x4(_312, _311, _310, _309);
    float4 _335;
    if (_233)
    {
        float4 _327 = _318;
        _327.x = _318.x * (1.0 - (_27._m1 * 2.0));
        _327.y = _318.y * (1.0 - (_27._m2 * 2.0));
        _335 = _327;
    }
    else
    {
        _335 = _318;
    }
    float4 _366;
    float4 _367;
    if (_233)
    {
        _366 = float4(_29._m2[0][1], _29._m2[1][1], _115, _115);
        _367 = float4(_29._m2[0][0], _29._m2[1][0], _115, _115);
    }
    else
    {
        _366 = float4(_19._m5[0][1], _19._m5[1][1], _115, _115);
        _367 = float4(_19._m5[0][0], _19._m5[1][0], _115, _115);
    }
    float2 _377 = float2(_335.w) / (float2(_25._m51, _25._m52) * abs(_29._m14.xy * float2x2(_367.xy, _366.xy)));
    float _388 = rsqrt(dot(_377, _377)) * ((abs(in.m_7.y) * _25._m50) * (_25._m54 + 1.0));
    float4 _398;
    if (_233)
    {
        _398 = float4(_118, _118, _118, _29._m2[3][3]);
    }
    else
    {
        _398 = float4(_118, _118, _118, _19._m5[3][3]);
    }
    float _432;
    if (_398.w == 0.0)
    {
        _432 = mix(abs(_388) * (1.0 - _25._m53), _388, abs(dot(fast::normalize((float3(1.0 / dot(float4(_32._m0[0]._m0[0][0], _32._m0[0]._m0[1][0], _32._m0[0]._m0[2][0], _32._m0[0]._m0[3][0]).xyz, float4(_32._m0[0]._m0[0][0], _32._m0[0]._m0[1][0], _32._m0[0]._m0[2][0], _32._m0[0]._m0[3][0]).xyz), 1.0 / dot(float4(_32._m0[0]._m0[0][1], _32._m0[0]._m0[1][1], _32._m0[0]._m0[2][1], _32._m0[0]._m0[3][1]).xyz, float4(_32._m0[0]._m0[0][1], _32._m0[0]._m0[1][1], _32._m0[0]._m0[2][1], _32._m0[0]._m0[3][1]).xyz), 1.0 / dot(float4(_32._m0[0]._m0[0][2], _32._m0[0]._m0[1][2], _32._m0[0]._m0[2][2], _32._m0[0]._m0[3][2]).xyz, float4(_32._m0[0]._m0[0][2], _32._m0[0]._m0[1][2], _32._m0[0]._m0[2][2], _32._m0[0]._m0[3][2]).xyz)) * in.m_4) * float3x3(float4(_32._m0[0]._m0[0][0], _32._m0[0]._m0[1][0], _32._m0[0]._m0[2][0], _32._m0[0]._m0[3][0]).xyz, float4(_32._m0[0]._m0[0][1], _32._m0[0]._m0[1][1], _32._m0[0]._m0[2][1], _32._m0[0]._m0[3][1]).xyz, float4(_32._m0[0]._m0[0][2], _32._m0[0]._m0[1][2], _32._m0[0]._m0[2][2], _32._m0[0]._m0[3][2]).xyz)), fast::normalize(_29._m11.xyz - (_32._m0[0]._m0 * _213).xyz))));
    }
    else
    {
        _432 = _388;
    }
    float _445 = (((mix(_25._m37, _25._m38, step(in.m_7.y, 0.0)) * 0.25) + _25._m3) * _25._m39) * 0.5;
    float _447 = 0.5 / _432;
    float _471;
    if (_23._m18 > 0.5)
    {
        _471 = (1.0 - (_23._m19 * _25._m39)) - (_23._m21 * _25._m39);
    }
    else
    {
        _471 = (1.0 - (_25._m8 * _25._m39)) - (_25._m4 * _25._m39);
    }
    float4 _477 = fast::clamp(_32._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float _481 = floor(in.m_7.x * 0.000244140625);
    float2 _485 = float2(_481, in.m_7.x - (4096.0 * _481)) * 0.001953125;
    float4 _498 = _335 * 0.5;
    float2 _507 = float2(_498.x, _498.y * _29._m13.x) + float2(_498.w);
    float4 _563 = _335;
    _563.y = -_335.y;
    out.gl_Position = _563;
    out.m_9 = in.m_5;
    out.m_10 = in.m_6;
    out.m_11 = float4(((_471 * 0.5) - _447) - _445, _432, (0.5 - _445) + _447, _445);
    out.m_13 = float4((float3(_29._m11.xyz) - (_32._m0[0]._m0 * _213).xyz) * float3x3(float4(_25._m19[0][0], _25._m19[1][0], _25._m19[2][0], _25._m19[3][0]).xyz, float4(_25._m19[0][1], _25._m19[1][1], _25._m19[2][1], _25._m19[3][1]).xyz, float4(_25._m19[0][2], _25._m19[1][2], _25._m19[2][2], _25._m19[3][2]).xyz), _214);
    out.m_14 = float4((_485 * _25._m57.xy) + _25._m57.zw, (_485 * _25._m58.xy) + _25._m58.zw);
    out.m_15 = float4(_507.x, _507.y, _335.z, _335.w);
    return out;
}

