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
    float4 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn3)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _9& _10 [[buffer(0)]], constant _11& _13 [[buffer(1)]], constant _14& _15 [[buffer(2)]], texture2d<float> _17 [[texture(0)]], texture2d<float> _19 [[texture(1)]], sampler _18 [[sampler(0)]], sampler _20 [[sampler(1)]])
{
    main0_out out = {};
    float4 _72 = in.m_3;
    _72.w = rint(_72.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _116 = _19.sample(_20, ((((float2x2(float2(_15._m17.xy), float2(_15._m17.zw)) * ((mix(in.m_4.xy, in.m_4.xy, float2(_15._m15)) + (_15._m16.xy * fmod(_10._m12.y, 1024.0))) - float2(0.5))) + float2(0.5)) * _15._m18.xy) + _15._m18.zw));
    float4 _121 = _72 * mix(_116, float4(1.0, 1.0, 1.0, _116.x), float4(_15._m14));
    float2 _194 = select(in.m_4.zw, float2((in.m_4.z < 0.0) ? in.m_4.z : (((in.m_4.z >= 0.0) && (in.m_4.z <= _15._m2.x)) ? ((in.m_4.z * _15._m3.x) / _15._m2.x) : ((in.m_4.z <= _15._m2.y) ? ((((in.m_4.z - _15._m2.x) * (_15._m3.y - _15._m3.x)) / (_15._m2.y - _15._m2.x)) + _15._m3.x) : ((in.m_4.z <= 1.0) ? ((((in.m_4.z - _15._m2.y) * (1.0 - _15._m3.y)) / (1.0 - _15._m2.y)) + _15._m3.y) : in.m_4.z))), (in.m_4.w < 0.0) ? in.m_4.w : (((in.m_4.w >= 0.0) && (in.m_4.w <= _15._m2.z)) ? ((in.m_4.w * _15._m3.z) / _15._m2.z) : ((in.m_4.w <= _15._m2.w) ? ((((in.m_4.w - _15._m2.z) * (_15._m3.w - _15._m3.z)) / (_15._m2.w - _15._m2.z)) + _15._m3.z) : ((in.m_4.w <= 1.0) ? ((((in.m_4.w - _15._m2.w) * (1.0 - _15._m3.w)) / (1.0 - _15._m2.w)) + _15._m3.w) : in.m_4.w)))), bool2(_15._m4 != 0.0));
    float2 _231 = fast::clamp(((_13._m0[0]._m3.zw - _13._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _236 = fast::clamp(_13._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _241 = ((in.m_5.xy + _236.xy) + _236.zw) * 0.5;
    float _242 = _241.x;
    float _247 = _241.y;
    float _267 = ((_121.w * ((_17.sample(_18, ((_194.xy * _15._m0.xy) + _15._m0.zw)).w * (step(_194.x, 1.0) * step(-_194.x, 0.0))) * (step(_194.y, 1.0) * step(-_194.y, 0.0)))) * (_231.x * _231.y)) * (((smoothstep(0.0, _13._m0[0]._m4.x, _242 - _236.x) * smoothstep(0.0, _13._m0[0]._m4.z, _236.z - _242)) * smoothstep(0.0, _13._m0[0]._m4.y, _247 - _236.y)) * smoothstep(0.0, _13._m0[0]._m4.w, _236.w - _247));
    if ((_267 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float3 _273 = _121.xyz * _267;
    float4 _274 = float4(_273.x, _273.y, _273.z, _121.w);
    _274.w = mix(_267, 0.0, _15._m8);
    out.m_6 = _274;
    return out;
}

