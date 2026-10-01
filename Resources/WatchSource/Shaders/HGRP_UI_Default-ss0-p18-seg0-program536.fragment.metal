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
    float4 _95 = in.m_3;
    _95.w = rint(_95.w * 255.0) * 0.0039215688593685626983642578125;
    float _114 = fmod(_10._m12.y, 1024.0);
    float4 _138 = _17.sample(_18, ((((float2x2(float2(_15._m17.xy), float2(_15._m17.zw)) * ((mix(in.m_4, in.m_4, float2(_15._m15)) + (_15._m16.xy * _114)) - float2(0.5))) + float2(0.5)) * _15._m18.xy) + _15._m18.zw));
    float4 _176 = _21.sample(_22, ((((float2x2(float2(_15._m41.xy), float2(_15._m41.zw)) * ((mix(in.m_4, in.m_4, float2(_15._m38)) + (_15._m40.xy * _114)) - float2(0.5))) + float2(0.5)) * _15._m39.xy) + _15._m39.zw));
    float4 _181 = (_95 * mix(_138, float4(1.0, 1.0, 1.0, _138.x), float4(_15._m14))) * mix(_176, float4(1.0, 1.0, 1.0, _176.x), float4(_15._m37));
    float2 _208 = (((float2x2(float2(_15._m31.xy), float2(_15._m31.zw)) * ((mix(in.m_4, in.m_4, float2(_15._m19)) + (_15._m30.xy * _114)) - float2(0.5))) + float2(0.5)) * _15._m29.xy) + _15._m29.zw;
    float4 _236 = _19.sample(_20, _208);
    float _250 = (_236.x - _15._m22) + mix(0.0, ((_15._m28 - 1.0) != 0.0) ? length(_208 - _15._m35.xy) : dot(fast::normalize(_15._m34.xy), _208), fast::clamp(_15._m28, 0.0, 1.0));
    if (_250 < 0.0)
    {
        discard_fragment();
    }
    float _257 = smoothstep(_15._m24, _15._m24 + _15._m25, 1.0 - fast::clamp(_250, 0.0, 1.0));
    float _258 = 1.0 - _15._m26;
    float2 _284 = fast::clamp(((_13._m0[0]._m3.zw - _13._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _289 = fast::clamp(_13._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _294 = ((in.m_5.xy + _289.xy) + _289.zw) * 0.5;
    float _295 = _294.x;
    float _300 = _294.y;
    float _320 = ((_181.w * mix(1.0, 1.0 - _257, _15._m23)) * (_284.x * _284.y)) * (((smoothstep(0.0, _13._m0[0]._m4.x, _295 - _289.x) * smoothstep(0.0, _13._m0[0]._m4.z, _289.z - _295)) * smoothstep(0.0, _13._m0[0]._m4.y, _300 - _289.y)) * smoothstep(0.0, _13._m0[0]._m4.w, _289.w - _300));
    if ((_320 - 0.001000000047497451305389404296875) < 0.0)
    {
        discard_fragment();
    }
    float3 _326 = float3(mix(_181, mix(_15._m32, _15._m33, float4(smoothstep(_258, _258 + _15._m27, _257))) * _15._m5.x, float4(_257)).xyz).xyz * _320;
    float4 _327 = float4(_326.x, _326.y, _326.z, _181.w);
    _327.w = mix(_320, 0.0, _15._m8);
    out.m_6 = _327;
    return out;
}

