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
    float _m19;
    float _m20;
    float _m21;
    float _m22;
    float _m23;
    float _m24;
    float _m25;
    float _m26;
    float _m27;
    float _m28;
    float4 _m29;
    float4 _m30;
    float4 _m31;
    float4 _m32;
    float4 _m33;
    float4 _m34;
    float4 _m35;
    float _m36;
    float _m37;
    float _m38;
    float4 _m39;
    float4 _m40;
    float4 _m41;
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

fragment main0_out main0(main0_in in [[stage_in]], constant _9& _10 [[buffer(0)]], constant _11& _13 [[buffer(1)]], constant _14& _15 [[buffer(2)]], texture2d<float> _17 [[texture(0)]], texture2d<float> _19 [[texture(1)]], texture2d<float> _21 [[texture(2)]], sampler _18 [[sampler(0)]], sampler _20 [[sampler(1)]], sampler _22 [[sampler(2)]])
{
    main0_out out = {};
    float4 _94 = in.m_3;
    _94.w = rint(_94.w * 255.0) * 0.0039215688593685626983642578125;
    float _113 = fmod(_10._m12.y, 1024.0);
    float4 _137 = _17.sample(_18, ((((float2x2(float2(_15._m17.xy), float2(_15._m17.zw)) * ((mix(in.m_4, in.m_4, float2(_15._m15)) + (_15._m16.xy * _113)) - float2(0.5))) + float2(0.5)) * _15._m18.xy) + _15._m18.zw));
    float4 _175 = _21.sample(_22, ((((float2x2(float2(_15._m41.xy), float2(_15._m41.zw)) * ((mix(in.m_4, in.m_4, float2(_15._m38)) + (_15._m40.xy * _113)) - float2(0.5))) + float2(0.5)) * _15._m39.xy) + _15._m39.zw));
    float4 _180 = (_94 * mix(_137, float4(1.0, 1.0, 1.0, _137.x), float4(_15._m14))) * mix(_175, float4(1.0, 1.0, 1.0, _175.x), float4(_15._m37));
    float2 _207 = (((float2x2(float2(_15._m31.xy), float2(_15._m31.zw)) * ((mix(in.m_4, in.m_4, float2(_15._m19)) + (_15._m30.xy * _113)) - float2(0.5))) + float2(0.5)) * _15._m29.xy) + _15._m29.zw;
    float4 _235 = _19.sample(_20, _207);
    float _249 = (_235.x - _15._m22) + mix(0.0, ((_15._m28 - 1.0) != 0.0) ? length(_207 - _15._m35.xy) : dot(fast::normalize(_15._m34.xy), _207), fast::clamp(_15._m28, 0.0, 1.0));
    if (_249 < 0.0)
    {
        discard_fragment();
    }
    float _256 = smoothstep(_15._m24, _15._m24 + _15._m25, 1.0 - fast::clamp(_249, 0.0, 1.0));
    float _257 = 1.0 - _15._m26;
    float2 _283 = fast::clamp(((_13._m0[0]._m3.zw - _13._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _288 = fast::clamp(_13._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _293 = ((in.m_5.xy + _288.xy) + _288.zw) * 0.5;
    float _294 = _293.x;
    float _299 = _293.y;
    float _319 = ((_180.w * mix(1.0, 1.0 - _256, _15._m23)) * (_283.x * _283.y)) * (((smoothstep(0.0, _13._m0[0]._m4.x, _294 - _288.x) * smoothstep(0.0, _13._m0[0]._m4.z, _288.z - _294)) * smoothstep(0.0, _13._m0[0]._m4.y, _299 - _288.y)) * smoothstep(0.0, _13._m0[0]._m4.w, _288.w - _299));
    float3 _321 = float3(mix(_180, mix(_15._m32, _15._m33, float4(smoothstep(_257, _257 + _15._m27, _256))) * _15._m5.x, float4(_256)).xyz).xyz * _319;
    float4 _322 = float4(_321.x, _321.y, _321.z, _180.w);
    _322.w = mix(_319, 0.0, _15._m8);
    out.m_6 = _322;
    return out;
}

