#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _6
{
    float _m0;
    float _m1;
    float _m2;
    float _m3;
    float _m4;
    float _m5;
    float _m6;
    float _m7;
};

struct main0_out
{
    float2 m_5 [[user(locn0)]];
    float4 gl_Position [[position]];
};

vertex main0_out main0(constant _6& _7 [[buffer(0)]], uint gl_VertexIndex [[vertex_id]])
{
    main0_out out = {};
    float _31 = float((gl_VertexIndex << 1u) & 2u);
    float _33 = float(gl_VertexIndex & 2u);
    float2 _36 = (float2(_31, _33) * 2.0) - float2(1.0);
    float _37 = _36.x;
    float4 _39 = float4(_37, _36.y, 1.0, 1.0);
    _39.x = _37 * (1.0 - (_7._m1 * 2.0));
    _39.y = -(_36.y * (1.0 - (_7._m2 * 2.0)));
    out.gl_Position = _39;
    out.m_5 = float2(_31, 1.0 - _33);
    return out;
}

