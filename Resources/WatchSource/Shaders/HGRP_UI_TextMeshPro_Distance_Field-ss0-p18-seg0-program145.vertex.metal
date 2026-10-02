#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _17
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

struct _19
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

struct _23
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

struct _25
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

struct _27
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _29
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

struct _32
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _31
{
    _32 _m0[1];
};

constant float _117 = {};
constant float _120 = {};

struct main0_out
{
    float4 m_9 [[user(locn0)]];
    float2 m_10 [[user(locn1)]];
    float4 m_11 [[user(locn2)]];
    float4 m_12 [[user(locn3)]];
    float4 m_13 [[user(locn4)]];
    float4 m_14 [[user(locn5)]];
    float4 m_15 [[user(locn6)]];
    float2 m_16 [[user(locn7)]];
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

vertex main0_out main0(main0_in in [[stage_in]], constant _17& _18 [[buffer(0)]], constant _19& _20 [[buffer(1)]], constant _23& _24 [[buffer(2)]], constant _25& _26 [[buffer(3)]], constant _27& _28 [[buffer(4)]], constant _29& _30 [[buffer(5)]], constant _31& _33 [[buffer(6)]], texture2d<float> _35 [[texture(0)]], sampler _36 [[sampler(0)]])
{
    main0_out out = {};
    float4 _122 = in.m_3;
    float4 _133 = _122;
    _133.x = _122.x + _26._m42;
    _133.y = _122.y + _26._m43;
    float4 _215;
    float _216;
    if (_24._m2 > 0.5)
    {
        float _154 = fast::clamp((in.m_3.x - _24._m11) / precise::max(9.9999997473787516355514526367188e-05, _24._m12 - _24._m11), 0.0, 1.0);
        float _164 = mix(_24._m9, _24._m10, _154);
        float2 _167 = _122.xy * (_164 * 0.00999999977648258209228515625);
        float4 _191 = _35.sample(_36, float4(((_167 + (in.m_6 * _24._m23.xy)) + _24._m23.zw) + (float2(_24._m3, _24._m4) * _18._m0.y), 0.0, 0.0).xy, level(0.0));
        float _192 = _191.x;
        float2 _200 = _167 + (in.m_6 * _164);
        float _201 = _200.x;
        float _202 = _200.y;
        float2 _213 = _133.xy + (float2(sin(_201 + _202) + (_192 * _24._m5), cos((_201 * 1.37000000476837158203125) - _202) + (_192 * _24._m6)) * mix(_24._m7, _24._m8, _154));
        _215 = float4(_213.x, _213.y, _133.z, _133.w);
        _216 = _154;
    }
    else
    {
        _215 = _133;
        _216 = 0.0;
    }
    bool _235 = _28._m0 > 0.0;
    float4 _311;
    float4 _312;
    float4 _313;
    float4 _314;
    if (_235)
    {
        _311 = float4(_30._m8[0][3], _30._m8[1][3], _30._m8[2][3], _30._m8[3][3]);
        _312 = float4(_30._m8[0][2], _30._m8[1][2], _30._m8[2][2], _30._m8[3][2]);
        _313 = float4(_30._m8[0][1], _30._m8[1][1], _30._m8[2][1], _30._m8[3][1]);
        _314 = float4(_30._m8[0][0], _30._m8[1][0], _30._m8[2][0], _30._m8[3][0]);
    }
    else
    {
        _311 = float4(_20._m8[0][3], _20._m8[1][3], _20._m8[2][3], _20._m8[3][3]);
        _312 = float4(_20._m8[0][2], _20._m8[1][2], _20._m8[2][2], _20._m8[3][2]);
        _313 = float4(_20._m8[0][1], _20._m8[1][1], _20._m8[2][1], _20._m8[3][1]);
        _314 = float4(_20._m8[0][0], _20._m8[1][0], _20._m8[2][0], _20._m8[3][0]);
    }
    float4 _320 = float4(float3((_33._m0[0]._m0 * float4(_215.xyz, 1.0)).xyz) - (_30._m11.xyz * _28._m0), 1.0) * float4x4(_314, _313, _312, _311);
    float4 _337;
    if (_235)
    {
        float4 _329 = _320;
        _329.x = _320.x * (1.0 - (_28._m1 * 2.0));
        _329.y = _320.y * (1.0 - (_28._m2 * 2.0));
        _337 = _329;
    }
    else
    {
        _337 = _320;
    }
    float4 _368;
    float4 _369;
    if (_235)
    {
        _368 = float4(_30._m2[0][1], _30._m2[1][1], _117, _117);
        _369 = float4(_30._m2[0][0], _30._m2[1][0], _117, _117);
    }
    else
    {
        _368 = float4(_20._m5[0][1], _20._m5[1][1], _117, _117);
        _369 = float4(_20._m5[0][0], _20._m5[1][0], _117, _117);
    }
    float2 _379 = float2(_337.w) / (float2(_26._m51, _26._m52) * abs(_30._m14.xy * float2x2(_369.xy, _368.xy)));
    float _390 = rsqrt(dot(_379, _379)) * ((abs(in.m_7.y) * _26._m50) * (_26._m54 + 1.0));
    float4 _400;
    if (_235)
    {
        _400 = float4(_120, _120, _120, _30._m2[3][3]);
    }
    else
    {
        _400 = float4(_120, _120, _120, _20._m5[3][3]);
    }
    float _434;
    if (_400.w == 0.0)
    {
        _434 = mix(abs(_390) * (1.0 - _26._m53), _390, abs(dot(fast::normalize((float3(1.0 / dot(float4(_33._m0[0]._m0[0][0], _33._m0[0]._m0[1][0], _33._m0[0]._m0[2][0], _33._m0[0]._m0[3][0]).xyz, float4(_33._m0[0]._m0[0][0], _33._m0[0]._m0[1][0], _33._m0[0]._m0[2][0], _33._m0[0]._m0[3][0]).xyz), 1.0 / dot(float4(_33._m0[0]._m0[0][1], _33._m0[0]._m0[1][1], _33._m0[0]._m0[2][1], _33._m0[0]._m0[3][1]).xyz, float4(_33._m0[0]._m0[0][1], _33._m0[0]._m0[1][1], _33._m0[0]._m0[2][1], _33._m0[0]._m0[3][1]).xyz), 1.0 / dot(float4(_33._m0[0]._m0[0][2], _33._m0[0]._m0[1][2], _33._m0[0]._m0[2][2], _33._m0[0]._m0[3][2]).xyz, float4(_33._m0[0]._m0[0][2], _33._m0[0]._m0[1][2], _33._m0[0]._m0[2][2], _33._m0[0]._m0[3][2]).xyz)) * in.m_4) * float3x3(float4(_33._m0[0]._m0[0][0], _33._m0[0]._m0[1][0], _33._m0[0]._m0[2][0], _33._m0[0]._m0[3][0]).xyz, float4(_33._m0[0]._m0[0][1], _33._m0[0]._m0[1][1], _33._m0[0]._m0[2][1], _33._m0[0]._m0[3][1]).xyz, float4(_33._m0[0]._m0[0][2], _33._m0[0]._m0[1][2], _33._m0[0]._m0[2][2], _33._m0[0]._m0[3][2]).xyz)), fast::normalize(_30._m11.xyz - (_33._m0[0]._m0 * _215).xyz))));
    }
    else
    {
        _434 = _390;
    }
    float _447 = (((mix(_26._m37, _26._m38, step(in.m_7.y, 0.0)) * 0.25) + _26._m3) * _26._m39) * 0.5;
    float _449 = 0.5 / _434;
    float _473;
    if (_24._m18 > 0.5)
    {
        _473 = (1.0 - (_24._m19 * _26._m39)) - (_24._m21 * _26._m39);
    }
    else
    {
        _473 = (1.0 - (_26._m8 * _26._m39)) - (_26._m4 * _26._m39);
    }
    float4 _479 = fast::clamp(_33._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float _483 = floor(in.m_7.x * 0.000244140625);
    float2 _487 = float2(_483, in.m_7.x - (4096.0 * _483)) * 0.001953125;
    float4 _500 = _337 * 0.5;
    float2 _509 = float2(_500.x, _500.y * _30._m13.x) + float2(_500.w);
    float4 _574 = _337;
    _574.y = -_337.y;
    out.gl_Position = _574;
    out.m_9 = in.m_5;
    out.m_10 = in.m_6;
    out.m_11 = float4(((_473 * 0.5) - _449) - _447, _434, (0.5 - _447) + _449, _447);
    out.m_12 = float4(((_215.xy * 2.0) - _479.xy) - _479.zw, float2(0.25) / ((float2(precise::max(_33._m0[0]._m5.y, _26._m46), precise::max(_33._m0[0]._m5.z, _26._m47)) * 0.25) + _379));
    out.m_13 = float4((float3(_30._m11.xyz) - (_33._m0[0]._m0 * _215).xyz) * float3x3(float4(_26._m19[0][0], _26._m19[1][0], _26._m19[2][0], _26._m19[3][0]).xyz, float4(_26._m19[0][1], _26._m19[1][1], _26._m19[2][1], _26._m19[3][1]).xyz, float4(_26._m19[0][2], _26._m19[1][2], _26._m19[2][2], _26._m19[3][2]).xyz), _216);
    out.m_14 = float4((_487 * _26._m57.xy) + _26._m57.zw, (_487 * _26._m58.xy) + _26._m58.zw);
    out.m_15 = float4(_509.x, _509.y, _337.z, _337.w);
    out.m_16 = float3((_26._m56 * float4(_122.xyz, 1.0)).xy, _117).xy;
    return out;
}

