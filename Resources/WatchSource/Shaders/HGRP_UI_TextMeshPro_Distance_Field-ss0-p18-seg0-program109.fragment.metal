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

struct _23
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _22
{
    _23 _m0[1];
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
    float4 m_6 [[user(locn3)]];
    float4 m_7 [[user(locn4)]];
    float4 m_8 [[user(locn5)]];
    float4 m_9 [[user(locn6)]];
    float2 m_10 [[user(locn7)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _12& _13 [[buffer(0)]], constant _16& _17 [[buffer(1)]], constant _18& _19 [[buffer(2)]], constant _20& _21 [[buffer(3)]], constant _22& _24 [[buffer(4)]], texture2d<float> _26 [[texture(0)]], texture2d<float> _28 [[texture(1)]], texture2d<float> _30 [[texture(2)]], texture2d<float> _32 [[texture(3)]], sampler _27 [[sampler(0)]], sampler _29 [[sampler(1)]], sampler _31 [[sampler(2)]], sampler _33 [[sampler(3)]])
{
    main0_out out = {};
    bool _110 = _17._m2 > 0.5;
    float2 _188;
    if (_110)
    {
        float _117 = _13._m0.y * _17._m16;
        float2 _123 = in.m_9.xy / float2(precise::max(in.m_9.w, 9.9999997473787516355514526367188e-05));
        float2 _136 = float2(_123.x * (_21._m14.x / precise::max(_21._m14.y, 1.0)), _123.y) * _17._m15;
        float2 _139 = _136 + float2(_117, _117 * 0.829999983310699462890625);
        float2 _144 = (_136 * 1.7000000476837158203125) + float2(_117 * (-0.9700000286102294921875), _117 * 1.309999942779541015625);
        float2 _158 = float2(sin(_139.y) + (sin(_144.y) * 0.5), cos(_139.x) + (cos(_144.x) * 0.5)) * 0.66670000553131103515625;
        float _175 = precise::max(_19._m50 - 1.0, 0.0);
        _188 = fast::clamp((float2(int2(sign(_158))) * powr(abs(_158), float2(precise::max(_17._m17, 1.0)))) * _17._m14, float2(-_175), float2(_175)) * float2(1.0 / _19._m48, 1.0 / _19._m49);
    }
    else
    {
        _188 = float2(0.0);
    }
    float2 _189 = in.m_4 + _188;
    float4 _193 = _30.sample(_31, _189);
    float _194 = _193.x;
    bool _197 = _17._m18 > 0.5;
    float _247;
    if (_197)
    {
        float2 _209 = float2(1.0 / _19._m48, 1.0 / _19._m49) * _17._m20;
        float _212 = _209.x;
        float _230 = _209.y;
        _247 = ((((_194 + _30.sample(_31, (_189 + float2(_212, 0.0))).x) + _30.sample(_31, (_189 + float2(-_212, 0.0))).x) + _30.sample(_31, (_189 + float2(0.0, _230))).x) + _30.sample(_31, (_189 + float2(0.0, -_230))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _247 = _194;
    }
    if ((_247 - in.m_5.x) < 0.0)
    {
        discard_fragment();
    }
    float _259 = (in.m_5.z - _247) * in.m_5.y;
    float _265 = (_19._m8 * _19._m39) * in.m_5.y;
    float _269 = (_19._m4 * _19._m39) * in.m_5.y;
    float3 _276 = _19._m2.xyz * in.m_3.xyz;
    float4 _291 = _28.sample(_29, (in.m_8.xy + (float2(_19._m0, _19._m1) * _21._m12.y)));
    float4 _292 = float4(_276.x, _276.y, _276.z, _19._m2.w) * _291;
    float4 _304 = _26.sample(_27, (in.m_8.zw + (float2(_19._m5, _19._m6) * _21._m12.y)));
    float4 _305 = _19._m7 * _304;
    float4 _354;
    if (_197)
    {
        float _316 = (_17._m21 * _19._m39) * in.m_5.y;
        float3 _327 = _292.xyz * _292.w;
        _354 = float4(_327.x, _327.y, _327.z, _292.w) * (1.0 - fast::clamp(((_259 - (((_17._m19 * _19._m39) * in.m_5.y) * 0.5)) + (_316 * 0.5)) / (1.0 + _316), 0.0, 1.0));
    }
    else
    {
        float _330 = _265 * 0.5;
        float3 _345 = _292.xyz * _292.w;
        float3 _349 = _305.xyz * _305.w;
        _354 = mix(float4(_345.x, _345.y, _345.z, _292.w), float4(_349.x, _349.y, _349.z, _305.w), float4(fast::clamp(_259 + _330, 0.0, 1.0) * sqrt(precise::min(1.0, _265)))) * (1.0 - fast::clamp(((_259 - _330) + (_269 * 0.5)) / (1.0 + _269), 0.0, 1.0));
    }
    float2 _365 = fast::clamp(((_24._m0[0]._m3.zw - _24._m0[0]._m3.xy) - abs(in.m_6.xy)) * in.m_6.zw, float2(0.0), float2(1.0));
    float4 _370 = fast::clamp(_24._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _375 = ((in.m_6.xy + _370.xy) + _370.zw) * 0.5;
    float _376 = _375.x;
    float _381 = _375.y;
    float4 _401 = (_354 * (_365.x * _365.y)) * (((smoothstep(0.0, _24._m0[0]._m4.x, _376 - _370.x) * smoothstep(0.0, _24._m0[0]._m4.z, _370.z - _376)) * smoothstep(0.0, _24._m0[0]._m4.y, _381 - _370.y)) * smoothstep(0.0, _24._m0[0]._m4.w, _370.w - _381));
    float4 _411 = _32.sample(_33, ((in.m_10 * _19._m55.xy) + _19._m55.zw));
    float4 _443;
    if (_110)
    {
        _443 = _401 * fast::clamp(mix(1.0, (_17._m13 >= 0.0) ? (1.0 - in.m_7.w) : in.m_7.w, abs(_17._m13)), 0.0, 1.0);
    }
    else
    {
        _443 = _401;
    }
    float4 _444 = _443 * (in.m_3.w * fast::clamp((_411.w * (step(in.m_10.x, 1.0) * step(-in.m_10.x, 0.0))) * (step(in.m_10.y, 1.0) * step(-in.m_10.y, 0.0)), 0.0, 1.0));
    _444.w = mix(_444.w, 0.0, _17._m22);
    out.m_11 = _444;
    return out;
}

