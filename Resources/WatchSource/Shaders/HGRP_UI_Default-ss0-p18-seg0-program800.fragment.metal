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
    float4 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn3)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _9& _10 [[buffer(0)]], constant _11& _13 [[buffer(1)]], constant _14& _15 [[buffer(2)]], texture2d<float> _17 [[texture(0)]], texture2d<float> _19 [[texture(1)]], texture2d<float> _21 [[texture(2)]], texture2d<float> _23 [[texture(3)]], sampler _18 [[sampler(0)]], sampler _20 [[sampler(1)]], sampler _22 [[sampler(2)]], sampler _24 [[sampler(3)]])
{
    main0_out out = {};
    float4 _96 = in.m_3;
    _96.w = rint(_96.w * 255.0) * 0.0039215688593685626983642578125;
    float _116 = fmod(_10._m12.y, 1024.0);
    float4 _140 = _19.sample(_20, ((((float2x2(float2(_15._m17.xy), float2(_15._m17.zw)) * ((mix(in.m_4.xy, in.m_4.xy, float2(_15._m15)) + (_15._m16.xy * _116)) - float2(0.5))) + float2(0.5)) * _15._m18.xy) + _15._m18.zw));
    float4 _178 = _23.sample(_24, ((((float2x2(float2(_15._m41.xy), float2(_15._m41.zw)) * ((mix(in.m_4.xy, in.m_4.xy, float2(_15._m38)) + (_15._m40.xy * _116)) - float2(0.5))) + float2(0.5)) * _15._m39.xy) + _15._m39.zw));
    float4 _183 = (_96 * mix(_140, float4(1.0, 1.0, 1.0, _140.x), float4(_15._m14))) * mix(_178, float4(1.0, 1.0, 1.0, _178.x), float4(_15._m37));
    float2 _210 = (((float2x2(float2(_15._m31.xy), float2(_15._m31.zw)) * ((mix(in.m_4.xy, in.m_4.xy, float2(_15._m19)) + (_15._m30.xy * _116)) - float2(0.5))) + float2(0.5)) * _15._m29.xy) + _15._m29.zw;
    float4 _238 = _21.sample(_22, _210);
    float _252 = (_238.x - _15._m22) + mix(0.0, ((_15._m28 - 1.0) != 0.0) ? length(_210 - _15._m35.xy) : dot(fast::normalize(_15._m34.xy), _210), fast::clamp(_15._m28, 0.0, 1.0));
    if (_252 < 0.0)
    {
        discard_fragment();
    }
    float _259 = smoothstep(_15._m24, _15._m24 + _15._m25, 1.0 - fast::clamp(_252, 0.0, 1.0));
    float _260 = 1.0 - _15._m26;
    float2 _348 = select(in.m_4.zw, float2((in.m_4.z < 0.0) ? in.m_4.z : (((in.m_4.z >= 0.0) && (in.m_4.z <= _15._m2.x)) ? ((in.m_4.z * _15._m3.x) / _15._m2.x) : ((in.m_4.z <= _15._m2.y) ? ((((in.m_4.z - _15._m2.x) * (_15._m3.y - _15._m3.x)) / (_15._m2.y - _15._m2.x)) + _15._m3.x) : ((in.m_4.z <= 1.0) ? ((((in.m_4.z - _15._m2.y) * (1.0 - _15._m3.y)) / (1.0 - _15._m2.y)) + _15._m3.y) : in.m_4.z))), (in.m_4.w < 0.0) ? in.m_4.w : (((in.m_4.w >= 0.0) && (in.m_4.w <= _15._m2.z)) ? ((in.m_4.w * _15._m3.z) / _15._m2.z) : ((in.m_4.w <= _15._m2.w) ? ((((in.m_4.w - _15._m2.z) * (_15._m3.w - _15._m3.z)) / (_15._m2.w - _15._m2.z)) + _15._m3.z) : ((in.m_4.w <= 1.0) ? ((((in.m_4.w - _15._m2.w) * (1.0 - _15._m3.w)) / (1.0 - _15._m2.w)) + _15._m3.w) : in.m_4.w)))), bool2(_15._m4 != 0.0));
    float2 _384 = fast::clamp(((_13._m0[0]._m3.zw - _13._m0[0]._m3.xy) - abs(in.m_5.xy)) * in.m_5.zw, float2(0.0), float2(1.0));
    float4 _389 = fast::clamp(_13._m0[0]._m3, float4(-20000000000.0), float4(20000000000.0));
    float2 _394 = ((in.m_5.xy + _389.xy) + _389.zw) * 0.5;
    float _395 = _394.x;
    float _400 = _394.y;
    float _420 = (((_183.w * mix(1.0, 1.0 - _259, _15._m23)) * ((_17.sample(_18, ((_348.xy * _15._m0.xy) + _15._m0.zw)).w * (step(_348.x, 1.0) * step(-_348.x, 0.0))) * (step(_348.y, 1.0) * step(-_348.y, 0.0)))) * (_384.x * _384.y)) * (((smoothstep(0.0, _13._m0[0]._m4.x, _395 - _389.x) * smoothstep(0.0, _13._m0[0]._m4.z, _389.z - _395)) * smoothstep(0.0, _13._m0[0]._m4.y, _400 - _389.y)) * smoothstep(0.0, _13._m0[0]._m4.w, _389.w - _400));
    float3 _422 = float3(mix(_183, mix(_15._m32, _15._m33, float4(smoothstep(_260, _260 + _15._m27, _259))) * _15._m5.x, float4(_259)).xyz).xyz * _420;
    float4 _423 = float4(_422.x, _422.y, _422.z, _183.w);
    _423.w = mix(_420, 0.0, _15._m8);
    out.m_6 = _423;
    return out;
}

