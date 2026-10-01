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
    bool _114 = _19._m2 > 0.5;
    float2 _192;
    if (_114)
    {
        float _121 = _15._m0.y * _19._m16;
        float2 _127 = in.m_11.xy / float2(precise::max(in.m_11.w, 9.9999997473787516355514526367188e-05));
        float2 _140 = float2(_127.x * (_23._m14.x / precise::max(_23._m14.y, 1.0)), _127.y) * _19._m15;
        float2 _143 = _140 + float2(_121, _121 * 0.829999983310699462890625);
        float2 _148 = (_140 * 1.7000000476837158203125) + float2(_121 * (-0.9700000286102294921875), _121 * 1.309999942779541015625);
        float2 _162 = float2(sin(_143.y) + (sin(_148.y) * 0.5), cos(_143.x) + (cos(_148.x) * 0.5)) * 0.66670000553131103515625;
        float _179 = precise::max(_21._m50 - 1.0, 0.0);
        _192 = fast::clamp((float2(int2(sign(_162))) * powr(abs(_162), float2(precise::max(_19._m17, 1.0)))) * _19._m14, float2(-_179), float2(_179)) * float2(1.0 / _21._m48, 1.0 / _21._m49);
    }
    else
    {
        _192 = float2(0.0);
    }
    float2 _193 = in.m_4 + _192;
    float4 _197 = _32.sample(_33, _193);
    float _198 = _197.x;
    bool _201 = _19._m18 > 0.5;
    float _251;
    if (_201)
    {
        float2 _213 = float2(1.0 / _21._m48, 1.0 / _21._m49) * _19._m20;
        float _216 = _213.x;
        float _234 = _213.y;
        _251 = ((((_198 + _32.sample(_33, (_193 + float2(_216, 0.0))).x) + _32.sample(_33, (_193 + float2(-_216, 0.0))).x) + _32.sample(_33, (_193 + float2(0.0, _234))).x) + _32.sample(_33, (_193 + float2(0.0, -_234))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _251 = _198;
    }
    float _257 = (in.m_5.z - _251) * in.m_5.y;
    float _263 = (_21._m8 * _21._m39) * in.m_5.y;
    float _267 = (_21._m4 * _21._m39) * in.m_5.y;
    float3 _274 = _21._m2.xyz * in.m_3.xyz;
    float4 _289 = _30.sample(_31, (in.m_10.xy + (float2(_21._m0, _21._m1) * _23._m12.y)));
    float4 _290 = float4(_274.x, _274.y, _274.z, _21._m2.w) * _289;
    float4 _302 = _28.sample(_29, (in.m_10.zw + (float2(_21._m5, _21._m6) * _23._m12.y)));
    float4 _303 = _21._m7 * _302;
    float4 _352;
    if (_201)
    {
        float _314 = (_19._m21 * _21._m39) * in.m_5.y;
        float3 _325 = _290.xyz * _290.w;
        _352 = float4(_325.x, _325.y, _325.z, _290.w) * (1.0 - fast::clamp(((_257 - (((_19._m19 * _21._m39) * in.m_5.y) * 0.5)) + (_314 * 0.5)) / (1.0 + _314), 0.0, 1.0));
    }
    else
    {
        float _328 = _263 * 0.5;
        float3 _343 = _290.xyz * _290.w;
        float3 _347 = _303.xyz * _303.w;
        _352 = mix(float4(_343.x, _343.y, _343.z, _290.w), float4(_347.x, _347.y, _347.z, _303.w), float4(fast::clamp(_257 + _328, 0.0, 1.0) * sqrt(precise::min(1.0, _263)))) * (1.0 - fast::clamp(((_257 - _328) + (_267 * 0.5)) / (1.0 + _267), 0.0, 1.0));
    }
    float4 _357 = _32.sample(_33, in.m_8.xy);
    float2 _381 = fast::clamp(((_26._m0[0]._m3.zw - _26._m0[0]._m3.xy) - abs(in.m_6.xy)) * in.m_6.zw, float2(0.0), float2(1.0));
    float4 _386 = fast::clamp(_26._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _391 = ((in.m_6.xy + _386.xy) + _386.zw) * 0.5;
    float _392 = _391.x;
    float _397 = _391.y;
    float4 _417 = ((_352 + ((in.m_9 * fast::clamp((_357.x * in.m_8.z) - in.m_8.w, 0.0, 1.0)) * (1.0 - _352.w))) * (_381.x * _381.y)) * (((smoothstep(0.0, _26._m0[0]._m4.x, _392 - _386.x) * smoothstep(0.0, _26._m0[0]._m4.z, _386.z - _392)) * smoothstep(0.0, _26._m0[0]._m4.y, _397 - _386.y)) * smoothstep(0.0, _26._m0[0]._m4.w, _386.w - _397));
    float4 _427 = _34.sample(_35, ((in.m_12 * _21._m55.xy) + _21._m55.zw));
    float4 _459;
    if (_114)
    {
        _459 = _417 * fast::clamp(mix(1.0, (_19._m13 >= 0.0) ? (1.0 - in.m_7.w) : in.m_7.w, abs(_19._m13)), 0.0, 1.0);
    }
    else
    {
        _459 = _417;
    }
    float4 _460 = _459 * (in.m_3.w * fast::clamp((_427.w * (step(in.m_12.x, 1.0) * step(-in.m_12.x, 0.0))) * (step(in.m_12.y, 1.0) * step(-in.m_12.y, 0.0)), 0.0, 1.0));
    _460.w = mix(_460.w, 0.0, _19._m22);
    out.m_13 = _460;
    return out;
}

