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
};

constant float _70 = {};

struct main0_out
{
    float4 m_7 [[user(locn0)]];
    float2 m_8 [[user(locn1)]];
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
    bool _93 = _15._m0 > 0.0;
    float4 _169;
    float4 _170;
    float4 _171;
    float4 _172;
    if (_93)
    {
        _169 = float4(_17._m8[0][3], _17._m8[1][3], _17._m8[2][3], _17._m8[3][3]);
        _170 = float4(_17._m8[0][2], _17._m8[1][2], _17._m8[2][2], _17._m8[3][2]);
        _171 = float4(_17._m8[0][1], _17._m8[1][1], _17._m8[2][1], _17._m8[3][1]);
        _172 = float4(_17._m8[0][0], _17._m8[1][0], _17._m8[2][0], _17._m8[3][0]);
    }
    else
    {
        _169 = float4(_13._m8[0][3], _13._m8[1][3], _13._m8[2][3], _13._m8[3][3]);
        _170 = float4(_13._m8[0][2], _13._m8[1][2], _13._m8[2][2], _13._m8[3][2]);
        _171 = float4(_13._m8[0][1], _13._m8[1][1], _13._m8[2][1], _13._m8[3][1]);
        _172 = float4(_13._m8[0][0], _13._m8[1][0], _13._m8[2][0], _13._m8[3][0]);
    }
    float4 _178 = float4(float3((_20._m0[0]._m0 * float4(in.m_3.xyz, 1.0)).xyz) - (_17._m11.xyz * _15._m0), 1.0) * float4x4(_172, _171, _170, _169);
    float4 _195;
    if (_93)
    {
        float4 _187 = _178;
        _187.x = _178.x * (1.0 - (_15._m1 * 2.0));
        _187.y = _178.y * (1.0 - (_15._m2 * 2.0));
        _195 = _187;
    }
    else
    {
        _195 = _178;
    }
    float4 _196 = _195 * 0.5;
    float2 _205 = float2(_196.x, _196.y * _17._m13.x) + float2(_196.w);
    float4 _232;
    float4 _233;
    if (_93)
    {
        _232 = float4(_17._m2[0][1], _17._m2[1][1], _70, _70);
        _233 = float4(_17._m2[0][0], _17._m2[1][0], _70, _70);
    }
    else
    {
        _232 = float4(_13._m5[0][1], _13._m5[1][1], _70, _70);
        _233 = float4(_13._m5[0][0], _13._m5[1][0], _70, _70);
    }
    float4 _245 = fast::clamp(_20._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float4 _274 = in.m_4 * _22._m5;
    bool _277 = _22._m9 != 0.0;
    float3 _282 = _274.xyz * (_277 ? _22._m10 : 1.0);
    float4 _283 = float4(_282.x, _282.y, _282.z, _274.w);
    _283.w = _274.w * (_277 ? _22._m11 : 1.0);
    float4 _292 = _195;
    _292.y = -_195.y;
    out.gl_Position = _292;
    out.m_7 = _283;
    out.m_8 = (in.m_5 * _22._m6.xy) + _22._m6.zw;
    return out;
}

