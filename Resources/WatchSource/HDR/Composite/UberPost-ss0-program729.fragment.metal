#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _10
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
    float4 _m9;
    float4 _m10;
    float4 _m11;
    float4 _m12;
    float4 _m13;
    float4 _m14;
    float4 _m15;
    float4 _m16;
    float4 _m17;
    float4 _m18;
    float4 _m19;
    float4 _m20;
    float4 _m21;
    float4 _m22;
    float4 _m23;
    float4 _m24;
    float4 _m25;
};

struct main0_out
{
    float4 m_4 [[color(0)]];
};

struct main0_in
{
    float2 m_3 [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _10& _11 [[buffer(0)]], texture2d<float> _7 [[texture(0)]], texture2d<float> _8 [[texture(1)]], sampler _9 [[sampler(0)]])
{
    main0_out out = {};
    float4 _50 = _7.sample(_9, in.m_3, level(0.0));
    float4 _54 = _8.sample(_9, in.m_3, level(0.0));
    float3 _55 = _54.xyz;
    float3 _65 = _50.xyz;
    float _74 = precise::max(precise::max(_50.x, _50.y), _50.z);
    float _78 = fast::clamp(_74 - _11._m10.y, 0.0, _11._m10.z);
    out.m_4 = float4(mix(_65, (_65 - ((_65 * (precise::max((_11._m10.w * _78) * _78, _74 - _11._m10.x) / precise::max(_74, 9.9999997473787516355514526367188e-05))) * _11._m9.z)) + (select(_55, (powr(_55, float3(0.3300000131130218505859375)) * 1.49380004405975341796875) - float3(0.699999988079071044921875), (_55 * (1.0 - _11._m9.z)) > float3(0.300000011920928955078125)).xyz * _11._m11.xyz), float3(_11._m9.x)), fast::clamp(_50.w + _54.w, 0.0, 1.0));
    return out;
}

