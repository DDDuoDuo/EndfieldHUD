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
    bool _110 = _18._m2 > 0.5;
    float2 _188;
    if (_110)
    {
        float _117 = _14._m0.y * _18._m16;
        float2 _123 = in.m_11.xy / float2(precise::max(in.m_11.w, 9.9999997473787516355514526367188e-05));
        float2 _136 = float2(_123.x * (_22._m14.x / precise::max(_22._m14.y, 1.0)), _123.y) * _18._m15;
        float2 _139 = _136 + float2(_117, _117 * 0.829999983310699462890625);
        float2 _144 = (_136 * 1.7000000476837158203125) + float2(_117 * (-0.9700000286102294921875), _117 * 1.309999942779541015625);
        float2 _158 = float2(sin(_139.y) + (sin(_144.y) * 0.5), cos(_139.x) + (cos(_144.x) * 0.5)) * 0.66670000553131103515625;
        float _175 = precise::max(_20._m50 - 1.0, 0.0);
        _188 = fast::clamp((float2(int2(sign(_158))) * powr(abs(_158), float2(precise::max(_18._m17, 1.0)))) * _18._m14, float2(-_175), float2(_175)) * float2(1.0 / _20._m48, 1.0 / _20._m49);
    }
    else
    {
        _188 = float2(0.0);
    }
    float2 _189 = in.m_4 + _188;
    float4 _193 = _31.sample(_32, _189);
    float _194 = _193.x;
    bool _197 = _18._m18 > 0.5;
    float _247;
    if (_197)
    {
        float2 _209 = float2(1.0 / _20._m48, 1.0 / _20._m49) * _18._m20;
        float _212 = _209.x;
        float _230 = _209.y;
        _247 = ((((_194 + _31.sample(_32, (_189 + float2(_212, 0.0))).x) + _31.sample(_32, (_189 + float2(-_212, 0.0))).x) + _31.sample(_32, (_189 + float2(0.0, _230))).x) + _31.sample(_32, (_189 + float2(0.0, -_230))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _247 = _194;
    }
    float _253 = (in.m_5.z - _247) * in.m_5.y;
    float _259 = (_20._m8 * _20._m39) * in.m_5.y;
    float _263 = (_20._m4 * _20._m39) * in.m_5.y;
    float3 _270 = _20._m2.xyz * in.m_3.xyz;
    float4 _285 = _29.sample(_30, (in.m_10.xy + (float2(_20._m0, _20._m1) * _22._m12.y)));
    float4 _286 = float4(_270.x, _270.y, _270.z, _20._m2.w) * _285;
    float4 _298 = _27.sample(_28, (in.m_10.zw + (float2(_20._m5, _20._m6) * _22._m12.y)));
    float4 _299 = _20._m7 * _298;
    float4 _348;
    if (_197)
    {
        float _310 = (_18._m21 * _20._m39) * in.m_5.y;
        float3 _321 = _286.xyz * _286.w;
        _348 = float4(_321.x, _321.y, _321.z, _286.w) * (1.0 - fast::clamp(((_253 - (((_18._m19 * _20._m39) * in.m_5.y) * 0.5)) + (_310 * 0.5)) / (1.0 + _310), 0.0, 1.0));
    }
    else
    {
        float _324 = _259 * 0.5;
        float3 _339 = _286.xyz * _286.w;
        float3 _343 = _299.xyz * _299.w;
        _348 = mix(float4(_339.x, _339.y, _339.z, _286.w), float4(_343.x, _343.y, _343.z, _299.w), float4(fast::clamp(_253 + _324, 0.0, 1.0) * sqrt(precise::min(1.0, _259)))) * (1.0 - fast::clamp(((_253 - _324) + (_263 * 0.5)) / (1.0 + _263), 0.0, 1.0));
    }
    float4 _353 = _31.sample(_32, in.m_8.xy);
    float2 _377 = fast::clamp(((_25._m0[0]._m3.zw - _25._m0[0]._m3.xy) - abs(in.m_6.xy)) * in.m_6.zw, float2(0.0), float2(1.0));
    float4 _382 = fast::clamp(_25._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _387 = ((in.m_6.xy + _382.xy) + _382.zw) * 0.5;
    float _388 = _387.x;
    float _393 = _387.y;
    float4 _413 = ((_348 + ((in.m_9 * fast::clamp((_353.x * in.m_8.z) - in.m_8.w, 0.0, 1.0)) * (1.0 - _348.w))) * (_377.x * _377.y)) * (((smoothstep(0.0, _25._m0[0]._m4.x, _388 - _382.x) * smoothstep(0.0, _25._m0[0]._m4.z, _382.z - _388)) * smoothstep(0.0, _25._m0[0]._m4.y, _393 - _382.y)) * smoothstep(0.0, _25._m0[0]._m4.w, _382.w - _393));
    if ((_413.w - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float4 _432;
    if (_110)
    {
        _432 = _413 * fast::clamp(mix(1.0, (_18._m13 >= 0.0) ? (1.0 - in.m_7.w) : in.m_7.w, abs(_18._m13)), 0.0, 1.0);
    }
    else
    {
        _432 = _413;
    }
    float4 _435 = _432 * in.m_3.w;
    _435.w = mix(_435.w, 0.0, _18._m22);
    out.m_12 = _435;
    return out;
}

