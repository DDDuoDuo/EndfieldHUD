#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

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

struct _14
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _16
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

struct _19
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _18
{
    _19 _m0[1];
};

struct _21
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
    float _m15;
    float4 _m16;
    float4 _m17;
    float4 _m18;
    float _m19;
    float _m20;
    float _m21;
    float _m22;
    float _m23;
    float _m24;
    float _m25;
    float _m26;
    float _m27;
    float _m28;
    float4 _m29;
    float4 _m30;
    float4 _m31;
    float4 _m32;
    float4 _m33;
    float4 _m34;
    float4 _m35;
    float _m36;
    float _m37;
    float _m38;
    float4 _m39;
    float4 _m40;
    float4 _m41;
};

constant float _69 = {};

struct main0_out
{
    float4 m_7 [[user(locn0)]];
    float4 m_8 [[user(locn1)]];
    float4 m_10 [[user(locn3)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float4 m_3 [[attribute(0)]];
    float4 m_4 [[attribute(1)]];
    float2 m_5 [[attribute(2)]];
};

vertex main0_out main0(main0_in in [[stage_in]], constant _12& _13 [[buffer(0)]], constant _14& _15 [[buffer(1)]], constant _16& _17 [[buffer(2)]], constant _18& _20 [[buffer(3)]], constant _21& _22 [[buffer(4)]])
{
    main0_out out = {};
    float4 _79 = float4(in.m_3.xyz, 1.0);
    bool _92 = _15._m0 > 0.0;
    float4 _168;
    float4 _169;
    float4 _170;
    float4 _171;
    if (_92)
    {
        _168 = float4(_17._m8[0][3], _17._m8[1][3], _17._m8[2][3], _17._m8[3][3]);
        _169 = float4(_17._m8[0][2], _17._m8[1][2], _17._m8[2][2], _17._m8[3][2]);
        _170 = float4(_17._m8[0][1], _17._m8[1][1], _17._m8[2][1], _17._m8[3][1]);
        _171 = float4(_17._m8[0][0], _17._m8[1][0], _17._m8[2][0], _17._m8[3][0]);
    }
    else
    {
        _168 = float4(_13._m8[0][3], _13._m8[1][3], _13._m8[2][3], _13._m8[3][3]);
        _169 = float4(_13._m8[0][2], _13._m8[1][2], _13._m8[2][2], _13._m8[3][2]);
        _170 = float4(_13._m8[0][1], _13._m8[1][1], _13._m8[2][1], _13._m8[3][1]);
        _171 = float4(_13._m8[0][0], _13._m8[1][0], _13._m8[2][0], _13._m8[3][0]);
    }
    float4 _177 = float4(float3((_20._m0[0]._m0 * _79).xyz) - (_17._m11.xyz * _15._m0), 1.0) * float4x4(_171, _170, _169, _168);
    float4 _194;
    if (_92)
    {
        float4 _186 = _177;
        _186.x = _177.x * (1.0 - (_15._m1 * 2.0));
        _186.y = _177.y * (1.0 - (_15._m2 * 2.0));
        _194 = _186;
    }
    else
    {
        _194 = _177;
    }
    float4 _195 = _194 * 0.5;
    float2 _204 = float2(_195.x, _195.y * _17._m13.x) + float2(_195.w);
    float4 _231;
    float4 _232;
    if (_92)
    {
        _231 = float4(_17._m2[0][1], _17._m2[1][1], _69, _69);
        _232 = float4(_17._m2[0][0], _17._m2[1][0], _69, _69);
    }
    else
    {
        _231 = float4(_13._m5[0][1], _13._m5[1][1], _69, _69);
        _232 = float4(_13._m5[0][0], _13._m5[1][0], _69, _69);
    }
    float4 _244 = fast::clamp(_20._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _250 = (in.m_5 * _22._m6.xy) + _22._m6.zw;
    float3 _256 = float3((_22._m1 * _79).xy, _69);
    float4 _280 = in.m_4 * _22._m5;
    bool _283 = _22._m9 != 0.0;
    float3 _288 = _280.xyz * (_283 ? _22._m10 : 1.0);
    float4 _289 = float4(_288.x, _288.y, _288.z, _280.w);
    _289.w = _280.w * (_283 ? _22._m11 : 1.0);
    float4 _298 = _194;
    _298.y = -_194.y;
    out.gl_Position = _298;
    out.m_7 = _289;
    out.m_8 = float4(_250.x, _250.y, _256.x, _256.y);
    out.m_10 = float4(((in.m_3.xy * 2.0) - _244.xy) - _244.zw, float2(0.25) / ((float2(_20._m0[0]._m5.y, _20._m0[0]._m5.z) * 0.25) + abs(float2(_194.w) / abs(_17._m14.xy * float2x2(_232.xy, _231.xy)))));
    return out;
}

