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
    bool _111 = _17._m2 > 0.5;
    float2 _189;
    if (_111)
    {
        float _118 = _13._m0.y * _17._m16;
        float2 _124 = in.m_9.xy / float2(precise::max(in.m_9.w, 9.9999997473787516355514526367188e-05));
        float2 _137 = float2(_124.x * (_21._m14.x / precise::max(_21._m14.y, 1.0)), _124.y) * _17._m15;
        float2 _140 = _137 + float2(_118, _118 * 0.829999983310699462890625);
        float2 _145 = (_137 * 1.7000000476837158203125) + float2(_118 * (-0.9700000286102294921875), _118 * 1.309999942779541015625);
        float2 _159 = float2(sin(_140.y) + (sin(_145.y) * 0.5), cos(_140.x) + (cos(_145.x) * 0.5)) * 0.66670000553131103515625;
        float _176 = precise::max(_19._m50 - 1.0, 0.0);
        _189 = fast::clamp((float2(int2(sign(_159))) * powr(abs(_159), float2(precise::max(_17._m17, 1.0)))) * _17._m14, float2(-_176), float2(_176)) * float2(1.0 / _19._m48, 1.0 / _19._m49);
    }
    else
    {
        _189 = float2(0.0);
    }
    float2 _190 = in.m_4 + _189;
    float4 _194 = _30.sample(_31, _190);
    float _195 = _194.x;
    bool _198 = _17._m18 > 0.5;
    float _248;
    if (_198)
    {
        float2 _210 = float2(1.0 / _19._m48, 1.0 / _19._m49) * _17._m20;
        float _213 = _210.x;
        float _231 = _210.y;
        _248 = ((((_195 + _30.sample(_31, (_190 + float2(_213, 0.0))).x) + _30.sample(_31, (_190 + float2(-_213, 0.0))).x) + _30.sample(_31, (_190 + float2(0.0, _231))).x) + _30.sample(_31, (_190 + float2(0.0, -_231))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _248 = _195;
    }
    if ((_248 - in.m_5.x) < 0.0)
    {
        discard_fragment();
    }
    float _260 = (in.m_5.z - _248) * in.m_5.y;
    float _266 = (_19._m8 * _19._m39) * in.m_5.y;
    float _270 = (_19._m4 * _19._m39) * in.m_5.y;
    float3 _277 = _19._m2.xyz * in.m_3.xyz;
    float4 _292 = _28.sample(_29, (in.m_8.xy + (float2(_19._m0, _19._m1) * _21._m12.y)));
    float4 _293 = float4(_277.x, _277.y, _277.z, _19._m2.w) * _292;
    float4 _305 = _26.sample(_27, (in.m_8.zw + (float2(_19._m5, _19._m6) * _21._m12.y)));
    float4 _306 = _19._m7 * _305;
    float4 _355;
    if (_198)
    {
        float _317 = (_17._m21 * _19._m39) * in.m_5.y;
        float3 _328 = _293.xyz * _293.w;
        _355 = float4(_328.x, _328.y, _328.z, _293.w) * (1.0 - fast::clamp(((_260 - (((_17._m19 * _19._m39) * in.m_5.y) * 0.5)) + (_317 * 0.5)) / (1.0 + _317), 0.0, 1.0));
    }
    else
    {
        float _331 = _266 * 0.5;
        float3 _346 = _293.xyz * _293.w;
        float3 _350 = _306.xyz * _306.w;
        _355 = mix(float4(_346.x, _346.y, _346.z, _293.w), float4(_350.x, _350.y, _350.z, _306.w), float4(fast::clamp(_260 + _331, 0.0, 1.0) * sqrt(precise::min(1.0, _266)))) * (1.0 - fast::clamp(((_260 - _331) + (_270 * 0.5)) / (1.0 + _270), 0.0, 1.0));
    }
    float2 _366 = fast::clamp(((_24._m0[0]._m3.zw - _24._m0[0]._m3.xy) - abs(in.m_6.xy)) * in.m_6.zw, float2(0.0), float2(1.0));
    float4 _371 = fast::clamp(_24._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _376 = ((in.m_6.xy + _371.xy) + _371.zw) * 0.5;
    float _377 = _376.x;
    float _382 = _376.y;
    float4 _402 = (_355 * (_366.x * _366.y)) * (((smoothstep(0.0, _24._m0[0]._m4.x, _377 - _371.x) * smoothstep(0.0, _24._m0[0]._m4.z, _371.z - _377)) * smoothstep(0.0, _24._m0[0]._m4.y, _382 - _371.y)) * smoothstep(0.0, _24._m0[0]._m4.w, _371.w - _382));
    if ((_402.w - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float4 _417 = _32.sample(_33, ((in.m_10 * _19._m55.xy) + _19._m55.zw));
    float4 _449;
    if (_111)
    {
        _449 = _402 * fast::clamp(mix(1.0, (_17._m13 >= 0.0) ? (1.0 - in.m_7.w) : in.m_7.w, abs(_17._m13)), 0.0, 1.0);
    }
    else
    {
        _449 = _402;
    }
    float4 _450 = _449 * (in.m_3.w * fast::clamp((_417.w * (step(in.m_10.x, 1.0) * step(-in.m_10.x, 0.0))) * (step(in.m_10.y, 1.0) * step(-in.m_10.y, 0.0)), 0.0, 1.0));
    _450.w = mix(_450.w, 0.0, _17._m22);
    out.m_11 = _450;
    return out;
}

