#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct main0_out
{
    float2 m_4 [[user(locn0)]];
    float4 gl_Position [[position]];
};

vertex main0_out main0(uint gl_VertexIndex [[vertex_id]])
{
    main0_out out = {};
    float _23 = float((gl_VertexIndex << 1u) & 2u);
    float _25 = float(gl_VertexIndex & 2u);
    float2 _28 = (float2(_23, _25) * 2.0) - float2(1.0);
    float4 _31 = float4(_28, 1.0, 1.0);
    _31.y = -_28.y;
    out.gl_Position = _31;
    out.m_4 = float2(_23, 1.0 - _25);
    return out;
}

