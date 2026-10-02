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
    bool _100 = _18._m2 > 0.5;
    float2 _178;
    if (_100)
    {
        float _107 = _14._m0.y * _18._m16;
        float2 _113 = in.m_10.xy / float2(precise::max(in.m_10.w, 9.9999997473787516355514526367188e-05));
        float2 _126 = float2(_113.x * (_22._m14.x / precise::max(_22._m14.y, 1.0)), _113.y) * _18._m15;
        float2 _129 = _126 + float2(_107, _107 * 0.829999983310699462890625);
        float2 _134 = (_126 * 1.7000000476837158203125) + float2(_107 * (-0.9700000286102294921875), _107 * 1.309999942779541015625);
        float2 _148 = float2(sin(_129.y) + (sin(_134.y) * 0.5), cos(_129.x) + (cos(_134.x) * 0.5)) * 0.66670000553131103515625;
        float _165 = precise::max(_20._m50 - 1.0, 0.0);
        _178 = fast::clamp((float2(int2(sign(_148))) * powr(abs(_148), float2(precise::max(_18._m17, 1.0)))) * _18._m14, float2(-_165), float2(_165)) * float2(1.0 / _20._m48, 1.0 / _20._m49);
    }
    else
    {
        _178 = float2(0.0);
    }
    float2 _179 = in.m_4 + _178;
    float4 _183 = _28.sample(_29, _179);
    float _184 = _183.x;
    bool _187 = _18._m18 > 0.5;
    float _237;
    if (_187)
    {
        float2 _199 = float2(1.0 / _20._m48, 1.0 / _20._m49) * _18._m20;
        float _202 = _199.x;
        float _220 = _199.y;
        _237 = ((((_184 + _28.sample(_29, (_179 + float2(_202, 0.0))).x) + _28.sample(_29, (_179 + float2(-_202, 0.0))).x) + _28.sample(_29, (_179 + float2(0.0, _220))).x) + _28.sample(_29, (_179 + float2(0.0, -_220))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _237 = _184;
    }
    float _243 = (in.m_5.z - _237) * in.m_5.y;
    float _249 = (_20._m8 * _20._m39) * in.m_5.y;
    float _253 = (_20._m4 * _20._m39) * in.m_5.y;
    float3 _260 = _20._m2.xyz * in.m_3.xyz;
    float4 _275 = _26.sample(_27, (in.m_9.xy + (float2(_20._m0, _20._m1) * _22._m12.y)));
    float4 _276 = float4(_260.x, _260.y, _260.z, _20._m2.w) * _275;
    float4 _288 = _24.sample(_25, (in.m_9.zw + (float2(_20._m5, _20._m6) * _22._m12.y)));
    float4 _289 = _20._m7 * _288;
    float4 _338;
    if (_187)
    {
        float _300 = (_18._m21 * _20._m39) * in.m_5.y;
        float3 _311 = _276.xyz * _276.w;
        _338 = float4(_311.x, _311.y, _311.z, _276.w) * (1.0 - fast::clamp(((_243 - (((_18._m19 * _20._m39) * in.m_5.y) * 0.5)) + (_300 * 0.5)) / (1.0 + _300), 0.0, 1.0));
    }
    else
    {
        float _314 = _249 * 0.5;
        float3 _329 = _276.xyz * _276.w;
        float3 _333 = _289.xyz * _289.w;
        _338 = mix(float4(_329.x, _329.y, _329.z, _276.w), float4(_333.x, _333.y, _333.z, _289.w), float4(fast::clamp(_243 + _314, 0.0, 1.0) * sqrt(precise::min(1.0, _249)))) * (1.0 - fast::clamp(((_243 - _314) + (_253 * 0.5)) / (1.0 + _253), 0.0, 1.0));
    }
    float4 _343 = _28.sample(_29, in.m_7.xy);
    float4 _356 = _338 + ((in.m_8 * fast::clamp((_343.x * in.m_7.z) - in.m_7.w, 0.0, 1.0)) * (1.0 - _338.w));
    float4 _366 = _30.sample(_31, ((in.m_11 * _20._m55.xy) + _20._m55.zw));
    float4 _398;
    if (_100)
    {
        _398 = _356 * fast::clamp(mix(1.0, (_18._m13 >= 0.0) ? (1.0 - in.m_6.w) : in.m_6.w, abs(_18._m13)), 0.0, 1.0);
    }
    else
    {
        _398 = _356;
    }
    float4 _399 = _398 * (in.m_3.w * fast::clamp((_366.w * (step(in.m_11.x, 1.0) * step(-in.m_11.x, 0.0))) * (step(in.m_11.y, 1.0) * step(-in.m_11.y, 0.0)), 0.0, 1.0));
    _399.w = mix(_399.w, 0.0, _18._m22);
    out.m_12 = _399;
    return out;
}

