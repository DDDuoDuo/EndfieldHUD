#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _19
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

struct _21
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

struct _25
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

struct _27
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

struct _29
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _31
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

struct _34
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _33
{
    _34 _m0[1];
};

constant float _127 = {};
constant float _130 = {};

struct main0_out
{
    float4 m_9 [[user(locn0)]];
    float2 m_10 [[user(locn1)]];
    float4 m_11 [[user(locn2)]];
    float4 m_12 [[user(locn3)]];
    float4 m_13 [[user(locn4)]];
    float4 m_14 [[user(locn5)]];
    float4 m_15 [[user(locn6)]];
    float4 m_16 [[user(locn7)]];
    float4 m_17 [[user(locn8)]];
    float2 m_18 [[user(locn9)]];
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

vertex main0_out main0(main0_in in [[stage_in]], constant _19& _20 [[buffer(0)]], constant _21& _22 [[buffer(1)]], constant _25& _26 [[buffer(2)]], constant _27& _28 [[buffer(3)]], constant _29& _30 [[buffer(4)]], constant _31& _32 [[buffer(5)]], constant _33& _35 [[buffer(6)]], texture2d<float> _37 [[texture(0)]], sampler _38 [[sampler(0)]])
{
    main0_out out = {};
    float4 _132 = in.m_3;
    float4 _143 = _132;
    _143.x = _132.x + _28._m42;
    _143.y = _132.y + _28._m43;
    float4 _225;
    float _226;
    if (_26._m2 > 0.5)
    {
        float _164 = fast::clamp((in.m_3.x - _26._m11) / precise::max(9.9999997473787516355514526367188e-05, _26._m12 - _26._m11), 0.0, 1.0);
        float _174 = mix(_26._m9, _26._m10, _164);
        float2 _177 = _132.xy * (_174 * 0.00999999977648258209228515625);
        float4 _201 = _37.sample(_38, float4(((_177 + (in.m_6 * _26._m23.xy)) + _26._m23.zw) + (float2(_26._m3, _26._m4) * _20._m0.y), 0.0, 0.0).xy, level(0.0));
        float _202 = _201.x;
        float2 _210 = _177 + (in.m_6 * _174);
        float _211 = _210.x;
        float _212 = _210.y;
        float2 _223 = _143.xy + (float2(sin(_211 + _212) + (_202 * _26._m5), cos((_211 * 1.37000000476837158203125) - _212) + (_202 * _26._m6)) * mix(_26._m7, _26._m8, _164));
        _225 = float4(_223.x, _223.y, _143.z, _143.w);
        _226 = _164;
    }
    else
    {
        _225 = _143;
        _226 = 0.0;
    }
    bool _245 = _30._m0 > 0.0;
    float4 _321;
    float4 _322;
    float4 _323;
    float4 _324;
    if (_245)
    {
        _321 = float4(_32._m8[0][3], _32._m8[1][3], _32._m8[2][3], _32._m8[3][3]);
        _322 = float4(_32._m8[0][2], _32._m8[1][2], _32._m8[2][2], _32._m8[3][2]);
        _323 = float4(_32._m8[0][1], _32._m8[1][1], _32._m8[2][1], _32._m8[3][1]);
        _324 = float4(_32._m8[0][0], _32._m8[1][0], _32._m8[2][0], _32._m8[3][0]);
    }
    else
    {
        _321 = float4(_22._m8[0][3], _22._m8[1][3], _22._m8[2][3], _22._m8[3][3]);
        _322 = float4(_22._m8[0][2], _22._m8[1][2], _22._m8[2][2], _22._m8[3][2]);
        _323 = float4(_22._m8[0][1], _22._m8[1][1], _22._m8[2][1], _22._m8[3][1]);
        _324 = float4(_22._m8[0][0], _22._m8[1][0], _22._m8[2][0], _22._m8[3][0]);
    }
    float4 _330 = float4(float3((_35._m0[0]._m0 * float4(_225.xyz, 1.0)).xyz) - (_32._m11.xyz * _30._m0), 1.0) * float4x4(_324, _323, _322, _321);
    float4 _347;
    if (_245)
    {
        float4 _339 = _330;
        _339.x = _330.x * (1.0 - (_30._m1 * 2.0));
        _339.y = _330.y * (1.0 - (_30._m2 * 2.0));
        _347 = _339;
    }
    else
    {
        _347 = _330;
    }
    float4 _378;
    float4 _379;
    if (_245)
    {
        _378 = float4(_32._m2[0][1], _32._m2[1][1], _127, _127);
        _379 = float4(_32._m2[0][0], _32._m2[1][0], _127, _127);
    }
    else
    {
        _378 = float4(_22._m5[0][1], _22._m5[1][1], _127, _127);
        _379 = float4(_22._m5[0][0], _22._m5[1][0], _127, _127);
    }
    float2 _389 = float2(_347.w) / (float2(_28._m51, _28._m52) * abs(_32._m14.xy * float2x2(_379.xy, _378.xy)));
    float _400 = rsqrt(dot(_389, _389)) * ((abs(in.m_7.y) * _28._m50) * (_28._m54 + 1.0));
    float4 _410;
    if (_245)
    {
        _410 = float4(_130, _130, _130, _32._m2[3][3]);
    }
    else
    {
        _410 = float4(_130, _130, _130, _22._m5[3][3]);
    }
    float _444;
    if (_410.w == 0.0)
    {
        _444 = mix(abs(_400) * (1.0 - _28._m53), _400, abs(dot(fast::normalize((float3(1.0 / dot(float4(_35._m0[0]._m0[0][0], _35._m0[0]._m0[1][0], _35._m0[0]._m0[2][0], _35._m0[0]._m0[3][0]).xyz, float4(_35._m0[0]._m0[0][0], _35._m0[0]._m0[1][0], _35._m0[0]._m0[2][0], _35._m0[0]._m0[3][0]).xyz), 1.0 / dot(float4(_35._m0[0]._m0[0][1], _35._m0[0]._m0[1][1], _35._m0[0]._m0[2][1], _35._m0[0]._m0[3][1]).xyz, float4(_35._m0[0]._m0[0][1], _35._m0[0]._m0[1][1], _35._m0[0]._m0[2][1], _35._m0[0]._m0[3][1]).xyz), 1.0 / dot(float4(_35._m0[0]._m0[0][2], _35._m0[0]._m0[1][2], _35._m0[0]._m0[2][2], _35._m0[0]._m0[3][2]).xyz, float4(_35._m0[0]._m0[0][2], _35._m0[0]._m0[1][2], _35._m0[0]._m0[2][2], _35._m0[0]._m0[3][2]).xyz)) * in.m_4) * float3x3(float4(_35._m0[0]._m0[0][0], _35._m0[0]._m0[1][0], _35._m0[0]._m0[2][0], _35._m0[0]._m0[3][0]).xyz, float4(_35._m0[0]._m0[0][1], _35._m0[0]._m0[1][1], _35._m0[0]._m0[2][1], _35._m0[0]._m0[3][1]).xyz, float4(_35._m0[0]._m0[0][2], _35._m0[0]._m0[1][2], _35._m0[0]._m0[2][2], _35._m0[0]._m0[3][2]).xyz)), fast::normalize(_32._m11.xyz - (_35._m0[0]._m0 * _225).xyz))));
    }
    else
    {
        _444 = _400;
    }
    float _457 = (((mix(_28._m37, _28._m38, step(in.m_7.y, 0.0)) * 0.25) + _28._m3) * _28._m39) * 0.5;
    float _458 = 0.5 - _457;
    float _459 = 0.5 / _444;
    float _483;
    if (_26._m18 > 0.5)
    {
        _483 = (1.0 - (_26._m19 * _28._m39)) - (_26._m21 * _28._m39);
    }
    else
    {
        _483 = (1.0 - (_28._m8 * _28._m39)) - (_28._m4 * _28._m39);
    }
    float3 _491 = _28._m26.xyz * _28._m26.w;
    float _500 = _444 / (1.0 + ((_28._m30 * _28._m41) * _444));
    float4 _528 = fast::clamp(_35._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float _532 = floor(in.m_7.x * 0.000244140625);
    float2 _536 = float2(_532, in.m_7.x - (4096.0 * _532)) * 0.001953125;
    float4 _549 = _347 * 0.5;
    float2 _558 = float2(_549.x, _549.y * _32._m13.x) + float2(_549.w);
    float4 _627 = _347;
    _627.y = -_347.y;
    out.gl_Position = _627;
    out.m_9 = in.m_5;
    out.m_10 = in.m_6;
    out.m_11 = float4(((_483 * 0.5) - _459) - _457, _444, _458 + _459, _457);
    out.m_12 = float4(((_225.xy * 2.0) - _528.xy) - _528.zw, float2(0.25) / ((float2(precise::max(_35._m0[0]._m5.y, _28._m46), precise::max(_35._m0[0]._m5.z, _28._m47)) * 0.25) + _389));
    out.m_13 = float4((float3(_32._m11.xyz) - (_35._m0[0]._m0 * _225).xyz) * float3x3(float4(_28._m19[0][0], _28._m19[1][0], _28._m19[2][0], _28._m19[3][0]).xyz, float4(_28._m19[0][1], _28._m19[1][1], _28._m19[2][1], _28._m19[3][1]).xyz, float4(_28._m19[0][2], _28._m19[1][2], _28._m19[2][2], _28._m19[3][2]).xyz), _226);
    out.m_14 = float4(in.m_6 + float2(((-(_28._m27 * _28._m41)) * _28._m50) / _28._m48, ((-(_28._m28 * _28._m41)) * _28._m50) / _28._m49), _500, ((_458 * _500) - 0.5) - (((_28._m29 * _28._m41) * 0.5) * _500));
    out.m_15 = float4(_491.x, _491.y, _491.z, _28._m26.w);
    out.m_16 = float4((_536 * _28._m57.xy) + _28._m57.zw, (_536 * _28._m58.xy) + _28._m58.zw);
    out.m_17 = float4(_558.x, _558.y, _347.z, _347.w);
    out.m_18 = float3((_28._m56 * float4(_132.xyz, 1.0)).xy, _127).xy;
    return out;
}

