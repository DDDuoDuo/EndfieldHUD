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

struct _18
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
};

kernel void main0(constant _6& _7 [[buffer(0)]], constant _18& _19 [[buffer(1)]], texture2d<float> _11 [[texture(0)]], texture2d<float> _12 [[texture(1)]], texture2d<float> _13 [[texture(2)]], texture2d<float, access::write> _17 [[texture(3)]], sampler _9 [[sampler(0)]], sampler _14 [[sampler(1)]], sampler _15 [[sampler(2)]], uint3 gl_WorkGroupID [[threadgroup_position_in_grid]], uint3 gl_LocalInvocationID [[thread_position_in_threadgroup]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    threadgroup spvUnsafeArray<uint, 128> _20;
    threadgroup spvUnsafeArray<uint, 128> _21;
    threadgroup spvUnsafeArray<uint, 128> _22;
    float2 _128 = float2(int2(((gl_LocalInvocationID.xy << uint2(1u)) + (gl_WorkGroupID.xy << uint2(3u))) - uint2(4u)));
    float3 _153 = fast::clamp(((log2(precise::max((((_11.sample(_9, ((_128 + float2(0.5)) * _19._m0.zw), level(0.0)).xyz * _7._m20.x) * _19._m2.w) * 5.555555820465087890625) + float3(0.04799599945545196533203125), float3(0.0))) * 0.3010300099849700927734375) * 0.24416099488735198974609375) + float3(0.3860360085964202880859375), float3(0.0), float3(1.0));
    float _159 = _153.z * _19._m2.z;
    float _160 = floor(_159);
    float2 _165 = _19._m2.xy * 0.5;
    float2 _166 = ((_153.xy * _19._m2.z) * _19._m2.xy) + _165;
    float3 _167 = float3(_166.x, _166.y, _153.z);
    _167.x = _166.x + (_160 * _19._m2.y);
    float2 _177 = float2(_19._m2.y, 0.0);
    float3 _185 = fast::clamp(mix(_12.sample(_14, _167.xy, level(0.0)).xyz, _12.sample(_14, (_167.xy + _177), level(0.0)).xyz, float3(_159 - _160)), float3(0.0), float3(1.0));
    float3 _192 = select((powr(abs(_185), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _185 * 12.9200000762939453125, _185 <= float3(0.003130800090730190277099609375));
    float _199 = _192.z * _19._m3.z;
    float _200 = floor(_199);
    float2 _205 = _19._m3.xy * 0.5;
    float2 _206 = ((_192.xy * _19._m3.z) * _19._m3.xy) + _205;
    float3 _207 = float3(_206.x, _206.y, _192.z);
    _207.x = _206.x + (_200 * _19._m3.y);
    float2 _217 = float2(_19._m3.y, 0.0);
    float3 _227 = float3(_19._m3.w);
    float3 _228 = mix(_192, mix(_13.sample(_15, _207.xy, level(0.0)).xyz, _13.sample(_15, (_207.xy + _217), level(0.0)).xyz, float3(_199 - _200)), _227);
    float3 _235 = select(powr(abs((_228 + float3(0.054999999701976776123046875)) * float3(0.947867333889007568359375)), float3(2.400000095367431640625)), _228 * float3(0.077399380505084991455078125), _228 <= float3(0.040449999272823333740234375));
    float3 _242 = select((powr(abs(_235), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _235 * 12.9200000762939453125, _235 <= float3(0.003130800090730190277099609375));
    float3 _253 = _242 / float3(precise::max(precise::max(precise::max(_242.x, _242.y), _242.z), _19._m1.x) / _19._m1.x);
    float3 _270 = fast::clamp(((log2(precise::max((((_11.sample(_9, ((_128 + float2(1.5, 0.5)) * _19._m0.zw), level(0.0)).xyz * _7._m20.x) * _19._m2.w) * 5.555555820465087890625) + float3(0.04799599945545196533203125), float3(0.0))) * 0.3010300099849700927734375) * 0.24416099488735198974609375) + float3(0.3860360085964202880859375), float3(0.0), float3(1.0));
    float _274 = _270.z * _19._m2.z;
    float _275 = floor(_274);
    float2 _279 = ((_270.xy * _19._m2.z) * _19._m2.xy) + _165;
    float3 _280 = float3(_279.x, _279.y, _270.z);
    _280.x = _279.x + (_275 * _19._m2.y);
    float3 _296 = fast::clamp(mix(_12.sample(_14, _280.xy, level(0.0)).xyz, _12.sample(_14, (_280.xy + _177), level(0.0)).xyz, float3(_274 - _275)), float3(0.0), float3(1.0));
    float3 _303 = select((powr(abs(_296), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _296 * 12.9200000762939453125, _296 <= float3(0.003130800090730190277099609375));
    float _307 = _303.z * _19._m3.z;
    float _308 = floor(_307);
    float2 _312 = ((_303.xy * _19._m3.z) * _19._m3.xy) + _205;
    float3 _313 = float3(_312.x, _312.y, _303.z);
    _313.x = _312.x + (_308 * _19._m3.y);
    float3 _329 = mix(_303, mix(_13.sample(_15, _313.xy, level(0.0)).xyz, _13.sample(_15, (_313.xy + _217), level(0.0)).xyz, float3(_307 - _308)), _227);
    float3 _336 = select(powr(abs((_329 + float3(0.054999999701976776123046875)) * float3(0.947867333889007568359375)), float3(2.400000095367431640625)), _329 * float3(0.077399380505084991455078125), _329 <= float3(0.040449999272823333740234375));
    float3 _343 = select((powr(abs(_336), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _336 * 12.9200000762939453125, _336 <= float3(0.003130800090730190277099609375));
    float3 _352 = _343 / float3(precise::max(precise::max(precise::max(_343.x, _343.y), _343.z), _19._m1.x) / _19._m1.x);
    float3 _369 = fast::clamp(((log2(precise::max((((_11.sample(_9, ((_128 + float2(0.5, 1.5)) * _19._m0.zw), level(0.0)).xyz * _7._m20.x) * _19._m2.w) * 5.555555820465087890625) + float3(0.04799599945545196533203125), float3(0.0))) * 0.3010300099849700927734375) * 0.24416099488735198974609375) + float3(0.3860360085964202880859375), float3(0.0), float3(1.0));
    float _373 = _369.z * _19._m2.z;
    float _374 = floor(_373);
    float2 _378 = ((_369.xy * _19._m2.z) * _19._m2.xy) + _165;
    float3 _379 = float3(_378.x, _378.y, _369.z);
    _379.x = _378.x + (_374 * _19._m2.y);
    float3 _395 = fast::clamp(mix(_12.sample(_14, _379.xy, level(0.0)).xyz, _12.sample(_14, (_379.xy + _177), level(0.0)).xyz, float3(_373 - _374)), float3(0.0), float3(1.0));
    float3 _402 = select((powr(abs(_395), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _395 * 12.9200000762939453125, _395 <= float3(0.003130800090730190277099609375));
    float _406 = _402.z * _19._m3.z;
    float _407 = floor(_406);
    float2 _411 = ((_402.xy * _19._m3.z) * _19._m3.xy) + _205;
    float3 _412 = float3(_411.x, _411.y, _402.z);
    _412.x = _411.x + (_407 * _19._m3.y);
    float3 _428 = mix(_402, mix(_13.sample(_15, _412.xy, level(0.0)).xyz, _13.sample(_15, (_412.xy + _217), level(0.0)).xyz, float3(_406 - _407)), _227);
    float3 _435 = select(powr(abs((_428 + float3(0.054999999701976776123046875)) * float3(0.947867333889007568359375)), float3(2.400000095367431640625)), _428 * float3(0.077399380505084991455078125), _428 <= float3(0.040449999272823333740234375));
    float3 _442 = select((powr(abs(_435), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _435 * 12.9200000762939453125, _435 <= float3(0.003130800090730190277099609375));
    float3 _451 = _442 / float3(precise::max(precise::max(precise::max(_442.x, _442.y), _442.z), _19._m1.x) / _19._m1.x);
    float3 _468 = fast::clamp(((log2(precise::max((((_11.sample(_9, ((_128 + float2(1.5)) * _19._m0.zw), level(0.0)).xyz * _7._m20.x) * _19._m2.w) * 5.555555820465087890625) + float3(0.04799599945545196533203125), float3(0.0))) * 0.3010300099849700927734375) * 0.24416099488735198974609375) + float3(0.3860360085964202880859375), float3(0.0), float3(1.0));
    float _472 = _468.z * _19._m2.z;
    float _473 = floor(_472);
    float2 _477 = ((_468.xy * _19._m2.z) * _19._m2.xy) + _165;
    float3 _478 = float3(_477.x, _477.y, _468.z);
    _478.x = _477.x + (_473 * _19._m2.y);
    float3 _494 = fast::clamp(mix(_12.sample(_14, _478.xy, level(0.0)).xyz, _12.sample(_14, (_478.xy + _177), level(0.0)).xyz, float3(_472 - _473)), float3(0.0), float3(1.0));
    float3 _501 = select((powr(abs(_494), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _494 * 12.9200000762939453125, _494 <= float3(0.003130800090730190277099609375));
    float _505 = _501.z * _19._m3.z;
    float _506 = floor(_505);
    float2 _510 = ((_501.xy * _19._m3.z) * _19._m3.xy) + _205;
    float3 _511 = float3(_510.x, _510.y, _501.z);
    _511.x = _510.x + (_506 * _19._m3.y);
    float3 _527 = mix(_501, mix(_13.sample(_15, _511.xy, level(0.0)).xyz, _13.sample(_15, (_511.xy + _217), level(0.0)).xyz, float3(_505 - _506)), _227);
    float3 _534 = select(powr(abs((_527 + float3(0.054999999701976776123046875)) * float3(0.947867333889007568359375)), float3(2.400000095367431640625)), _527 * float3(0.077399380505084991455078125), _527 <= float3(0.040449999272823333740234375));
    float3 _541 = select((powr(abs(_534), float3(0.4166666567325592041015625)) * 1.05499994754791259765625) - float3(0.054999999701976776123046875), _534 * 12.9200000762939453125, _534 <= float3(0.003130800090730190277099609375));
    float3 _550 = _541 / float3(precise::max(precise::max(precise::max(_541.x, _541.y), _541.z), _19._m1.x) / _19._m1.x);
    uint _553 = gl_LocalInvocationID.y << 4u;
    uint _554 = gl_LocalInvocationID.x + _553;
    _20[_554] = as_type<uint>(half2(float2(_253.x, 0.0))) | (as_type<uint>(half2(float2(_352.x, 0.0))) << 16u);
    _21[_554] = as_type<uint>(half2(float2(_253.y, 0.0))) | (as_type<uint>(half2(float2(_352.y, 0.0))) << 16u);
    _22[_554] = as_type<uint>(half2(float2(_253.z, 0.0))) | (as_type<uint>(half2(float2(_352.z, 0.0))) << 16u);
    uint _582 = _554 + 8u;
    _20[_582] = as_type<uint>(half2(float2(_451.x, 0.0))) | (as_type<uint>(half2(float2(_550.x, 0.0))) << 16u);
    _21[_582] = as_type<uint>(half2(float2(_451.y, 0.0))) | (as_type<uint>(half2(float2(_550.y, 0.0))) << 16u);
    _22[_582] = as_type<uint>(half2(float2(_451.z, 0.0))) | (as_type<uint>(half2(float2(_550.z, 0.0))) << 16u);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint _611 = _553 + (gl_LocalInvocationID.x << 1u);
    uint _614 = (_553 + gl_LocalInvocationID.x) + (gl_LocalInvocationID.x & 4u);
    uint _616 = _20[_614];
    uint _618 = _21[_614];
    uint _620 = _22[_614];
    float3 _637 = float3(float2(as_type<half2>(_616 >> 16u)).x, float2(as_type<half2>(_618 >> 16u)).x, float2(as_type<half2>(_620 >> 16u)).x);
    uint _638 = _614 + 1u;
    uint _640 = _20[_638];
    uint _642 = _21[_638];
    uint _644 = _22[_638];
    float3 _651 = float3(float2(as_type<half2>(_640)).x, float2(as_type<half2>(_642)).x, float2(as_type<half2>(_644)).x);
    float3 _661 = float3(float2(as_type<half2>(_640 >> 16u)).x, float2(as_type<half2>(_642 >> 16u)).x, float2(as_type<half2>(_644 >> 16u)).x);
    uint _662 = _614 + 2u;
    uint _664 = _20[_662];
    uint _666 = _21[_662];
    uint _668 = _22[_662];
    float3 _675 = float3(float2(as_type<half2>(_664)).x, float2(as_type<half2>(_666)).x, float2(as_type<half2>(_668)).x);
    float3 _685 = float3(float2(as_type<half2>(_664 >> 16u)).x, float2(as_type<half2>(_666 >> 16u)).x, float2(as_type<half2>(_668 >> 16u)).x);
    uint _686 = _614 + 3u;
    uint _688 = _20[_686];
    uint _690 = _21[_686];
    uint _692 = _22[_686];
    float3 _699 = float3(float2(as_type<half2>(_688)).x, float2(as_type<half2>(_690)).x, float2(as_type<half2>(_692)).x);
    float3 _709 = float3(float2(as_type<half2>(_688 >> 16u)).x, float2(as_type<half2>(_690 >> 16u)).x, float2(as_type<half2>(_692 >> 16u)).x);
    uint _710 = _614 + 4u;
    uint _712 = _20[_710];
    uint _714 = _21[_710];
    uint _716 = _22[_710];
    float3 _723 = float3(float2(as_type<half2>(_712)).x, float2(as_type<half2>(_714)).x, float2(as_type<half2>(_716)).x);
    float3 _746 = ((((_675 * 0.2734375) + ((_661 + _685) * 0.21875)) + ((_651 + _699) * 0.109375)) + ((_637 + _709) * 0.03125)) + ((float3(float2(as_type<half2>(_616)).x, float2(as_type<half2>(_618)).x, float2(as_type<half2>(_620)).x) + _723) * 0.00390625);
    _20[_611] = as_type<uint>(_746.x);
    _21[_611] = as_type<uint>(_746.y);
    _22[_611] = as_type<uint>(_746.z);
    uint _756 = _611 + 1u;
    float3 _769 = ((((_685 * 0.2734375) + ((_675 + _699) * 0.21875)) + ((_661 + _709) * 0.109375)) + ((_651 + _723) * 0.03125)) + ((_637 + float3(float2(as_type<half2>(_712 >> 16u)).x, float2(as_type<half2>(_714 >> 16u)).x, float2(as_type<half2>(_716 >> 16u)).x)) * 0.00390625);
    _20[_756] = as_type<uint>(_769.x);
    _21[_756] = as_type<uint>(_769.y);
    _22[_756] = as_type<uint>(_769.z);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint _781 = (gl_LocalInvocationID.y << 3u) + gl_LocalInvocationID.x;
    uint _790 = _781 + 8u;
    uint _799 = _781 + 16u;
    uint _808 = _781 + 24u;
    uint _817 = _781 + 32u;
    uint _826 = _781 + 40u;
    uint _835 = _781 + 48u;
    uint _844 = _781 + 56u;
    uint _853 = _781 + 64u;
    float3 _874 = ((((as_type<float3>(uint3(_20[_817], _21[_817], _22[_817])) * 0.2734375) + ((as_type<float3>(uint3(_20[_808], _21[_808], _22[_808])) + as_type<float3>(uint3(_20[_826], _21[_826], _22[_826]))) * 0.21875)) + ((as_type<float3>(uint3(_20[_799], _21[_799], _22[_799])) + as_type<float3>(uint3(_20[_835], _21[_835], _22[_835]))) * 0.109375)) + ((as_type<float3>(uint3(_20[_790], _21[_790], _22[_790])) + as_type<float3>(uint3(_20[_844], _21[_844], _22[_844]))) * 0.03125)) + ((as_type<float3>(uint3(_20[_781], _21[_781], _22[_781])) + as_type<float3>(uint3(_20[_853], _21[_853], _22[_853]))) * 0.00390625);
    _17.write((_874 * float(all(gl_GlobalInvocationID.xy < uint2(_19._m0.xy)))).xyzz, uint2(gl_GlobalInvocationID.xy));
}

