#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _12
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
    float4 _m6;
    float4 _m7;
    float4 _m8;
};

struct _16
{
    float4 _m0;
    float4 _m1;
    float _m2;
    float _m3;
    float _m4;
    float _m5;
    float _m6;
    float _m7;
    float _m8;
    float _m9;
    float _m10;
    float _m11;
    float _m12;
    float _m13;
    float _m14;
    float _m15;
    float _m16;
    float _m17;
    float _m18;
    float _m19;
    float _m20;
    float _m21;
    float _m22;
    float4 _m23;
};

struct _18
{
    float _m0;
    float _m1;
    float4 _m2;
    float _m3;
    float _m4;
    float _m5;
    float _m6;
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
    float3 _m18;
    float4x4 _m19;
    float4 _m20;
    float _m21;
    float _m22;
    float _m23;
    float _m24;
    float _m25;
    float4 _m26;
    float _m27;
    float _m28;
    float _m29;
    float _m30;
    float4 _m31;
    float _m32;
    float _m33;
    float _m34;
    float _m35;
    float _m36;
    float _m37;
    float _m38;
    float _m39;
    float _m40;
    float _m41;
    float _m42;
    float _m43;
    float _m44;
    float4 _m45;
    float _m46;
    float _m47;
    float _m48;
    float _m49;
    float _m50;
    float _m51;
    float _m52;
    float _m53;
    float _m54;
    float4 _m55;
    float4x4 _m56;
    float4 _m57;
    float4 _m58;
};

struct _20
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

struct main0_out
{
    float4 m_11 [[color(0)]];
};

struct main0_in
{
    float4 m_3 [[user(locn0)]];
    float2 m_4 [[user(locn1)]];
    float4 m_5 [[user(locn2)]];
    float4 m_6 [[user(locn4)]];
    float4 m_7 [[user(locn5)]];
    float4 m_8 [[user(locn6)]];
    float4 m_9 [[user(locn7)]];
    float4 m_10 [[user(locn8)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _12& _13 [[buffer(0)]], constant _16& _17 [[buffer(1)]], constant _18& _19 [[buffer(2)]], constant _20& _21 [[buffer(3)]], texture2d<float> _23 [[texture(0)]], texture2d<float> _25 [[texture(1)]], texture2d<float> _27 [[texture(2)]], sampler _24 [[sampler(0)]], sampler _26 [[sampler(1)]], sampler _28 [[sampler(2)]])
{
    main0_out out = {};
    bool _95 = _17._m2 > 0.5;
    float2 _173;
    if (_95)
    {
        float _102 = _13._m0.y * _17._m16;
        float2 _108 = in.m_10.xy / float2(precise::max(in.m_10.w, 9.9999997473787516355514526367188e-05));
        float2 _121 = float2(_108.x * (_21._m14.x / precise::max(_21._m14.y, 1.0)), _108.y) * _17._m15;
        float2 _124 = _121 + float2(_102, _102 * 0.829999983310699462890625);
        float2 _129 = (_121 * 1.7000000476837158203125) + float2(_102 * (-0.9700000286102294921875), _102 * 1.309999942779541015625);
        float2 _143 = float2(sin(_124.y) + (sin(_129.y) * 0.5), cos(_124.x) + (cos(_129.x) * 0.5)) * 0.66670000553131103515625;
        float _160 = precise::max(_19._m50 - 1.0, 0.0);
        _173 = fast::clamp((float2(int2(sign(_143))) * powr(abs(_143), float2(precise::max(_17._m17, 1.0)))) * _17._m14, float2(-_160), float2(_160)) * float2(1.0 / _19._m48, 1.0 / _19._m49);
    }
    else
    {
        _173 = float2(0.0);
    }
    float2 _174 = in.m_4 + _173;
    float4 _178 = _27.sample(_28, _174);
    float _179 = _178.x;
    bool _182 = _17._m18 > 0.5;
    float _232;
    if (_182)
    {
        float2 _194 = float2(1.0 / _19._m48, 1.0 / _19._m49) * _17._m20;
        float _197 = _194.x;
        float _215 = _194.y;
        _232 = ((((_179 + _27.sample(_28, (_174 + float2(_197, 0.0))).x) + _27.sample(_28, (_174 + float2(-_197, 0.0))).x) + _27.sample(_28, (_174 + float2(0.0, _215))).x) + _27.sample(_28, (_174 + float2(0.0, -_215))).x) * 0.20000000298023223876953125;
    }
    else
    {
        _232 = _179;
    }
    float _238 = (in.m_5.z - _232) * in.m_5.y;
    float _244 = (_19._m8 * _19._m39) * in.m_5.y;
    float _248 = (_19._m4 * _19._m39) * in.m_5.y;
    float3 _255 = _19._m2.xyz * in.m_3.xyz;
    float4 _270 = _25.sample(_26, (in.m_9.xy + (float2(_19._m0, _19._m1) * _21._m12.y)));
    float4 _271 = float4(_255.x, _255.y, _255.z, _19._m2.w) * _270;
    float4 _283 = _23.sample(_24, (in.m_9.zw + (float2(_19._m5, _19._m6) * _21._m12.y)));
    float4 _284 = _19._m7 * _283;
    float4 _333;
    if (_182)
    {
        float _295 = (_17._m21 * _19._m39) * in.m_5.y;
        float3 _306 = _271.xyz * _271.w;
        _333 = float4(_306.x, _306.y, _306.z, _271.w) * (1.0 - fast::clamp(((_238 - (((_17._m19 * _19._m39) * in.m_5.y) * 0.5)) + (_295 * 0.5)) / (1.0 + _295), 0.0, 1.0));
    }
    else
    {
        float _309 = _244 * 0.5;
        float3 _324 = _271.xyz * _271.w;
        float3 _328 = _284.xyz * _284.w;
        _333 = mix(float4(_324.x, _324.y, _324.z, _271.w), float4(_328.x, _328.y, _328.z, _284.w), float4(fast::clamp(_238 + _309, 0.0, 1.0) * sqrt(precise::min(1.0, _244)))) * (1.0 - fast::clamp(((_238 - _309) + (_248 * 0.5)) / (1.0 + _248), 0.0, 1.0));
    }
    float4 _338 = _27.sample(_28, in.m_7.xy);
    float4 _351 = _333 + ((in.m_8 * fast::clamp((_338.x * in.m_7.z) - in.m_7.w, 0.0, 1.0)) * (1.0 - _333.w));
    float4 _365;
    if (_95)
    {
        _365 = _351 * fast::clamp(mix(1.0, (_17._m13 >= 0.0) ? (1.0 - in.m_6.w) : in.m_6.w, abs(_17._m13)), 0.0, 1.0);
    }
    else
    {
        _365 = _351;
    }
    float4 _368 = _365 * in.m_3.w;
    _368.w = mix(_368.w, 0.0, _17._m22);
    out.m_11 = _368;
    return out;
}

