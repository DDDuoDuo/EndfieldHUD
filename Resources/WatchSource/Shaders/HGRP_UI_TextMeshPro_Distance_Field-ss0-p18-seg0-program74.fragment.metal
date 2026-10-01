#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _13
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

struct _17
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

struct _19
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

struct _21
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
    float4 m_12 [[color(0)]];
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
    float2 m_11 [[user(locn9)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _13& _14 [[buffer(0)]], constant _17& _18 [[buffer(1)]], constant _19& _20 [[buffer(2)]], constant _21& _22 [[buffer(3)]], texture2d<float> _24 [[texture(0)]], texture2d<float> _26 [[texture(1)]], texture2d<float> _28 [[texture(2)]], texture2d<float> _30 [[texture(3)]], sampler _25 [[sampler(0)]], sampler _27 [[sampler(1)]], sampler _29 [[sampler(2)]], sampler _31 [[sampler(3)]])
{
    main0_out out = {};
    bool _101 = _18._m2 > 0.5;
    float2 _179;
    if (_101)
    {
        float _108 = _14._m0.y * _18._m16;
        float2 _114 = in.m_10.xy / float2(precise::max(in.m_10.w, 9.9999997473787516355514526367188e-05));
        float2 _127 = float2(_114.x * (_22._m14.x / precise::max(_22._m14.y, 1.0)), _114.y) * _18._m15;
        float2 _130 = _127 + float2(_108, _108 * 0.829999983310699462890625);
        float2 _135 = (_127 * 1.7000000476837158203125) + float2(_108 * (-0.9700000286102294921875), _108 * 1.309999942779541015625);
        float2 _149 = float2(sin(_130.y) + (sin(_135.y) * 0.5), cos(_130.x) + (cos(_135.x) * 0.5)) * 0.66670000553131103515625;
        float _166 = precise::max(_20._m50 - 1.0, 0.0);
        _179 = fast::clamp((float2(int2(sign(_149))) * powr(abs(_149), float2(precise::max(_18._m17, 1.0)))) * _18._m14, float2(-_166), float2(_166)) * float2(1.0 / _20._m48, 1.0 / _20._m49);
    }
    else
    {
        _179 = float2(0.0);
    }
    float2 _180 = in.m_4 + _179;
    float4 _184 = _28.sample(_29, _180);
    float _185 = _184.x;
    bool _188 = _18._m18 > 0.5;
    float _238;
    if (_188)
    {
        float2 _200 = float2(1.0 / _20._m48, 1.0 / _20._m49) * _18._m20;
        float _203 = _200.x;
        float _221 = _200.y;
        _238 = ((((_185 + _28.sample(_29, (_180 + float2(_203, 0.0))).x) + _28.sample(_29, (_180 + float2(-_203, 0.0))).x) + _28.sample(_29, (_180 + float2(0.0, _221))).x) + _28.sample(_29, (_180 + float2(0.0, -_221))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _238 = _185;
    }
    float _244 = (in.m_5.z - _238) * in.m_5.y;
    float _250 = (_20._m8 * _20._m39) * in.m_5.y;
    float _254 = (_20._m4 * _20._m39) * in.m_5.y;
    float3 _261 = _20._m2.xyz * in.m_3.xyz;
    float4 _276 = _26.sample(_27, (in.m_9.xy + (float2(_20._m0, _20._m1) * _22._m12.y)));
    float4 _277 = float4(_261.x, _261.y, _261.z, _20._m2.w) * _276;
    float4 _289 = _24.sample(_25, (in.m_9.zw + (float2(_20._m5, _20._m6) * _22._m12.y)));
    float4 _290 = _20._m7 * _289;
    float4 _339;
    if (_188)
    {
        float _301 = (_18._m21 * _20._m39) * in.m_5.y;
        float3 _312 = _277.xyz * _277.w;
        _339 = float4(_312.x, _312.y, _312.z, _277.w) * (1.0 - fast::clamp(((_244 - (((_18._m19 * _20._m39) * in.m_5.y) * 0.5)) + (_301 * 0.5)) / (1.0 + _301), 0.0, 1.0));
    }
    else
    {
        float _315 = _250 * 0.5;
        float3 _330 = _277.xyz * _277.w;
        float3 _334 = _290.xyz * _290.w;
        _339 = mix(float4(_330.x, _330.y, _330.z, _277.w), float4(_334.x, _334.y, _334.z, _290.w), float4(fast::clamp(_244 + _315, 0.0, 1.0) * sqrt(precise::min(1.0, _250)))) * (1.0 - fast::clamp(((_244 - _315) + (_254 * 0.5)) / (1.0 + _254), 0.0, 1.0));
    }
    float4 _344 = _28.sample(_29, in.m_7.xy);
    float4 _357 = _339 + ((in.m_8 * fast::clamp((_344.x * in.m_7.z) - in.m_7.w, 0.0, 1.0)) * (1.0 - _339.w));
    if ((_357.w - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float4 _372 = _30.sample(_31, ((in.m_11 * _20._m55.xy) + _20._m55.zw));
    float4 _404;
    if (_101)
    {
        _404 = _357 * fast::clamp(mix(1.0, (_18._m13 >= 0.0) ? (1.0 - in.m_6.w) : in.m_6.w, abs(_18._m13)), 0.0, 1.0);
    }
    else
    {
        _404 = _357;
    }
    float4 _405 = _404 * (in.m_3.w * fast::clamp((_372.w * (step(in.m_11.x, 1.0) * step(-in.m_11.x, 0.0))) * (step(in.m_11.y, 1.0) * step(-in.m_11.y, 0.0)), 0.0, 1.0));
    _405.w = mix(_405.w, 0.0, _18._m22);
    out.m_12 = _405;
    return out;
}

