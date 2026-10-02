#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _10
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _9
{
    _10 _m0[1];
};

struct _12
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

fragment main0_out main0(main0_in in [[stage_in]], constant _9& _11 [[buffer(0)]], constant _12& _13 [[buffer(1)]], texture2d<float> _15 [[texture(0)]], texture2d<float> _17 [[texture(1)]], sampler _16 [[sampler(0)]], sampler _18 [[sampler(1)]])
{
    main0_out out = {};
    float4 _61 = in.m_3;
    _61.w = rint(_61.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _73 = _15.sample(_16, in.m_4.xy);
    _73.w = (_13._m12 != 0.0) ? 1.0 : _73.w;
    float4 _83 = _61 * (_73 + _11._m0[0]._m2);
    float2 _156 = select(in.m_4.zw, float2((in.m_4.z < 0.0) ? in.m_4.z : (((in.m_4.z >= 0.0) && (in.m_4.z <= _13._m2.x)) ? ((in.m_4.z * _13._m3.x) / _13._m2.x) : ((in.m_4.z <= _13._m2.y) ? ((((in.m_4.z - _13._m2.x) * (_13._m3.y - _13._m3.x)) / (_13._m2.y - _13._m2.x)) + _13._m3.x) : ((in.m_4.z <= 1.0) ? ((((in.m_4.z - _13._m2.y) * (1.0 - _13._m3.y)) / (1.0 - _13._m2.y)) + _13._m3.y) : in.m_4.z))), (in.m_4.w < 0.0) ? in.m_4.w : (((in.m_4.w >= 0.0) && (in.m_4.w <= _13._m2.z)) ? ((in.m_4.w * _13._m3.z) / _13._m2.z) : ((in.m_4.w <= _13._m2.w) ? ((((in.m_4.w - _13._m2.z) * (_13._m3.w - _13._m3.z)) / (_13._m2.w - _13._m2.z)) + _13._m3.z) : ((in.m_4.w <= 1.0) ? ((((in.m_4.w - _13._m2.w) * (1.0 - _13._m3.w)) / (1.0 - _13._m2.w)) + _13._m3.w) : in.m_4.w)))), bool2(_13._m4 != 0.0));
    float4 _167 = _17.sample(_18, ((_156.xy * _13._m0.xy) + _13._m0.zw));
    float2 _193 = fast::clamp(((_11._m0[0]._m3.zw - _11._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _198 = fast::clamp(_11._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _203 = ((in.m_5.xy + _198.xy) + _198.zw) * 0.5;
    float _204 = _203.x;
    float _209 = _203.y;
    float _229 = ((_83.w * ((_167.w * (step(_156.x, 1.0) * step(-_156.x, 0.0))) * (step(_156.y, 1.0) * step(-_156.y, 0.0)))) * (_193.x * _193.y)) * (((smoothstep(0.0, _11._m0[0]._m4.x, _204 - _198.x) * smoothstep(0.0, _11._m0[0]._m4.z, _198.z - _204)) * smoothstep(0.0, _11._m0[0]._m4.y, _209 - _198.y)) * smoothstep(0.0, _11._m0[0]._m4.w, _198.w - _209));
    if ((_229 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float3 _235 = _83.xyz * _229;
    float4 _236 = float4(_235.x, _235.y, _235.z, _83.w);
    _236.w = mix(_229, 0.0, _13._m8);
    out.m_6 = _236;
    return out;
}

