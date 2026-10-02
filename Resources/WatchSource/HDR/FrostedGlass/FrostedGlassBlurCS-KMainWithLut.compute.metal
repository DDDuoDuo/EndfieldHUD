#pragma clang diagnostic ignored "-Wmissing-prototypes"
#pragma clang diagnostic ignored "-Wmissing-braces"

#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

template<typename T, size_t Num>
struct spvUnsafeArray
{
    T elements[Num ? Num : 1];
    
    thread T& operator [] (size_t pos) thread
    {
        return elements[pos];
    }
    constexpr const thread T& operator [] (size_t pos) const thread
    {
        return elements[pos];
    }
    
    device T& operator [] (size_t pos) device
    {
        return elements[pos];
    }
    constexpr const device T& operator [] (size_t pos) const device
    {
        return elements[pos];
    }
    
    constexpr const constant T& operator [] (size_t pos) const constant
    {
        return elements[pos];
    }
    
    threadgroup T& operator [] (size_t pos) threadgroup
    {
        return elements[pos];
    }
    constexpr const threadgroup T& operator [] (size_t pos) const threadgroup
    {
        return elements[pos];
    }
};

struct _6
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
    float4 _m4;
    float4 _m5;
    float4 _m6[6];
    float4 _m7[6];
    float4 _m8;
    float4 _m9;
    float4 _m10;
    float4 _m11;
    float4 _m12;
    float4 _m13;
    float4 _m14;
    float4 _m15;
    float _m16;
    float _m17;
    float _m18;
    uint _m19;
    float4 _m20;
    int4 _m21;
    float4 _m22;
    float4 _m23;
    float4 _m24;
    float4 _m25;
    float4 _m26;
    float4 _m27;
    float4 _m28;
    float4 _m29;
    float4 _m30;
    float4 _m31;
    float4 _m32[4];
    float4 _m33[4];
    float4 _m34[4];
    float4 _m35[4];
    float4 _m36;
    float4 _m37;
    float4 _m38[4];
    float4 _m39[4];
    float4 _m40[4];
    float4 _m41;
    float4 _m42;
    float4 _m43;
    float4 _m44;
    float4 _m45;
    float4 _m46;
    float4 _m47;
    float4 _m48;
    float4 _m49;
    float4 _m50;
    float4 _m51;
    float4 _m52;
    float4 _m53;
    float4 _m54;
    float4 _m55;
    float4 _m56;
    float4 _m57;
    float4 _m58;
    float4 _m59;
    float4 _m60;
    float4 _m61;
    float4 _m62;
    float4 _m63;
    float4 _m64;
    float4 _m65;
    float4 _m66;
    float4 _m67;
    float4 _m68;
    float4 _m69;
    float4 _m70;
    float4 _m71;
    float4 _m72;
    float4 _m73;
    float4 _m74;
    float4 _m75;
    float4 _m76;
    float4 _m77;
    float4 _m78;
    float4 _m79;
    float4 _m80;
    float4 _m81;
    float4 _m82;
    float4 _m83;
    float4 _m84;
    float4 _m85;
    float4 _m86;
    float4 _m87;
    float4 _m88;
    float4 _m89;
    float4 _m90;
    float4 _m91;
    float4 _m92;
    float4 _m93;
    float4 _m94;
    float4 _m95;
    float4 _m96;
    float4 _m97;
    float4 _m98;
    float4 _m99[2];
    float4 _m100[2];
    float _m101;
    float _m102;
    float _m103;
    float _m104;
    float4 _m105;
    float4 _m106;
    float4 _m107;
    float4 _m108;
    float4 _m109;
    float4 _m110;
    float4 _m111;
    float4 _m112;
    float4 _m113;
    float4 _m114;
    float4 _m115;
    float4 _m116;
    float4 _m117;
    float4 _m118;
    float4 _m119;
    float4 _m120;
    float4 _m121;
    float4 _m122;
    float4 _m123;
    float4 _m124;
    float4 _m125;
    float4 _m126;
    float4 _m127;
    float4 _m128;
    float4 _m129;
    float4 _m130;
    float4 _m131;
    float4 _m132;
    float4 _m133;
    float4 _m134;
    float4x4 _m135;
    float4 _m136;
    float4 _m137;
    float4 _m138[32];
};

