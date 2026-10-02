#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _11
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

struct _15
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

struct _17
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

struct _19
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

struct _22
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _21
{
    _22 _m0[1];
};

struct main0_out
{
    float4 m_10 [[color(0)]];
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
};

fragment main0_out main0(main0_in in [[stage_in]], constant _11& _12 [[buffer(0)]], constant _15& _16 [[buffer(1)]], constant _17& _18 [[buffer(2)]], constant _19& _20 [[buffer(3)]], constant _21& _23 [[buffer(4)]], texture2d<float> _25 [[texture(0)]], texture2d<float> _27 [[texture(1)]], texture2d<float> _29 [[texture(2)]], sampler _26 [[sampler(0)]], sampler _28 [[sampler(1)]], sampler _30 [[sampler(2)]])
{
    main0_out out = {};
    bool _105 = _16._m2 > 0.5;
    float2 _183;
    if (_105)
    {
        float _112 = _12._m0.y * _16._m16;
        float2 _118 = in.m_9.xy / float2(precise::max(in.m_9.w, 9.9999997473787516355514526367188e-05));
        float2 _131 = float2(_118.x * (_20._m14.x / precise::max(_20._m14.y, 1.0)), _118.y) * _16._m15;
        float2 _134 = _131 + float2(_112, _112 * 0.829999983310699462890625);
        float2 _139 = (_131 * 1.7000000476837158203125) + float2(_112 * (-0.9700000286102294921875), _112 * 1.309999942779541015625);
        float2 _153 = float2(sin(_134.y) + (sin(_139.y) * 0.5), cos(_134.x) + (cos(_139.x) * 0.5)) * 0.66670000553131103515625;
        float _170 = precise::max(_18._m50 - 1.0, 0.0);
        _183 = fast::clamp((float2(int2(sign(_153))) * powr(abs(_153), float2(precise::max(_16._m17, 1.0)))) * _16._m14, float2(-_170), float2(_170)) * float2(1.0 / _18._m48, 1.0 / _18._m49);
    }
    else
    {
        _183 = float2(0.0);
    }
    float2 _184 = in.m_4 + _183;
    float4 _188 = _29.sample(_30, _184);
    float _189 = _188.x;
    bool _192 = _16._m18 > 0.5;
    float _242;
    if (_192)
    {
        float2 _204 = float2(1.0 / _18._m48, 1.0 / _18._m49) * _16._m20;
        float _207 = _204.x;
        float _225 = _204.y;
        _242 = ((((_189 + _29.sample(_30, (_184 + float2(_207, 0.0))).x) + _29.sample(_30, (_184 + float2(-_207, 0.0))).x) + _29.sample(_30, (_184 + float2(0.0, _225))).x) + _29.sample(_30, (_184 + float2(0.0, -_225))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _242 = _189;
    }
    if ((_242 - in.m_5.x) < 0.0)
    {
        discard_fragment();
    }
    float _254 = (in.m_5.z - _242) * in.m_5.y;
    float _260 = (_18._m8 * _18._m39) * in.m_5.y;
    float _264 = (_18._m4 * _18._m39) * in.m_5.y;
    float3 _271 = _18._m2.xyz * in.m_3.xyz;
    float4 _286 = _27.sample(_28, (in.m_8.xy + (float2(_18._m0, _18._m1) * _20._m12.y)));
    float4 _287 = float4(_271.x, _271.y, _271.z, _18._m2.w) * _286;
    float4 _299 = _25.sample(_26, (in.m_8.zw + (float2(_18._m5, _18._m6) * _20._m12.y)));
    float4 _300 = _18._m7 * _299;
    float4 _349;
    if (_192)
    {
        float _311 = (_16._m21 * _18._m39) * in.m_5.y;
        float3 _322 = _287.xyz * _287.w;
        _349 = float4(_322.x, _322.y, _322.z, _287.w) * (1.0 - fast::clamp(((_254 - (((_16._m19 * _18._m39) * in.m_5.y) * 0.5)) + (_311 * 0.5)) / (1.0 + _311), 0.0, 1.0));
    }
    else
    {
        float _325 = _260 * 0.5;
        float3 _340 = _287.xyz * _287.w;
        float3 _344 = _300.xyz * _300.w;
        _349 = mix(float4(_340.x, _340.y, _340.z, _287.w), float4(_344.x, _344.y, _344.z, _300.w), float4(fast::clamp(_254 + _325, 0.0, 1.0) * sqrt(precise::min(1.0, _260)))) * (1.0 - fast::clamp(((_254 - _325) + (_264 * 0.5)) / (1.0 + _264), 0.0, 1.0));
    }
    float2 _360 = fast::clamp(((_23._m0[0]._m3.zw - _23._m0[0]._m3.xy) - abs(in.m_6.xy)) * in.m_6.zw, float2(0.0), float2(1.0));
    float4 _365 = fast::clamp(_23._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _370 = ((in.m_6.xy + _365.xy) + _365.zw) * 0.5;
    float _371 = _370.x;
    float _376 = _370.y;
    float4 _396 = (_349 * (_360.x * _360.y)) * (((smoothstep(0.0, _23._m0[0]._m4.x, _371 - _365.x) * smoothstep(0.0, _23._m0[0]._m4.z, _365.z - _371)) * smoothstep(0.0, _23._m0[0]._m4.y, _376 - _365.y)) * smoothstep(0.0, _23._m0[0]._m4.w, _365.w - _376));
    float4 _410;
    if (_105)
    {
        _410 = _396 * fast::clamp(mix(1.0, (_16._m13 >= 0.0) ? (1.0 - in.m_7.w) : in.m_7.w, abs(_16._m13)), 0.0, 1.0);
    }
    else
    {
        _410 = _396;
    }
    float4 _413 = _410 * in.m_3.w;
    _413.w = mix(_413.w, 0.0, _16._m22);
    out.m_10 = _413;
    return out;
}

