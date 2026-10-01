#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _8
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

struct _10
{
    float4 _m0;
    float4x4 _m1;
    float4 _m2;
    float4 _m3;
    float _m4;
    float4 _m5;
    float4 _m6;
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
    float4 _m18;
    float _m19;
    float _m20;
    float _m21;
    float _m22;
    float _m23;
    float _m24;
    float _m25;
    float _m26;
    float _m27;
    float _m28;
    float4 _m29;
    float4 _m30;
    float4 _m31;
    float4 _m32;
    float4 _m33;
    float4 _m34;
    float4 _m35;
    float _m36;
    float _m37;
    float _m38;
    float4 _m39;
    float4 _m40;
    float4 _m41;
};

struct main0_out
{
    float4 m_5 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float4 m_4 [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _9 [[buffer(0)]], constant _10& _11 [[buffer(1)]], texture2d<float> _13 [[texture(0)]], texture2d<float> _15 [[texture(1)]], texture2d<float> _17 [[texture(2)]], texture2d<float> _19 [[texture(3)]], sampler _14 [[sampler(0)]], sampler _16 [[sampler(1)]], sampler _18 [[sampler(2)]], sampler _20 [[sampler(3)]])
{
    main0_out out = {};
    float4 _83 = in.m_3;
    _83.w = rint(_83.w * 255.0) * 0.0039215688593685626983642578125;
    float _102 = fmod(_9._m12.y, 1024.0);
    float4 _126 = _15.sample(_16, ((((float2x2(float2(_11._m17.xy), float2(_11._m17.zw)) * ((mix(in.m_4.xy, in.m_4.xy, float2(_11._m15)) + (_11._m16.xy * _102)) - float2(0.5))) + float2(0.5)) * _11._m18.xy) + _11._m18.zw));
    float4 _164 = _19.sample(_20, ((((float2x2(float2(_11._m41.xy), float2(_11._m41.zw)) * ((mix(in.m_4.xy, in.m_4.xy, float2(_11._m38)) + (_11._m40.xy * _102)) - float2(0.5))) + float2(0.5)) * _11._m39.xy) + _11._m39.zw));
    float4 _169 = (_83 * mix(_126, float4(1.0, 1.0, 1.0, _126.x), float4(_11._m14))) * mix(_164, float4(1.0, 1.0, 1.0, _164.x), float4(_11._m37));
    float2 _196 = (((float2x2(float2(_11._m31.xy), float2(_11._m31.zw)) * ((mix(in.m_4.xy, in.m_4.xy, float2(_11._m19)) + (_11._m30.xy * _102)) - float2(0.5))) + float2(0.5)) * _11._m29.xy) + _11._m29.zw;
    float4 _224 = _17.sample(_18, _196);
    float _238 = (_224.x - _11._m22) + mix(0.0, ((_11._m28 - 1.0) != 0.0) ? length(_196 - _11._m35.xy) : dot(fast::normalize(_11._m34.xy), _196), fast::clamp(_11._m28, 0.0, 1.0));
    if (_238 < 0.0)
    {
        discard_fragment();
    }
    float _245 = smoothstep(_11._m24, _11._m24 + _11._m25, 1.0 - fast::clamp(_238, 0.0, 1.0));
    float _246 = 1.0 - _11._m26;
    float2 _334 = select(in.m_4.zw, float2((in.m_4.z < 0.0) ? in.m_4.z : (((in.m_4.z >= 0.0) && (in.m_4.z <= _11._m2.x)) ? ((in.m_4.z * _11._m3.x) / _11._m2.x) : ((in.m_4.z <= _11._m2.y) ? ((((in.m_4.z - _11._m2.x) * (_11._m3.y - _11._m3.x)) / (_11._m2.y - _11._m2.x)) + _11._m3.x) : ((in.m_4.z <= 1.0) ? ((((in.m_4.z - _11._m2.y) * (1.0 - _11._m3.y)) / (1.0 - _11._m2.y)) + _11._m3.y) : in.m_4.z))), (in.m_4.w < 0.0) ? in.m_4.w : (((in.m_4.w >= 0.0) && (in.m_4.w <= _11._m2.z)) ? ((in.m_4.w * _11._m3.z) / _11._m2.z) : ((in.m_4.w <= _11._m2.w) ? ((((in.m_4.w - _11._m2.z) * (_11._m3.w - _11._m3.z)) / (_11._m2.w - _11._m2.z)) + _11._m3.z) : ((in.m_4.w <= 1.0) ? ((((in.m_4.w - _11._m2.w) * (1.0 - _11._m3.w)) / (1.0 - _11._m2.w)) + _11._m3.w) : in.m_4.w)))), bool2(_11._m4 != 0.0));
    float4 _345 = _13.sample(_14, ((_334.xy * _11._m0.xy) + _11._m0.zw));
    float _359 = (_169.w * mix(1.0, 1.0 - _245, _11._m23)) * ((_345.w * (step(_334.x, 1.0) * step(-_334.x, 0.0))) * (step(_334.y, 1.0) * step(-_334.y, 0.0)));
    if ((_359 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float3 _365 = float3(mix(_169, mix(_11._m32, _11._m33, float4(smoothstep(_246, _246 + _11._m27, _245))) * _11._m5.x, float4(_245)).xyz).xyz * _359;
    float4 _366 = float4(_365.x, _365.y, _365.z, _169.w);
    _366.w = mix(_359, 0.0, _11._m8);
    out.m_5 = _366;
    return out;
}

