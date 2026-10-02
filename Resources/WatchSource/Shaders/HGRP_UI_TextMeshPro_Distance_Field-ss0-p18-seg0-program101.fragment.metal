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

struct _24
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _23
{
    _24 _m0[1];
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
    float4 m_6 [[user(locn3)]];
    float4 m_7 [[user(locn4)]];
    float4 m_8 [[user(locn5)]];
    float4 m_9 [[user(locn6)]];
    float4 m_10 [[user(locn7)]];
    float4 m_11 [[user(locn8)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _13& _14 [[buffer(0)]], constant _17& _18 [[buffer(1)]], constant _19& _20 [[buffer(2)]], constant _21& _22 [[buffer(3)]], constant _23& _25 [[buffer(4)]], texture2d<float> _27 [[texture(0)]], texture2d<float> _29 [[texture(1)]], texture2d<float> _31 [[texture(2)]], sampler _28 [[sampler(0)]], sampler _30 [[sampler(1)]], sampler _32 [[sampler(2)]])
{
    main0_out out = {};
    bool _109 = _18._m2 > 0.5;
    float2 _187;
    if (_109)
    {
        float _116 = _14._m0.y * _18._m16;
        float2 _122 = in.m_11.xy / float2(precise::max(in.m_11.w, 9.9999997473787516355514526367188e-05));
        float2 _135 = float2(_122.x * (_22._m14.x / precise::max(_22._m14.y, 1.0)), _122.y) * _18._m15;
        float2 _138 = _135 + float2(_116, _116 * 0.829999983310699462890625);
        float2 _143 = (_135 * 1.7000000476837158203125) + float2(_116 * (-0.9700000286102294921875), _116 * 1.309999942779541015625);
        float2 _157 = float2(sin(_138.y) + (sin(_143.y) * 0.5), cos(_138.x) + (cos(_143.x) * 0.5)) * 0.66670000553131103515625;
        float _174 = precise::max(_20._m50 - 1.0, 0.0);
        _187 = fast::clamp((float2(int2(sign(_157))) * powr(abs(_157), float2(precise::max(_18._m17, 1.0)))) * _18._m14, float2(-_174), float2(_174)) * float2(1.0 / _20._m48, 1.0 / _20._m49);
    }
    else
    {
        _187 = float2(0.0);
    }
    float2 _188 = in.m_4 + _187;
    float4 _192 = _31.sample(_32, _188);
    float _193 = _192.x;
    bool _196 = _18._m18 > 0.5;
    float _246;
    if (_196)
    {
        float2 _208 = float2(1.0 / _20._m48, 1.0 / _20._m49) * _18._m20;
        float _211 = _208.x;
        float _229 = _208.y;
        _246 = ((((_193 + _31.sample(_32, (_188 + float2(_211, 0.0))).x) + _31.sample(_32, (_188 + float2(-_211, 0.0))).x) + _31.sample(_32, (_188 + float2(0.0, _229))).x) + _31.sample(_32, (_188 + float2(0.0, -_229))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _246 = _193;
    }
    float _252 = (in.m_5.z - _246) * in.m_5.y;
    float _258 = (_20._m8 * _20._m39) * in.m_5.y;
    float _262 = (_20._m4 * _20._m39) * in.m_5.y;
    float3 _269 = _20._m2.xyz * in.m_3.xyz;
    float4 _284 = _29.sample(_30, (in.m_10.xy + (float2(_20._m0, _20._m1) * _22._m12.y)));
    float4 _285 = float4(_269.x, _269.y, _269.z, _20._m2.w) * _284;
    float4 _297 = _27.sample(_28, (in.m_10.zw + (float2(_20._m5, _20._m6) * _22._m12.y)));
    float4 _298 = _20._m7 * _297;
    float4 _347;
    if (_196)
    {
        float _309 = (_18._m21 * _20._m39) * in.m_5.y;
        float3 _320 = _285.xyz * _285.w;
        _347 = float4(_320.x, _320.y, _320.z, _285.w) * (1.0 - fast::clamp(((_252 - (((_18._m19 * _20._m39) * in.m_5.y) * 0.5)) + (_309 * 0.5)) / (1.0 + _309), 0.0, 1.0));
    }
    else
    {
        float _323 = _258 * 0.5;
        float3 _338 = _285.xyz * _285.w;
        float3 _342 = _298.xyz * _298.w;
        _347 = mix(float4(_338.x, _338.y, _338.z, _285.w), float4(_342.x, _342.y, _342.z, _298.w), float4(fast::clamp(_252 + _323, 0.0, 1.0) * sqrt(precise::min(1.0, _258)))) * (1.0 - fast::clamp(((_252 - _323) + (_262 * 0.5)) / (1.0 + _262), 0.0, 1.0));
    }
    float4 _352 = _31.sample(_32, in.m_8.xy);
    float2 _376 = fast::clamp(((_25._m0[0]._m3.zw - _25._m0[0]._m3.xy) - abs(in.m_6.xy)) * in.m_6.zw, float2(0.0), float2(1.0));
    float4 _381 = fast::clamp(_25._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _386 = ((in.m_6.xy + _381.xy) + _381.zw) * 0.5;
    float _387 = _386.x;
    float _392 = _386.y;
    float4 _412 = ((_347 + ((in.m_9 * fast::clamp((_352.x * in.m_8.z) - in.m_8.w, 0.0, 1.0)) * (1.0 - _347.w))) * (_376.x * _376.y)) * (((smoothstep(0.0, _25._m0[0]._m4.x, _387 - _381.x) * smoothstep(0.0, _25._m0[0]._m4.z, _381.z - _387)) * smoothstep(0.0, _25._m0[0]._m4.y, _392 - _381.y)) * smoothstep(0.0, _25._m0[0]._m4.w, _381.w - _392));
    float4 _426;
    if (_109)
    {
        _426 = _412 * fast::clamp(mix(1.0, (_18._m13 >= 0.0) ? (1.0 - in.m_7.w) : in.m_7.w, abs(_18._m13)), 0.0, 1.0);
    }
    else
    {
        _426 = _412;
    }
    float4 _429 = _426 * in.m_3.w;
    _429.w = mix(_429.w, 0.0, _18._m22);
    out.m_12 = _429;
    return out;
}

