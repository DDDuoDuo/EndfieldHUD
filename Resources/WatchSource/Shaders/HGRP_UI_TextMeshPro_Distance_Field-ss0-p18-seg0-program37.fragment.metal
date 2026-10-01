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

struct main0_out
{
    float4 m_10 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn2)]];
    float4 m_6 [[user(locn4)]];
    float4 m_7 [[user(locn5)]];
    float4 m_8 [[user(locn6)]];
    float2 m_9 [[user(locn7)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _11& _12 [[buffer(0)]], constant _15& _16 [[buffer(1)]], constant _17& _18 [[buffer(2)]], constant _19& _20 [[buffer(3)]], texture2d<float> _22 [[texture(0)]], texture2d<float> _24 [[texture(1)]], texture2d<float> _26 [[texture(2)]], texture2d<float> _28 [[texture(3)]], sampler _23 [[sampler(0)]], sampler _25 [[sampler(1)]], sampler _27 [[sampler(2)]], sampler _29 [[sampler(3)]])
{
    main0_out out = {};
    bool _96 = _16._m2 > 0.5;
    float2 _174;
    if (_96)
    {
        float _103 = _12._m0.y * _16._m16;
        float2 _109 = in.m_8.xy / float2(precise::max(in.m_8.w, 9.9999997473787516355514526367188e-05));
        float2 _122 = float2(_109.x * (_20._m14.x / precise::max(_20._m14.y, 1.0)), _109.y) * _16._m15;
        float2 _125 = _122 + float2(_103, _103 * 0.829999983310699462890625);
        float2 _130 = (_122 * 1.7000000476837158203125) + float2(_103 * (-0.9700000286102294921875), _103 * 1.309999942779541015625);
        float2 _144 = float2(sin(_125.y) + (sin(_130.y) * 0.5), cos(_125.x) + (cos(_130.x) * 0.5)) * 0.66670000553131103515625;
        float _161 = precise::max(_18._m50 - 1.0, 0.0);
        _174 = fast::clamp((float2(int2(sign(_144))) * powr(abs(_144), float2(precise::max(_16._m17, 1.0)))) * _16._m14, float2(-_161), float2(_161)) * float2(1.0 / _18._m48, 1.0 / _18._m49);
    }
    else
    {
        _174 = float2(0.0);
    }
    float2 _175 = in.m_4 + _174;
    float4 _179 = _26.sample(_27, _175);
    float _180 = _179.x;
    bool _183 = _16._m18 > 0.5;
    float _233;
    if (_183)
    {
        float2 _195 = float2(1.0 / _18._m48, 1.0 / _18._m49) * _16._m20;
        float _198 = _195.x;
        float _216 = _195.y;
        _233 = ((((_180 + _26.sample(_27, (_175 + float2(_198, 0.0))).x) + _26.sample(_27, (_175 + float2(-_198, 0.0))).x) + _26.sample(_27, (_175 + float2(0.0, _216))).x) + _26.sample(_27, (_175 + float2(0.0, -_216))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _233 = _180;
    }
    if ((_233 - in.m_5.x) < 0.0)
    {
        discard_fragment();
    }
    float _245 = (in.m_5.z - _233) * in.m_5.y;
    float _251 = (_18._m8 * _18._m39) * in.m_5.y;
    float _255 = (_18._m4 * _18._m39) * in.m_5.y;
    float3 _262 = _18._m2.xyz * in.m_3.xyz;
    float4 _277 = _24.sample(_25, (in.m_7.xy + (float2(_18._m0, _18._m1) * _20._m12.y)));
    float4 _278 = float4(_262.x, _262.y, _262.z, _18._m2.w) * _277;
    float4 _290 = _22.sample(_23, (in.m_7.zw + (float2(_18._m5, _18._m6) * _20._m12.y)));
    float4 _291 = _18._m7 * _290;
    float4 _340;
    if (_183)
    {
        float _302 = (_16._m21 * _18._m39) * in.m_5.y;
        float3 _313 = _278.xyz * _278.w;
        _340 = float4(_313.x, _313.y, _313.z, _278.w) * (1.0 - fast::clamp(((_245 - (((_16._m19 * _18._m39) * in.m_5.y) * 0.5)) + (_302 * 0.5)) / (1.0 + _302), 0.0, 1.0));
    }
    else
    {
        float _316 = _251 * 0.5;
        float3 _331 = _278.xyz * _278.w;
        float3 _335 = _291.xyz * _291.w;
        _340 = mix(float4(_331.x, _331.y, _331.z, _278.w), float4(_335.x, _335.y, _335.z, _291.w), float4(fast::clamp(_245 + _316, 0.0, 1.0) * sqrt(precise::min(1.0, _251)))) * (1.0 - fast::clamp(((_245 - _316) + (_255 * 0.5)) / (1.0 + _255), 0.0, 1.0));
    }
    float4 _350 = _28.sample(_29, ((in.m_9 * _18._m55.xy) + _18._m55.zw));
    float4 _382;
    if (_96)
    {
        _382 = _340 * fast::clamp(mix(1.0, (_16._m13 >= 0.0) ? (1.0 - in.m_6.w) : in.m_6.w, abs(_16._m13)), 0.0, 1.0);
    }
    else
    {
        _382 = _340;
    }
    float4 _383 = _382 * (in.m_3.w * fast::clamp((_350.w * (step(in.m_9.x, 1.0) * step(-in.m_9.x, 0.0))) * (step(in.m_9.y, 1.0) * step(-in.m_9.y, 0.0)), 0.0, 1.0));
    _383.w = mix(_383.w, 0.0, _16._m22);
    out.m_10 = _383;
    return out;
}

