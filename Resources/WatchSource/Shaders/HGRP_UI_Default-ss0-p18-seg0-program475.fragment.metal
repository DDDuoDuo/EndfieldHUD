#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _9
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

struct _12
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _11
{
    _12 _m0[1];
};

struct _14
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
};

struct main0_out
{
    float4 m_6 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn3)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _9& _10 [[buffer(0)]], constant _11& _13 [[buffer(1)]], constant _14& _15 [[buffer(2)]], texture2d<float> _17 [[texture(0)]], sampler _18 [[sampler(0)]])
{
    main0_out out = {};
    float4 _69 = in.m_3;
    _69.w = rint(_69.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _112 = _17.sample(_18, ((((float2x2(float2(_15._m17.xy), float2(_15._m17.zw)) * ((mix(in.m_4, in.m_4, float2(_15._m15)) + (_15._m16.xy * fmod(_10._m12.y, 1024.0))) - float2(0.5))) + float2(0.5)) * _15._m18.xy) + _15._m18.zw));
    float4 _117 = _69 * mix(_112, float4(1.0, 1.0, 1.0, _112.x), float4(_15._m14));
    float2 _128 = fast::clamp(((_13._m0[0]._m3.zw - _13._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _134 = fast::clamp(_13._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _139 = ((in.m_5.xy + _134.xy) + _134.zw) * 0.5;
    float _140 = _139.x;
    float _145 = _139.y;
    float _165 = (_117.w * (_128.x * _128.y)) * (((smoothstep(0.0, _13._m0[0]._m4.x, _140 - _134.x) * smoothstep(0.0, _13._m0[0]._m4.z, _134.z - _140)) * smoothstep(0.0, _13._m0[0]._m4.y, _145 - _134.y)) * smoothstep(0.0, _13._m0[0]._m4.w, _134.w - _145));
    if ((_165 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float3 _171 = _117.xyz * _165;
    float4 _172 = float4(_171.x, _171.y, _171.z, _117.w);
    _172.w = mix(_165, 0.0, _15._m8);
    out.m_6 = _172;
    return out;
}

