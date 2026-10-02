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
    float4 _m5;
    float4 _m6;
    float4 _m7;
    float4 _m8;
};

struct _16
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

struct _18
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

struct _20
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
    float4 m_11 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn2)]];
    float4 m_6 [[user(locn4)]];
    float4 m_7 [[user(locn5)]];
    float4 m_8 [[user(locn6)]];
    float4 m_9 [[user(locn7)]];
    float4 m_10 [[user(locn8)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _12& _13 [[buffer(0)]], constant _16& _17 [[buffer(1)]], constant _18& _19 [[buffer(2)]], constant _20& _21 [[buffer(3)]], texture2d<float> _23 [[texture(0)]], texture2d<float> _25 [[texture(1)]], texture2d<float> _27 [[texture(2)]], sampler _24 [[sampler(0)]], sampler _26 [[sampler(1)]], sampler _28 [[sampler(2)]])
{
    main0_out out = {};
    bool _96 = _17._m2 > 0.5;
    float2 _174;
    if (_96)
    {
        float _103 = _13._m0.y * _17._m16;
        float2 _109 = in.m_10.xy / float2(precise::max(in.m_10.w, 9.9999997473787516355514526367188e-05));
        float2 _122 = float2(_109.x * (_21._m14.x / precise::max(_21._m14.y, 1.0)), _109.y) * _17._m15;
        float2 _125 = _122 + float2(_103, _103 * 0.829999983310699462890625);
        float2 _130 = (_122 * 1.7000000476837158203125) + float2(_103 * (-0.9700000286102294921875), _103 * 1.309999942779541015625);
        float2 _144 = float2(sin(_125.y) + (sin(_130.y) * 0.5), cos(_125.x) + (cos(_130.x) * 0.5)) * 0.66670000553131103515625;
        float _161 = precise::max(_19._m50 - 1.0, 0.0);
        _174 = fast::clamp((float2(int2(sign(_144))) * powr(abs(_144), float2(precise::max(_17._m17, 1.0)))) * _17._m14, float2(-_161), float2(_161)) * float2(1.0 / _19._m48, 1.0 / _19._m49);
    }
    else
    {
        _174 = float2(0.0);
    }
    float2 _175 = in.m_4 + _174;
    float4 _179 = _27.sample(_28, _175);
    float _180 = _179.x;
    bool _183 = _17._m18 > 0.5;
    float _233;
    if (_183)
    {
        float2 _195 = float2(1.0 / _19._m48, 1.0 / _19._m49) * _17._m20;
        float _198 = _195.x;
        float _216 = _195.y;
        _233 = ((((_180 + _27.sample(_28, (_175 + float2(_198, 0.0))).x) + _27.sample(_28, (_175 + float2(-_198, 0.0))).x) + _27.sample(_28, (_175 + float2(0.0, _216))).x) + _27.sample(_28, (_175 + float2(0.0, -_216))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _233 = _180;
    }
    float _239 = (in.m_5.z - _233) * in.m_5.y;
    float _245 = (_19._m8 * _19._m39) * in.m_5.y;
    float _249 = (_19._m4 * _19._m39) * in.m_5.y;
    float3 _256 = _19._m2.xyz * in.m_3.xyz;
    float4 _271 = _25.sample(_26, (in.m_9.xy + (float2(_19._m0, _19._m1) * _21._m12.y)));
    float4 _272 = float4(_256.x, _256.y, _256.z, _19._m2.w) * _271;
    float4 _284 = _23.sample(_24, (in.m_9.zw + (float2(_19._m5, _19._m6) * _21._m12.y)));
    float4 _285 = _19._m7 * _284;
    float4 _334;
    if (_183)
    {
        float _296 = (_17._m21 * _19._m39) * in.m_5.y;
        float3 _307 = _272.xyz * _272.w;
        _334 = float4(_307.x, _307.y, _307.z, _272.w) * (1.0 - fast::clamp(((_239 - (((_17._m19 * _19._m39) * in.m_5.y) * 0.5)) + (_296 * 0.5)) / (1.0 + _296), 0.0, 1.0));
    }
    else
    {
        float _310 = _245 * 0.5;
        float3 _325 = _272.xyz * _272.w;
        float3 _329 = _285.xyz * _285.w;
        _334 = mix(float4(_325.x, _325.y, _325.z, _272.w), float4(_329.x, _329.y, _329.z, _285.w), float4(fast::clamp(_239 + _310, 0.0, 1.0) * sqrt(precise::min(1.0, _245)))) * (1.0 - fast::clamp(((_239 - _310) + (_249 * 0.5)) / (1.0 + _249), 0.0, 1.0));
    }
    float4 _339 = _27.sample(_28, in.m_7.xy);
    float4 _352 = _334 + ((in.m_8 * fast::clamp((_339.x * in.m_7.z) - in.m_7.w, 0.0, 1.0)) * (1.0 - _334.w));
    if ((_352.w - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float4 _371;
    if (_96)
    {
        _371 = _352 * fast::clamp(mix(1.0, (_17._m13 >= 0.0) ? (1.0 - in.m_6.w) : in.m_6.w, abs(_17._m13)), 0.0, 1.0);
    }
    else
    {
        _371 = _352;
    }
    float4 _374 = _371 * in.m_3.w;
    _374.w = mix(_374.w, 0.0, _17._m22);
    out.m_11 = _374;
    return out;
}

