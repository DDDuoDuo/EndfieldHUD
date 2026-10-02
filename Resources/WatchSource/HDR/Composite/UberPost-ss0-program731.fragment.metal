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

fragment main0_out main0(main0_in in [[stage_in]], constant _12& _13 [[buffer(0)]], texture2d<float> _7 [[texture(0)]], texture2d<float> _8 [[texture(1)]], texture2d<float> _9 [[texture(2)]], sampler _10 [[sampler(0)]], sampler _11 [[sampler(1)]])
{
    main0_out out = {};
    float4 _58 = _7.sample(_11, in.m_3, level(0.0));
    float4 _62 = _8.sample(_11, in.m_3, level(0.0));
    float3 _63 = _62.xyz;
    float3 _73 = _58.xyz;
    float _82 = precise::max(precise::max(_58.x, _58.y), _58.z);
    float _86 = fast::clamp(_82 - _13._m10.y, 0.0, _13._m10.z);
    float4 _114 = float4(mix(_73, (_73 - ((_73 * (precise::max((_13._m10.w * _86) * _86, _82 - _13._m10.x) / precise::max(_82, 9.9999997473787516355514526367188e-05))) * _13._m9.z)) + (select(_63, (powr(_63, float3(0.3300000131130218505859375)) * 1.49380004405975341796875) - float3(0.699999988079071044921875), (_63 * (1.0 - _13._m9.z)) > float3(0.300000011920928955078125)).xyz * _13._m11.xyz), float3(_13._m9.x)), fast::clamp(_58.w + _62.w, 0.0, 1.0));
    float4 _118 = _9.sample(_10, in.m_3, level(0.0));
    float _119 = _118.w;
    float3 _125 = _114.xyz;
    float3 _135 = mix(_125, _125 * mix(_13._m4.xyz, float3(1.0), float3(_119 * ((_119 * ((_119 * 0.305306017398834228515625) + 0.6821711063385009765625)) + 0.01252287812530994415283203125))), float3(_13._m4.w));
    out.m_4 = float4(_135.x, _135.y, _135.z, _114.w);
    return out;
}

