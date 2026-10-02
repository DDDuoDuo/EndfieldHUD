#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _8
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
    float4 _m9[6];
    float4x4 _m10;
    float4x4 _m11;
    float4x4 _m12;
    float4x4 _m13;
    float4 _m14;
    float4 _m15;
    float4 _m16;
    float4 _m17;
    float4 _m18;
    float4 _m19;
    float4 _m20;
    float4 _m21[8];
    float4 _m22[8];
    float4 _m23[8];
    float4 _m24[8];
    float4 _m25;
    float4 _m26;
    float4 _m27;
    float4 _m28;
    float4 _m29;
    float4 _m30;
    float4 _m31;
    float4 _m32;
    float4 _m33;
    float4 _m34;
    float4 _m35;
    float4 _m36;
    float4 _m37;
    float4 _m38[4];
    float4 _m39;
    float4 _m40;
    float4 _m41;
    float4 _m42;
    float4x4 _m43[4];
    float4 _m44;
    float4 _m45;
    float4x4 _m46;
    float4 _m47;
    float4 _m48;
    float4 _m49;
    float4x4 _m50;
    float4 _m51;
    float4 _m52;
    float4 _m53;
    float4 _m54;
    float4 _m55;
    float4x4 _m56;
    float4x4 _m57;
    float4x4 _m58;
    float4x4 _m59;
    int _m60;
    float4 _m61;
    float4 _m62;
    float4 _m63;
    float4 _m64;
    float4 _m65;
    float4 _m66;
    float4 _m67;
    float4 _m68;
    float4 _m69;
    float4 _m70;
    float4 _m71;
    float4 _m72;
    float4 _m73;
    float4 _m74;
    float4x4 _m75;
    float4 _m76;
    float4 _m77;
    float4 _m78;
    float4 _m79;
    float4 _m80;
    float4 _m81;
    float4 _m82;
    float4 _m83;
    float _m84;
    float _m85;
    float _m86;
};

struct _10
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
};

struct _12
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

struct main0_out
{
    float2 m_6 [[user(locn0)]];
    float4 m_7 [[user(locn1)]];
    float4 gl_Position [[position]];
};

struct main0_in
{
    float4 m_2 [[attribute(0)]];
    float2 m_3 [[attribute(1)]];
    float4 m_4 [[attribute(2)]];
};

vertex main0_out main0(main0_in in [[stage_in]], constant _8& _9 [[buffer(0)]], constant _10& _11 [[buffer(1)]], constant _12& _13 [[buffer(2)]])
{
    main0_out out = {};
    bool _74 = _11._m0 > 0.0;
    float4 _150;
    float4 _151;
    float4 _152;
    float4 _153;
    if (_74)
    {
        _150 = float4(_13._m8[0][3], _13._m8[1][3], _13._m8[2][3], _13._m8[3][3]);
        _151 = float4(_13._m8[0][2], _13._m8[1][2], _13._m8[2][2], _13._m8[3][2]);
        _152 = float4(_13._m8[0][1], _13._m8[1][1], _13._m8[2][1], _13._m8[3][1]);
        _153 = float4(_13._m8[0][0], _13._m8[1][0], _13._m8[2][0], _13._m8[3][0]);
    }
    else
    {
        _150 = float4(_9._m59[0][3], _9._m59[1][3], _9._m59[2][3], _9._m59[3][3]);
        _151 = float4(_9._m59[0][2], _9._m59[1][2], _9._m59[2][2], _9._m59[3][2]);
        _152 = float4(_9._m59[0][1], _9._m59[1][1], _9._m59[2][1], _9._m59[3][1]);
        _153 = float4(_9._m59[0][0], _9._m59[1][0], _9._m59[2][0], _9._m59[3][0]);
    }
    float4 _159 = float4(float3((_9._m46 * float4(in.m_2.xyz, 1.0)).xyz) - (_13._m11.xyz * _11._m0), 1.0) * float4x4(_153, _152, _151, _150);
    float4 _176;
    if (_74)
    {
        float4 _168 = _159;
        _168.x = _159.x * (1.0 - (_11._m1 * 2.0));
        _168.y = _159.y * (1.0 - (_11._m2 * 2.0));
        _176 = _168;
    }
    else
    {
        _176 = _159;
    }
    float4 _179 = _176;
    _179.y = -_176.y;
    out.gl_Position = _179;
    out.m_6 = in.m_3;
    out.m_7 = in.m_4;
    return out;
}

