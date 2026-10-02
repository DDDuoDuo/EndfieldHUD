#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _10
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

struct _14
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

struct _16
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

struct _18
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
    float4 m_9 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn2)]];
    float4 m_6 [[user(locn4)]];
    float4 m_7 [[user(locn5)]];
    float4 m_8 [[user(locn6)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _10& _11 [[buffer(0)]], constant _14& _15 [[buffer(1)]], constant _16& _17 [[buffer(2)]], constant _18& _19 [[buffer(3)]], texture2d<float> _21 [[texture(0)]], texture2d<float> _23 [[texture(1)]], texture2d<float> _25 [[texture(2)]], sampler _22 [[sampler(0)]], sampler _24 [[sampler(1)]], sampler _26 [[sampler(2)]])
{
    main0_out out = {};
    bool _92 = _15._m2 > 0.5;
    float2 _170;
    if (_92)
    {
        float _99 = _11._m0.y * _15._m16;
        float2 _105 = in.m_8.xy / float2(precise::max(in.m_8.w, 9.9999997473787516355514526367188e-05));
        float2 _118 = float2(_105.x * (_19._m14.x / precise::max(_19._m14.y, 1.0)), _105.y) * _15._m15;
        float2 _121 = _118 + float2(_99, _99 * 0.829999983310699462890625);
        float2 _126 = (_118 * 1.7000000476837158203125) + float2(_99 * (-0.9700000286102294921875), _99 * 1.309999942779541015625);
        float2 _140 = float2(sin(_121.y) + (sin(_126.y) * 0.5), cos(_121.x) + (cos(_126.x) * 0.5)) * 0.66670000553131103515625;
        float _157 = precise::max(_17._m50 - 1.0, 0.0);
        _170 = fast::clamp((float2(int2(sign(_140))) * powr(abs(_140), float2(precise::max(_15._m17, 1.0)))) * _15._m14, float2(-_157), float2(_157)) * float2(1.0 / _17._m48, 1.0 / _17._m49);
    }
    else
    {
        _170 = float2(0.0);
    }
    float2 _171 = in.m_4 + _170;
    float4 _175 = _25.sample(_26, _171);
    float _176 = _175.x;
    bool _179 = _15._m18 > 0.5;
    float _229;
    if (_179)
    {
        float2 _191 = float2(1.0 / _17._m48, 1.0 / _17._m49) * _15._m20;
        float _194 = _191.x;
        float _212 = _191.y;
        _229 = ((((_176 + _25.sample(_26, (_171 + float2(_194, 0.0))).x) + _25.sample(_26, (_171 + float2(-_194, 0.0))).x) + _25.sample(_26, (_171 + float2(0.0, _212))).x) + _25.sample(_26, (_171 + float2(0.0, -_212))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _229 = _176;
    }
    if ((_229 - in.m_5.x) < 0.0)
    {
        discard_fragment();
    }
    float _241 = (in.m_5.z - _229) * in.m_5.y;
    float _247 = (_17._m8 * _17._m39) * in.m_5.y;
    float _251 = (_17._m4 * _17._m39) * in.m_5.y;
    float3 _258 = _17._m2.xyz * in.m_3.xyz;
    float4 _273 = _23.sample(_24, (in.m_7.xy + (float2(_17._m0, _17._m1) * _19._m12.y)));
    float4 _274 = float4(_258.x, _258.y, _258.z, _17._m2.w) * _273;
    float4 _286 = _21.sample(_22, (in.m_7.zw + (float2(_17._m5, _17._m6) * _19._m12.y)));
    float4 _287 = _17._m7 * _286;
    float4 _336;
    if (_179)
    {
        float _298 = (_15._m21 * _17._m39) * in.m_5.y;
        float3 _309 = _274.xyz * _274.w;
        _336 = float4(_309.x, _309.y, _309.z, _274.w) * (1.0 - fast::clamp(((_241 - (((_15._m19 * _17._m39) * in.m_5.y) * 0.5)) + (_298 * 0.5)) / (1.0 + _298), 0.0, 1.0));
    }
    else
    {
        float _312 = _247 * 0.5;
        float3 _327 = _274.xyz * _274.w;
        float3 _331 = _287.xyz * _287.w;
        _336 = mix(float4(_327.x, _327.y, _327.z, _274.w), float4(_331.x, _331.y, _331.z, _287.w), float4(fast::clamp(_241 + _312, 0.0, 1.0) * sqrt(precise::min(1.0, _247)))) * (1.0 - fast::clamp(((_241 - _312) + (_251 * 0.5)) / (1.0 + _251), 0.0, 1.0));
    }
    if ((_336.w - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float4 _355;
    if (_92)
    {
        _355 = _336 * fast::clamp(mix(1.0, (_15._m13 >= 0.0) ? (1.0 - in.m_6.w) : in.m_6.w, abs(_15._m13)), 0.0, 1.0);
    }
    else
    {
        _355 = _336;
    }
    float4 _358 = _355 * in.m_3.w;
    _358.w = mix(_358.w, 0.0, _15._m22);
    out.m_9 = _358;
    return out;
}

