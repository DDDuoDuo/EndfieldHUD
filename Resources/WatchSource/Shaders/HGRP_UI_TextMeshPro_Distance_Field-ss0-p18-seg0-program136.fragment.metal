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
    bool _106 = _16._m2 > 0.5;
    float2 _184;
    if (_106)
    {
        float _113 = _12._m0.y * _16._m16;
        float2 _119 = in.m_9.xy / float2(precise::max(in.m_9.w, 9.9999997473787516355514526367188e-05));
        float2 _132 = float2(_119.x * (_20._m14.x / precise::max(_20._m14.y, 1.0)), _119.y) * _16._m15;
        float2 _135 = _132 + float2(_113, _113 * 0.829999983310699462890625);
        float2 _140 = (_132 * 1.7000000476837158203125) + float2(_113 * (-0.9700000286102294921875), _113 * 1.309999942779541015625);
        float2 _154 = float2(sin(_135.y) + (sin(_140.y) * 0.5), cos(_135.x) + (cos(_140.x) * 0.5)) * 0.66670000553131103515625;
        float _171 = precise::max(_18._m50 - 1.0, 0.0);
        _184 = fast::clamp((float2(int2(sign(_154))) * powr(abs(_154), float2(precise::max(_16._m17, 1.0)))) * _16._m14, float2(-_171), float2(_171)) * float2(1.0 / _18._m48, 1.0 / _18._m49);
    }
    else
    {
        _184 = float2(0.0);
    }
    float2 _185 = in.m_4 + _184;
    float4 _189 = _29.sample(_30, _185);
    float _190 = _189.x;
    bool _193 = _16._m18 > 0.5;
    float _243;
    if (_193)
    {
        float2 _205 = float2(1.0 / _18._m48, 1.0 / _18._m49) * _16._m20;
        float _208 = _205.x;
        float _226 = _205.y;
        _243 = ((((_190 + _29.sample(_30, (_185 + float2(_208, 0.0))).x) + _29.sample(_30, (_185 + float2(-_208, 0.0))).x) + _29.sample(_30, (_185 + float2(0.0, _226))).x) + _29.sample(_30, (_185 + float2(0.0, -_226))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _243 = _190;
    }
    if ((_243 - in.m_5.x) < 0.0)
    {
        discard_fragment();
    }
    float _255 = (in.m_5.z - _243) * in.m_5.y;
    float _261 = (_18._m8 * _18._m39) * in.m_5.y;
    float _265 = (_18._m4 * _18._m39) * in.m_5.y;
    float3 _272 = _18._m2.xyz * in.m_3.xyz;
    float4 _287 = _27.sample(_28, (in.m_8.xy + (float2(_18._m0, _18._m1) * _20._m12.y)));
    float4 _288 = float4(_272.x, _272.y, _272.z, _18._m2.w) * _287;
    float4 _300 = _25.sample(_26, (in.m_8.zw + (float2(_18._m5, _18._m6) * _20._m12.y)));
    float4 _301 = _18._m7 * _300;
    float4 _350;
    if (_193)
    {
        float _312 = (_16._m21 * _18._m39) * in.m_5.y;
        float3 _323 = _288.xyz * _288.w;
        _350 = float4(_323.x, _323.y, _323.z, _288.w) * (1.0 - fast::clamp(((_255 - (((_16._m19 * _18._m39) * in.m_5.y) * 0.5)) + (_312 * 0.5)) / (1.0 + _312), 0.0, 1.0));
    }
    else
    {
        float _326 = _261 * 0.5;
        float3 _341 = _288.xyz * _288.w;
        float3 _345 = _301.xyz * _301.w;
        _350 = mix(float4(_341.x, _341.y, _341.z, _288.w), float4(_345.x, _345.y, _345.z, _301.w), float4(fast::clamp(_255 + _326, 0.0, 1.0) * sqrt(precise::min(1.0, _261)))) * (1.0 - fast::clamp(((_255 - _326) + (_265 * 0.5)) / (1.0 + _265), 0.0, 1.0));
    }
    float2 _361 = fast::clamp(((_23._m0[0]._m3.zw - _23._m0[0]._m3.xy) - abs(in.m_6.xy)) * in.m_6.zw, float2(0.0), float2(1.0));
    float4 _366 = fast::clamp(_23._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _371 = ((in.m_6.xy + _366.xy) + _366.zw) * 0.5;
    float _372 = _371.x;
    float _377 = _371.y;
    float4 _397 = (_350 * (_361.x * _361.y)) * (((smoothstep(0.0, _23._m0[0]._m4.x, _372 - _366.x) * smoothstep(0.0, _23._m0[0]._m4.z, _366.z - _372)) * smoothstep(0.0, _23._m0[0]._m4.y, _377 - _366.y)) * smoothstep(0.0, _23._m0[0]._m4.w, _366.w - _377));
    if ((_397.w - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float4 _416;
    if (_106)
    {
        _416 = _397 * fast::clamp(mix(1.0, (_16._m13 >= 0.0) ? (1.0 - in.m_7.w) : in.m_7.w, abs(_16._m13)), 0.0, 1.0);
    }
    else
    {
        _416 = _397;
    }
    float4 _419 = _416 * in.m_3.w;
    _419.w = mix(_419.w, 0.0, _16._m22);
    out.m_10 = _419;
    return out;
}

