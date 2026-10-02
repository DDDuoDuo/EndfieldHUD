#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _18
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

struct _20
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

struct _24
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

struct _26
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

struct _28
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _30
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

struct _33
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _32
{
    _33 _m0[1];
};

constant float _125 = {};
constant float _128 = {};

struct main0_out
{
    float4 m_9 [[user(locn0)]];
    float2 m_10 [[user(locn1)]];
    float4 m_11 [[user(locn2)]];
    float4 m_13 [[user(locn4)]];
    float4 m_14 [[user(locn5)]];
    float4 m_15 [[user(locn6)]];
    float4 m_16 [[user(locn7)]];
    float4 m_17 [[user(locn8)]];
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

vertex main0_out main0(main0_in in [[stage_in]], constant _18& _19 [[buffer(0)]], constant _20& _21 [[buffer(1)]], constant _24& _25 [[buffer(2)]], constant _26& _27 [[buffer(3)]], constant _28& _29 [[buffer(4)]], constant _30& _31 [[buffer(5)]], constant _32& _34 [[buffer(6)]], texture2d<float> _36 [[texture(0)]], sampler _37 [[sampler(0)]])
{
    main0_out out = {};
    float4 _130 = in.m_3;
    float4 _141 = _130;
    _141.x = _130.x + _27._m42;
    _141.y = _130.y + _27._m43;
    float4 _223;
    float _224;
    if (_25._m2 > 0.5)
    {
        float _162 = fast::clamp((in.m_3.x - _25._m11) / precise::max(9.9999997473787516355514526367188e-05, _25._m12 - _25._m11), 0.0, 1.0);
        float _172 = mix(_25._m9, _25._m10, _162);
        float2 _175 = _130.xy * (_172 * 0.00999999977648258209228515625);
        float4 _199 = _36.sample(_37, float4(((_175 + (in.m_6 * _25._m23.xy)) + _25._m23.zw) + (float2(_25._m3, _25._m4) * _19._m0.y), 0.0, 0.0).xy, level(0.0));
        float _200 = _199.x;
        float2 _208 = _175 + (in.m_6 * _172);
        float _209 = _208.x;
        float _210 = _208.y;
        float2 _221 = _141.xy + (float2(sin(_209 + _210) + (_200 * _25._m5), cos((_209 * 1.37000000476837158203125) - _210) + (_200 * _25._m6)) * mix(_25._m7, _25._m8, _162));
        _223 = float4(_221.x, _221.y, _141.z, _141.w);
        _224 = _162;
    }
    else
    {
        _223 = _141;
        _224 = 0.0;
    }
    bool _243 = _29._m0 > 0.0;
    float4 _319;
    float4 _320;
    float4 _321;
    float4 _322;
    if (_243)
    {
        _319 = float4(_31._m8[0][3], _31._m8[1][3], _31._m8[2][3], _31._m8[3][3]);
        _320 = float4(_31._m8[0][2], _31._m8[1][2], _31._m8[2][2], _31._m8[3][2]);
        _321 = float4(_31._m8[0][1], _31._m8[1][1], _31._m8[2][1], _31._m8[3][1]);
        _322 = float4(_31._m8[0][0], _31._m8[1][0], _31._m8[2][0], _31._m8[3][0]);
    }
    else
    {
        _319 = float4(_21._m8[0][3], _21._m8[1][3], _21._m8[2][3], _21._m8[3][3]);
        _320 = float4(_21._m8[0][2], _21._m8[1][2], _21._m8[2][2], _21._m8[3][2]);
        _321 = float4(_21._m8[0][1], _21._m8[1][1], _21._m8[2][1], _21._m8[3][1]);
        _322 = float4(_21._m8[0][0], _21._m8[1][0], _21._m8[2][0], _21._m8[3][0]);
    }
    float4 _328 = float4(float3((_34._m0[0]._m0 * float4(_223.xyz, 1.0)).xyz) - (_31._m11.xyz * _29._m0), 1.0) * float4x4(_322, _321, _320, _319);
    float4 _345;
    if (_243)
    {
        float4 _337 = _328;
        _337.x = _328.x * (1.0 - (_29._m1 * 2.0));
        _337.y = _328.y * (1.0 - (_29._m2 * 2.0));
        _345 = _337;
    }
    else
    {
        _345 = _328;
    }
    float4 _376;
    float4 _377;
    if (_243)
    {
        _376 = float4(_31._m2[0][1], _31._m2[1][1], _125, _125);
        _377 = float4(_31._m2[0][0], _31._m2[1][0], _125, _125);
    }
    else
    {
        _376 = float4(_21._m5[0][1], _21._m5[1][1], _125, _125);
        _377 = float4(_21._m5[0][0], _21._m5[1][0], _125, _125);
    }
    float2 _387 = float2(_345.w) / (float2(_27._m51, _27._m52) * abs(_31._m14.xy * float2x2(_377.xy, _376.xy)));
    float _398 = rsqrt(dot(_387, _387)) * ((abs(in.m_7.y) * _27._m50) * (_27._m54 + 1.0));
    float4 _408;
    if (_243)
    {
        _408 = float4(_128, _128, _128, _31._m2[3][3]);
    }
    else
    {
        _408 = float4(_128, _128, _128, _21._m5[3][3]);
    }
    float _442;
    if (_408.w == 0.0)
    {
        _442 = mix(abs(_398) * (1.0 - _27._m53), _398, abs(dot(fast::normalize((float3(1.0 / dot(float4(_34._m0[0]._m0[0][0], _34._m0[0]._m0[1][0], _34._m0[0]._m0[2][0], _34._m0[0]._m0[3][0]).xyz, float4(_34._m0[0]._m0[0][0], _34._m0[0]._m0[1][0], _34._m0[0]._m0[2][0], _34._m0[0]._m0[3][0]).xyz), 1.0 / dot(float4(_34._m0[0]._m0[0][1], _34._m0[0]._m0[1][1], _34._m0[0]._m0[2][1], _34._m0[0]._m0[3][1]).xyz, float4(_34._m0[0]._m0[0][1], _34._m0[0]._m0[1][1], _34._m0[0]._m0[2][1], _34._m0[0]._m0[3][1]).xyz), 1.0 / dot(float4(_34._m0[0]._m0[0][2], _34._m0[0]._m0[1][2], _34._m0[0]._m0[2][2], _34._m0[0]._m0[3][2]).xyz, float4(_34._m0[0]._m0[0][2], _34._m0[0]._m0[1][2], _34._m0[0]._m0[2][2], _34._m0[0]._m0[3][2]).xyz)) * in.m_4) * float3x3(float4(_34._m0[0]._m0[0][0], _34._m0[0]._m0[1][0], _34._m0[0]._m0[2][0], _34._m0[0]._m0[3][0]).xyz, float4(_34._m0[0]._m0[0][1], _34._m0[0]._m0[1][1], _34._m0[0]._m0[2][1], _34._m0[0]._m0[3][1]).xyz, float4(_34._m0[0]._m0[0][2], _34._m0[0]._m0[1][2], _34._m0[0]._m0[2][2], _34._m0[0]._m0[3][2]).xyz)), fast::normalize(_31._m11.xyz - (_34._m0[0]._m0 * _223).xyz))));
    }
    else
    {
        _442 = _398;
    }
    float _455 = (((mix(_27._m37, _27._m38, step(in.m_7.y, 0.0)) * 0.25) + _27._m3) * _27._m39) * 0.5;
    float _456 = 0.5 - _455;
    float _457 = 0.5 / _442;
    float _481;
    if (_25._m18 > 0.5)
    {
        _481 = (1.0 - (_25._m19 * _27._m39)) - (_25._m21 * _27._m39);
    }
    else
    {
        _481 = (1.0 - (_27._m8 * _27._m39)) - (_27._m4 * _27._m39);
    }
    float3 _489 = _27._m26.xyz * _27._m26.w;
    float _498 = _442 / (1.0 + ((_27._m30 * _27._m41) * _442));
    float4 _526 = fast::clamp(_34._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float _530 = floor(in.m_7.x * 0.000244140625);
    float2 _534 = float2(_530, in.m_7.x - (4096.0 * _530)) * 0.001953125;
    float4 _547 = _345 * 0.5;
    float2 _556 = float2(_547.x, _547.y * _31._m13.x) + float2(_547.w);
    float4 _616 = _345;
    _616.y = -_345.y;
    out.gl_Position = _616;
    out.m_9 = in.m_5;
    out.m_10 = in.m_6;
    out.m_11 = float4(((_481 * 0.5) - _457) - _455, _442, _456 + _457, _455);
    out.m_13 = float4((float3(_31._m11.xyz) - (_34._m0[0]._m0 * _223).xyz) * float3x3(float4(_27._m19[0][0], _27._m19[1][0], _27._m19[2][0], _27._m19[3][0]).xyz, float4(_27._m19[0][1], _27._m19[1][1], _27._m19[2][1], _27._m19[3][1]).xyz, float4(_27._m19[0][2], _27._m19[1][2], _27._m19[2][2], _27._m19[3][2]).xyz), _224);
    out.m_14 = float4(in.m_6 + float2(((-(_27._m27 * _27._m41)) * _27._m50) / _27._m48, ((-(_27._m28 * _27._m41)) * _27._m50) / _27._m49), _498, ((_456 * _498) - 0.5) - (((_27._m29 * _27._m41) * 0.5) * _498));
    out.m_15 = float4(_489.x, _489.y, _489.z, _27._m26.w);
    out.m_16 = float4((_534 * _27._m57.xy) + _27._m57.zw, (_534 * _27._m58.xy) + _27._m58.zw);
    out.m_17 = float4(_556.x, _556.y, _345.z, _345.w);
    return out;
}

