#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _9
{
    float4x4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
};

struct _8
{
    _9 _m0[1];
};

struct _11
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
    float4 m_5 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float4 m_4 [[user(locn1)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _8& _10 [[buffer(0)]], constant _11& _12 [[buffer(1)]], texture2d<float> _14 [[texture(0)]], texture2d<float> _16 [[texture(1)]], sampler _15 [[sampler(0)]], sampler _17 [[sampler(1)]])
{
    main0_out out = {};
    float4 _52 = in.m_3;
    _52.w = rint(_52.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _63 = _14.sample(_15, in.m_4.xy);
    _63.w = (_12._m12 != 0.0) ? 1.0 : _63.w;
    float4 _73 = _52 * (_63 + _10._m0[0]._m2);
    float2 _146 = select(in.m_4.zw, float2((in.m_4.z < 0.0) ? in.m_4.z : (((in.m_4.z >= 0.0) && (in.m_4.z <= _12._m2.x)) ? ((in.m_4.z * _12._m3.x) / _12._m2.x) : ((in.m_4.z <= _12._m2.y) ? ((((in.m_4.z - _12._m2.x) * (_12._m3.y - _12._m3.x)) / (_12._m2.y - _12._m2.x)) + _12._m3.x) : ((in.m_4.z <= 1.0) ? ((((in.m_4.z - _12._m2.y) * (1.0 - _12._m3.y)) / (1.0 - _12._m2.y)) + _12._m3.y) : in.m_4.z))), (in.m_4.w < 0.0) ? in.m_4.w : (((in.m_4.w >= 0.0) && (in.m_4.w <= _12._m2.z)) ? ((in.m_4.w * _12._m3.z) / _12._m2.z) : ((in.m_4.w <= _12._m2.w) ? ((((in.m_4.w - _12._m2.z) * (_12._m3.w - _12._m3.z)) / (_12._m2.w - _12._m2.z)) + _12._m3.z) : ((in.m_4.w <= 1.0) ? ((((in.m_4.w - _12._m2.w) * (1.0 - _12._m3.w)) / (1.0 - _12._m2.w)) + _12._m3.w) : in.m_4.w)))), bool2(_12._m4 != 0.0));
    float _172 = _73.w * ((_16.sample(_17, ((_146.xy * _12._m0.xy) + _12._m0.zw)).w * (step(_146.x, 1.0) * step(-_146.x, 0.0))) * (step(_146.y, 1.0) * step(-_146.y, 0.0)));
    float3 _174 = _73.xyz * _172;
    float4 _175 = float4(_174.x, _174.y, _174.z, _73.w);
    _175.w = mix(_172, 0.0, _12._m8);
    out.m_5 = _175;
    return out;
}

