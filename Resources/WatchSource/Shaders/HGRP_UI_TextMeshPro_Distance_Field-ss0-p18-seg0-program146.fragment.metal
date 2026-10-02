#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _14
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

struct _18
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

struct _20
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

struct _22
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

struct _25
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _24
{
    _25 _m0[1];
};

struct main0_out
{
    float4 m_13 [[color(0)]];
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
    float4 m_10 [[user(locn7)]];
    float4 m_11 [[user(locn8)]];
    float2 m_12 [[user(locn9)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _14& _15 [[buffer(0)]], constant _18& _19 [[buffer(1)]], constant _20& _21 [[buffer(2)]], constant _22& _23 [[buffer(3)]], constant _24& _26 [[buffer(4)]], texture2d<float> _28 [[texture(0)]], texture2d<float> _30 [[texture(1)]], texture2d<float> _32 [[texture(2)]], texture2d<float> _34 [[texture(3)]], sampler _29 [[sampler(0)]], sampler _31 [[sampler(1)]], sampler _33 [[sampler(2)]], sampler _35 [[sampler(3)]])
{
    main0_out out = {};
    bool _115 = _19._m2 > 0.5;
    float2 _193;
    if (_115)
    {
        float _122 = _15._m0.y * _19._m16;
        float2 _128 = in.m_11.xy / float2(precise::max(in.m_11.w, 9.9999997473787516355514526367188e-05));
        float2 _141 = float2(_128.x * (_23._m14.x / precise::max(_23._m14.y, 1.0)), _128.y) * _19._m15;
        float2 _144 = _141 + float2(_122, _122 * 0.829999983310699462890625);
        float2 _149 = (_141 * 1.7000000476837158203125) + float2(_122 * (-0.9700000286102294921875), _122 * 1.309999942779541015625);
        float2 _163 = float2(sin(_144.y) + (sin(_149.y) * 0.5), cos(_144.x) + (cos(_149.x) * 0.5)) * 0.66670000553131103515625;
        float _180 = precise::max(_21._m50 - 1.0, 0.0);
        _193 = fast::clamp((float2(int2(sign(_163))) * powr(abs(_163), float2(precise::max(_19._m17, 1.0)))) * _19._m14, float2(-_180), float2(_180)) * float2(1.0 / _21._m48, 1.0 / _21._m49);
    }
    else
    {
        _193 = float2(0.0);
    }
    float2 _194 = in.m_4 + _193;
    float4 _198 = _32.sample(_33, _194);
    float _199 = _198.x;
    bool _202 = _19._m18 > 0.5;
    float _252;
    if (_202)
    {
        float2 _214 = float2(1.0 / _21._m48, 1.0 / _21._m49) * _19._m20;
        float _217 = _214.x;
        float _235 = _214.y;
        _252 = ((((_199 + _32.sample(_33, (_194 + float2(_217, 0.0))).x) + _32.sample(_33, (_194 + float2(-_217, 0.0))).x) + _32.sample(_33, (_194 + float2(0.0, _235))).x) + _32.sample(_33, (_194 + float2(0.0, -_235))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _252 = _199;
    }
    float _258 = (in.m_5.z - _252) * in.m_5.y;
    float _264 = (_21._m8 * _21._m39) * in.m_5.y;
    float _268 = (_21._m4 * _21._m39) * in.m_5.y;
    float3 _275 = _21._m2.xyz * in.m_3.xyz;
    float4 _290 = _30.sample(_31, (in.m_10.xy + (float2(_21._m0, _21._m1) * _23._m12.y)));
    float4 _291 = float4(_275.x, _275.y, _275.z, _21._m2.w) * _290;
    float4 _303 = _28.sample(_29, (in.m_10.zw + (float2(_21._m5, _21._m6) * _23._m12.y)));
    float4 _304 = _21._m7 * _303;
    float4 _353;
    if (_202)
    {
        float _315 = (_19._m21 * _21._m39) * in.m_5.y;
        float3 _326 = _291.xyz * _291.w;
        _353 = float4(_326.x, _326.y, _326.z, _291.w) * (1.0 - fast::clamp(((_258 - (((_19._m19 * _21._m39) * in.m_5.y) * 0.5)) + (_315 * 0.5)) / (1.0 + _315), 0.0, 1.0));
    }
    else
    {
        float _329 = _264 * 0.5;
        float3 _344 = _291.xyz * _291.w;
        float3 _348 = _304.xyz * _304.w;
        _353 = mix(float4(_344.x, _344.y, _344.z, _291.w), float4(_348.x, _348.y, _348.z, _304.w), float4(fast::clamp(_258 + _329, 0.0, 1.0) * sqrt(precise::min(1.0, _264)))) * (1.0 - fast::clamp(((_258 - _329) + (_268 * 0.5)) / (1.0 + _268), 0.0, 1.0));
    }
    float4 _358 = _32.sample(_33, in.m_8.xy);
    float2 _382 = fast::clamp(((_26._m0[0]._m3.zw - _26._m0[0]._m3.xy) - abs(in.m_6.xy)) * in.m_6.zw, float2(0.0), float2(1.0));
    float4 _387 = fast::clamp(_26._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _392 = ((in.m_6.xy + _387.xy) + _387.zw) * 0.5;
    float _393 = _392.x;
    float _398 = _392.y;
    float4 _418 = ((_353 + ((in.m_9 * fast::clamp((_358.x * in.m_8.z) - in.m_8.w, 0.0, 1.0)) * (1.0 - _353.w))) * (_382.x * _382.y)) * (((smoothstep(0.0, _26._m0[0]._m4.x, _393 - _387.x) * smoothstep(0.0, _26._m0[0]._m4.z, _387.z - _393)) * smoothstep(0.0, _26._m0[0]._m4.y, _398 - _387.y)) * smoothstep(0.0, _26._m0[0]._m4.w, _387.w - _398));
    if ((_418.w - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float4 _433 = _34.sample(_35, ((in.m_12 * _21._m55.xy) + _21._m55.zw));
    float4 _465;
    if (_115)
    {
        _465 = _418 * fast::clamp(mix(1.0, (_19._m13 >= 0.0) ? (1.0 - in.m_7.w) : in.m_7.w, abs(_19._m13)), 0.0, 1.0);
    }
    else
    {
        _465 = _418;
    }
    float4 _466 = _465 * (in.m_3.w * fast::clamp((_433.w * (step(in.m_12.x, 1.0) * step(-in.m_12.x, 0.0))) * (step(in.m_12.y, 1.0) * step(-in.m_12.y, 0.0)), 0.0, 1.0));
    _466.w = mix(_466.w, 0.0, _19._m22);
    out.m_13 = _466;
    return out;
}

