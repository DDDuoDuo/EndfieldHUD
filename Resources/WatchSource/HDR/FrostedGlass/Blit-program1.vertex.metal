#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _5
{
    float4 _m0;
    float4 _m1;
    float _m2;
    packed_float2 _m3;
    uint _m4;
    int _m5;
};

struct main0_out
{
    float2 m_4 [[user(locn0)]];
    float4 gl_Position [[position]];
};

vertex main0_out main0(constant _5& _6 [[buffer(0)]], uint gl_VertexIndex [[vertex_id]])
{
    main0_out out = {};
    float _29 = float((gl_VertexIndex << 1u) & 2u);
    float _31 = float(gl_VertexIndex & 2u);
    float2 _34 = (float2(_29, _31) * 2.0) - float2(1.0);
    float4 _37 = float4(_34, 1.0, 1.0);
    _37.y = -_34.y;
    out.gl_Position = _37;
    out.m_4 = (float2(_29, 1.0 - _31) * _6._m0.xy) + _6._m0.zw;
    return out;
}

