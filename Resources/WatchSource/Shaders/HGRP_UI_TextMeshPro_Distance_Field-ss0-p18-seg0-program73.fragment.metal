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
    bool _97 = _16._m2 > 0.5;
    float2 _175;
    if (_97)
    {
        float _104 = _12._m0.y * _16._m16;
        float2 _110 = in.m_8.xy / float2(precise::max(in.m_8.w, 9.9999997473787516355514526367188e-05));
        float2 _123 = float2(_110.x * (_20._m14.x / precise::max(_20._m14.y, 1.0)), _110.y) * _16._m15;
        float2 _126 = _123 + float2(_104, _104 * 0.829999983310699462890625);
        float2 _131 = (_123 * 1.7000000476837158203125) + float2(_104 * (-0.9700000286102294921875), _104 * 1.309999942779541015625);
        float2 _145 = float2(sin(_126.y) + (sin(_131.y) * 0.5), cos(_126.x) + (cos(_131.x) * 0.5)) * 0.66670000553131103515625;
        float _162 = precise::max(_18._m50 - 1.0, 0.0);
        _175 = fast::clamp((float2(int2(sign(_145))) * powr(abs(_145), float2(precise::max(_16._m17, 1.0)))) * _16._m14, float2(-_162), float2(_162)) * float2(1.0 / _18._m48, 1.0 / _18._m49);
    }
    else
    {
        _175 = float2(0.0);
    }
    float2 _176 = in.m_4 + _175;
    float4 _180 = _26.sample(_27, _176);
    float _181 = _180.x;
    bool _184 = _16._m18 > 0.5;
    float _234;
    if (_184)
    {
        float2 _196 = float2(1.0 / _18._m48, 1.0 / _18._m49) * _16._m20;
        float _199 = _196.x;
        float _217 = _196.y;
        _234 = ((((_181 + _26.sample(_27, (_176 + float2(_199, 0.0))).x) + _26.sample(_27, (_176 + float2(-_199, 0.0))).x) + _26.sample(_27, (_176 + float2(0.0, _217))).x) + _26.sample(_27, (_176 + float2(0.0, -_217))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _234 = _181;
    }
    if ((_234 - in.m_5.x) < 0.0)
    {
        discard_fragment();
    }
    float _246 = (in.m_5.z - _234) * in.m_5.y;
    float _252 = (_18._m8 * _18._m39) * in.m_5.y;
    float _256 = (_18._m4 * _18._m39) * in.m_5.y;
    float3 _263 = _18._m2.xyz * in.m_3.xyz;
    float4 _278 = _24.sample(_25, (in.m_7.xy + (float2(_18._m0, _18._m1) * _20._m12.y)));
    float4 _279 = float4(_263.x, _263.y, _263.z, _18._m2.w) * _278;
    float4 _291 = _22.sample(_23, (in.m_7.zw + (float2(_18._m5, _18._m6) * _20._m12.y)));
    float4 _292 = _18._m7 * _291;
    float4 _341;
    if (_184)
    {
        float _303 = (_16._m21 * _18._m39) * in.m_5.y;
        float3 _314 = _279.xyz * _279.w;
        _341 = float4(_314.x, _314.y, _314.z, _279.w) * (1.0 - fast::clamp(((_246 - (((_16._m19 * _18._m39) * in.m_5.y) * 0.5)) + (_303 * 0.5)) / (1.0 + _303), 0.0, 1.0));
    }
    else
    {
        float _317 = _252 * 0.5;
        float3 _332 = _279.xyz * _279.w;
        float3 _336 = _292.xyz * _292.w;
        _341 = mix(float4(_332.x, _332.y, _332.z, _279.w), float4(_336.x, _336.y, _336.z, _292.w), float4(fast::clamp(_246 + _317, 0.0, 1.0) * sqrt(precise::min(1.0, _252)))) * (1.0 - fast::clamp(((_246 - _317) + (_256 * 0.5)) / (1.0 + _256), 0.0, 1.0));
    }
    if ((_341.w - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float4 _356 = _28.sample(_29, ((in.m_9 * _18._m55.xy) + _18._m55.zw));
    float4 _388;
    if (_97)
    {
        _388 = _341 * fast::clamp(mix(1.0, (_16._m13 >= 0.0) ? (1.0 - in.m_6.w) : in.m_6.w, abs(_16._m13)), 0.0, 1.0);
    }
    else
    {
        _388 = _341;
    }
    float4 _389 = _388 * (in.m_3.w * fast::clamp((_356.w * (step(in.m_9.x, 1.0) * step(-in.m_9.x, 0.0))) * (step(in.m_9.y, 1.0) * step(-in.m_9.y, 0.0)), 0.0, 1.0));
    _389.w = mix(_389.w, 0.0, _16._m22);
    out.m_10 = _389;
    return out;
}

