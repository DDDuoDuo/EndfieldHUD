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
    float _m13;
    float _m14;
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
    float4 _62 = in.m_3;
    _62.w = rint(_62.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _74 = _15.sample(_16, in.m_4.xy);
    _74.w = (_13._m12 != 0.0) ? 1.0 : _74.w;
    float4 _84 = _62 * (_74 + _11._m0[0]._m2);
    float2 _157 = select(in.m_4.zw, float2((in.m_4.z < 0.0) ? in.m_4.z : (((in.m_4.z >= 0.0) && (in.m_4.z <= _13._m2.x)) ? ((in.m_4.z * _13._m3.x) / _13._m2.x) : ((in.m_4.z <= _13._m2.y) ? ((((in.m_4.z - _13._m2.x) * (_13._m3.y - _13._m3.x)) / (_13._m2.y - _13._m2.x)) + _13._m3.x) : ((in.m_4.z <= 1.0) ? ((((in.m_4.z - _13._m2.y) * (1.0 - _13._m3.y)) / (1.0 - _13._m2.y)) + _13._m3.y) : in.m_4.z))), (in.m_4.w < 0.0) ? in.m_4.w : (((in.m_4.w >= 0.0) && (in.m_4.w <= _13._m2.z)) ? ((in.m_4.w * _13._m3.z) / _13._m2.z) : ((in.m_4.w <= _13._m2.w) ? ((((in.m_4.w - _13._m2.z) * (_13._m3.w - _13._m3.z)) / (_13._m2.w - _13._m2.z)) + _13._m3.z) : ((in.m_4.w <= 1.0) ? ((((in.m_4.w - _13._m2.w) * (1.0 - _13._m3.w)) / (1.0 - _13._m2.w)) + _13._m3.w) : in.m_4.w)))), bool2(_13._m4 != 0.0));
    float4 _168 = _17.sample(_18, ((_157.xy * _13._m0.xy) + _13._m0.zw));
    float2 _194 = fast::clamp(((_11._m0[0]._m3.zw - _11._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _199 = fast::clamp(_11._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _204 = ((in.m_5.xy + _199.xy) + _199.zw) * 0.5;
    float _205 = _204.x;
    float _210 = _204.y;
    float _230 = ((_84.w * ((_168.w * (step(_157.x, 1.0) * step(-_157.x, 0.0))) * (step(_157.y, 1.0) * step(-_157.y, 0.0)))) * (_194.x * _194.y)) * (((smoothstep(0.0, _11._m0[0]._m4.x, _205 - _199.x) * smoothstep(0.0, _11._m0[0]._m4.z, _199.z - _205)) * smoothstep(0.0, _11._m0[0]._m4.y, _210 - _199.y)) * smoothstep(0.0, _11._m0[0]._m4.w, _199.w - _210));
    if ((_230 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float _237 = _230 * _13._m13;
    float3 _239 = _84.xyz * _237;
    float4 _240 = float4(_239.x, _239.y, _239.z, _84.w);
    _240.w = mix(_237, 0.0, _13._m8);
    out.m_6 = _240;
    return out;
}

