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
    float2 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn3)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _9& _11 [[buffer(0)]], constant _12& _13 [[buffer(1)]], texture2d<float> _15 [[texture(0)]], sampler _16 [[sampler(0)]])
{
    main0_out out = {};
    float4 _59 = in.m_3;
    _59.w = rint(_59.w * 255.0) * 0.0039215688593685626983642578125;
    float4 _70 = _15.sample(_16, in.m_4);
    _70.w = (_13._m12 != 0.0) ? 1.0 : _70.w;
    float4 _80 = _59 * (_70 + _11._m0[0]._m2);
    float2 _91 = fast::clamp(((_11._m0[0]._m3.zw - _11._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _97 = fast::clamp(_11._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _102 = ((in.m_5.xy + _97.xy) + _97.zw) * 0.5;
    float _103 = _102.x;
    float _108 = _102.y;
    float _128 = (_80.w * (_91.x * _91.y)) * (((smoothstep(0.0, _11._m0[0]._m4.x, _103 - _97.x) * smoothstep(0.0, _11._m0[0]._m4.z, _97.z - _103)) * smoothstep(0.0, _11._m0[0]._m4.y, _108 - _97.y)) * smoothstep(0.0, _11._m0[0]._m4.w, _97.w - _108));
    if ((_128 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float _135 = _128 * _13._m13;
    float3 _137 = _80.xyz * _135;
    float4 _138 = float4(_137.x, _137.y, _137.z, _80.w);
    _138.w = mix(_135, 0.0, _13._m8);
    out.m_6 = _138;
    return out;
}