struct _16
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
};

kernel void main0(constant _6& _7 [[buffer(0)]], constant _16& _17 [[buffer(1)]], texture2d<float> _11 [[texture(0)]], texture2d<float> _12 [[texture(1)]], texture2d<float, access::write> _15 [[texture(2)]], sampler _9 [[sampler(0)]], sampler _13 [[sampler(1)]], uint3 gl_WorkGroupID [[threadgroup_position_in_grid]], uint3 gl_LocalInvocationID [[thread_position_in_threadgroup]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    threadgroup spvUnsafeArray<uint, 128> _18;
    threadgroup spvUnsafeArray<uint, 128> _19;
    threadgroup spvUnsafeArray<uint, 128> _20;
    float2 _118 = float2(int2(((gl_LocalInvocationID.xy << uint2(1u)) + (gl_WorkGroupID.xy << uint2(3u))) - uint2(4u)));
    float3 _143 = fast::clamp(((log2(precise::max((((_11.sample(_9, ((_118 + float2(0.5)) * _17._m0.zw), level(0.0)).xyz * _7._m20.x) * _17._m2.w) * 5.555555820465087890625) + float3(0.04799599945545196533203125), float3(0.0))) * 0.3010300099849700927734375) * 0.24416099488735198974609375) + float3(0.3860360085964202880859375), float3(0.0), float3(1.0));
    float _149 = _143.z * _17._m2.z;
    float _150 = floor(_149);
    float2 _155 = _17._m2.xy * 0.5;
    float2 _156 = ((_143.xy * _17._m2.z) * _17._m2.xy) + _155;
    float3 _157 = float3(_156.x, _156.y, _143.z);
    _157.x = _156.x + (_150 * _17._m2.y);
    float2 _167 = float2(_17._m2.y, 0.0);
    float3 _174 = mix(_12.sample(_13, _157.xy, level(0.0)).xyz, _12.sample(_13, (_157.xy + _167), level(0.0)).xyz, float3(_149 - _150));
    float3 _181 = select((powr(abs(_174), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _174 * 12.9200000762939453125, _174 <= float3(0.003130800090730190277099609375));
    float3 _192 = _181 / float3(precise::max(precise::max(precise::max(_181.x, _181.y), _181.z), _17._m1.x) / _17._m1.x);
    float3 _209 = fast::clamp(((log2(precise::max((((_11.sample(_9, ((_118 + float2(1.5, 0.5)) * _17._m0.zw), level(0.0)).xyz * _7._m20.x) * _17._m2.w) * 5.555555820465087890625) + float3(0.04799599945545196533203125), float3(0.0))) * 0.3010300099849700927734375) * 0.24416099488735198974609375) + float3(0.3860360085964202880859375), float3(0.0), float3(1.0));
    float _213 = _209.z * _17._m2.z;
    float _214 = floor(_213);
    float2 _218 = ((_209.xy * _17._m2.z) * _17._m2.xy) + _155;
    float3 _219 = float3(_218.x, _218.y, _209.z);
    _219.x = _218.x + (_214 * _17._m2.y);
    float3 _234 = mix(_12.sample(_13, _219.xy, level(0.0)).xyz, _12.sample(_13, (_219.xy + _167), level(0.0)).xyz, float3(_213 - _214));
    float3 _241 = select((powr(abs(_234), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _234 * 12.9200000762939453125, _234 <= float3(0.003130800090730190277099609375));
    float3 _250 = _241 / float3(precise::max(precise::max(precise::max(_241.x, _241.y), _241.z), _17._m1.x) / _17._m1.x);
    float3 _267 = fast::clamp(((log2(precise::max((((_11.sample(_9, ((_118 + float2(0.5, 1.5)) * _17._m0.zw), level(0.0)).xyz * _7._m20.x) * _17._m2.w) * 5.555555820465087890625) + float3(0.04799599945545196533203125), float3(0.0))) * 0.3010300099849700927734375) * 0.24416099488735198974609375) + float3(0.3860360085964202880859375), float3(0.0), float3(1.0));
    float _271 = _267.z * _17._m2.z;
    float _272 = floor(_271);
    float2 _276 = ((_267.xy * _17._m2.z) * _17._m2.xy) + _155;
    float3 _277 = float3(_276.x, _276.y, _267.z);
    _277.x = _276.x + (_272 * _17._m2.y);
    float3 _292 = mix(_12.sample(_13, _277.xy, level(0.0)).xyz, _12.sample(_13, (_277.xy + _167), level(0.0)).xyz, float3(_271 - _272));
    float3 _299 = select((powr(abs(_292), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _292 * 12.9200000762939453125, _292 <= float3(0.003130800090730190277099609375));
    float3 _308 = _299 / float3(precise::max(precise::max(precise::max(_299.x, _299.y), _299.z), _17._m1.x) / _17._m1.x);
    float3 _325 = fast::clamp(((log2(precise::max((((_11.sample(_9, ((_118 + float2(1.5)) * _17._m0.zw), level(0.0)).xyz * _7._m20.x) * _17._m2.w) * 5.555555820465087890625) + float3(0.04799599945545196533203125), float3(0.0))) * 0.3010300099849700927734375) * 0.24416099488735198974609375) + float3(0.3860360085964202880859375), float3(0.0), float3(1.0));
    float _329 = _325.z * _17._m2.z;
    float _330 = floor(_329);
    float2 _334 = ((_325.xy * _17._m2.z) * _17._m2.xy) + _155;
    float3 _335 = float3(_334.x, _334.y, _325.z);
    _335.x = _334.x + (_330 * _17._m2.y);
    float3 _350 = mix(_12.sample(_13, _335.xy, level(0.0)).xyz, _12.sample(_13, (_335.xy + _167), level(0.0)).xyz, float3(_329 - _330));
    float3 _357 = select((powr(abs(_350), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _350 * 12.9200000762939453125, _350 <= float3(0.003130800090730190277099609375));
    float3 _366 = _357 / float3(precise::max(precise::max(precise::max(_357.x, _357.y), _357.z), _17._m1.x) / _17._m1.x);
    uint _369 = gl_LocalInvocationID.y << 4u;
    uint _370 = gl_LocalInvocationID.x + _369;
    _18[_370] = as_type<uint>(half2(float2(_192.x, 0.0))) | (as_type<uint>(half2(float2(_250.x, 0.0))) << 16u);
    _19[_370] = as_type<uint>(half2(float2(_192.y, 0.0))) | (as_type<uint>(half2(float2(_250.y, 0.0))) << 16u);
    _20[_370] = as_type<uint>(half2(float2(_192.z, 0.0))) | (as_type<uint>(half2(float2(_250.z, 0.0))) << 16u);
    uint _398 = _370 + 8u;
    _18[_398] = as_type<uint>(half2(float2(_308.x, 0.0))) | (as_type<uint>(half2(float2(_366.x, 0.0))) << 16u);
    _19[_398] = as_type<uint>(half2(float2(_308.y, 0.0))) | (as_type<uint>(half2(float2(_366.y, 0.0))) << 16u);
    _20[_398] = as_type<uint>(half2(float2(_308.z, 0.0))) | (as_type<uint>(half2(float2(_366.z, 0.0))) << 16u);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint _427 = _369 + (gl_LocalInvocationID.x << 1u);
    uint _430 = (_369 + gl_LocalInvocationID.x) + (gl_LocalInvocationID.x & 4u);
    uint _432 = _18[_430];
    uint _434 = _19[_430];
    uint _436 = _20[_430];
    float3 _453 = float3(float2(as_type<half2>(_432 >> 16u)).x, float2(as_type<half2>(_434 >> 16u)).x, float2(as_type<half2>(_436 >> 16u)).x);
    uint _454 = _430 + 1u;
    uint _456 = _18[_454];
    uint _458 = _19[_454];
    uint _460 = _20[_454];
    float3 _467 = float3(float2(as_type<half2>(_456)).x, float2(as_type<half2>(_458)).x, float2(as_type<half2>(_460)).x);
    float3 _477 = float3(float2(as_type<half2>(_456 >> 16u)).x, float2(as_type<half2>(_458 >> 16u)).x, float2(as_type<half2>(_460 >> 16u)).x);
    uint _478 = _430 + 2u;
    uint _480 = _18[_478];
    uint _482 = _19[_478];
    uint _484 = _20[_478];
    float3 _491 = float3(float2(as_type<half2>(_480)).x, float2(as_type<half2>(_482)).x, float2(as_type<half2>(_484)).x);
    float3 _501 = float3(float2(as_type<half2>(_480 >> 16u)).x, float2(as_type<half2>(_482 >> 16u)).x, float2(as_type<half2>(_484 >> 16u)).x);
    uint _502 = _430 + 3u;
    uint _504 = _18[_502];
    uint _506 = _19[_502];
    uint _508 = _20[_502];
    float3 _515 = float3(float2(as_type<half2>(_504)).x, float2(as_type<half2>(_506)).x, float2(as_type<half2>(_508)).x);
    float3 _525 = float3(float2(as_type<half2>(_504 >> 16u)).x, float2(as_type<half2>(_506 >> 16u)).x, float2(as_type<half2>(_508 >> 16u)).x);
    uint _526 = _430 + 4u;
    uint _528 = _18[_526];
    uint _530 = _19[_526];
    uint _532 = _20[_526];
    float3 _539 = float3(float2(as_type<half2>(_528)).x, float2(as_type<half2>(_530)).x, float2(as_type<half2>(_532)).x);
    float3 _562 = ((((_491 * 0.2734375) + ((_477 + _501) * 0.21875)) + ((_467 + _515) * 0.109375)) + ((_453 + _525) * 0.03125)) + ((float3(float2(as_type<half2>(_432)).x, float2(as_type<half2>(_434)).x, float2(as_type<half2>(_436)).x) + _539) * 0.00390625);
    _18[_427] = as_type<uint>(_562.x);
    _19[_427] = as_type<uint>(_562.y);
    _20[_427] = as_type<uint>(_562.z);
    uint _572 = _427 + 1u;
    float3 _585 = ((((_501 * 0.2734375) + ((_491 + _515) * 0.21875)) + ((_477 + _525) * 0.109375)) + ((_467 + _539) * 0.03125)) + ((_453 + float3(float2(as_type<half2>(_528 >> 16u)).x, float2(as_type<half2>(_530 >> 16u)).x, float2(as_type<half2>(_532 >> 16u)).x)) * 0.00390625);
    _18[_572] = as_type<uint>(_585.x);
    _19[_572] = as_type<uint>(_585.y);
    _20[_572] = as_type<uint>(_585.z);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint _597 = (gl_LocalInvocationID.y << 3u) + gl_LocalInvocationID.x;
    uint _606 = _597 + 8u;
    uint _615 = _597 + 16u;
    uint _624 = _597 + 24u;
    uint _633 = _597 + 32u;
    uint _642 = _597 + 40u;
    uint _651 = _597 + 48u;
    uint _660 = _597 + 56u;
    uint _669 = _597 + 64u;
    float3 _690 = ((((as_type<float3>(uint3(_18[_633], _19[_633], _20[_633])) * 0.2734375) + ((as_type<float3>(uint3(_18[_624], _19[_624], _20[_624])) + as_type<float3>(uint3(_18[_642], _19[_642], _20[_642]))) * 0.21875)) + ((as_type<float3>(uint3(_18[_615], _19[_615], _20[_615])) + as_type<float3>(uint3(_18[_651], _19[_651], _20[_651]))) * 0.109375)) + ((as_type<float3>(uint3(_18[_606], _19[_606], _20[_606])) + as_type<float3>(uint3(_18[_660], _19[_660], _20[_660]))) * 0.03125)) + ((as_type<float3>(uint3(_18[_597], _19[_597], _20[_597])) + as_type<float3>(uint3(_18[_669], _19[_669], _20[_669]))) * 0.00390625);
    _15.write((_690 * float(all(gl_GlobalInvocationID.xy < uint2(_17._m0.xy)))).xyzz, uint2(gl_GlobalInvocationID.xy));
}

