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

struct _12
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
};

kernel void main0(constant _12& _13 [[buffer(0)]], texture2d<float> _9 [[texture(0)]], texture2d<float, access::write> _11 [[texture(1)]], sampler _7 [[sampler(0)]], uint3 gl_WorkGroupID [[threadgroup_position_in_grid]], uint3 gl_LocalInvocationID [[thread_position_in_threadgroup]], uint3 gl_GlobalInvocationID [[thread_position_in_grid]])
{
    threadgroup spvUnsafeArray<uint, 128> _14;
    threadgroup spvUnsafeArray<uint, 128> _15;
    threadgroup spvUnsafeArray<uint, 128> _16;
    float2 _84 = float2(int2(((gl_LocalInvocationID.xy << uint2(1u)) + (gl_WorkGroupID.xy << uint2(3u))) - uint2(4u)));
    float4 _93 = _9.sample(_7, ((_84 + float2(0.5)) * _13._m0.zw), level(0.0));
    float3 _105 = _93.xyz / float3(precise::max(precise::max(precise::max(_93.x, _93.y), _93.z), _13._m1.x) / _13._m1.x);
    float4 _111 = _9.sample(_7, ((_84 + float2(1.5, 0.5)) * _13._m0.zw), level(0.0));
    float3 _121 = _111.xyz / float3(precise::max(precise::max(precise::max(_111.x, _111.y), _111.z), _13._m1.x) / _13._m1.x);
    float4 _127 = _9.sample(_7, ((_84 + float2(0.5, 1.5)) * _13._m0.zw), level(0.0));
    float3 _137 = _127.xyz / float3(precise::max(precise::max(precise::max(_127.x, _127.y), _127.z), _13._m1.x) / _13._m1.x);
    float4 _143 = _9.sample(_7, ((_84 + float2(1.5)) * _13._m0.zw), level(0.0));
    float3 _153 = _143.xyz / float3(precise::max(precise::max(precise::max(_143.x, _143.y), _143.z), _13._m1.x) / _13._m1.x);
    uint _156 = gl_LocalInvocationID.y << 4u;
    uint _157 = gl_LocalInvocationID.x + _156;
    _14[_157] = as_type<uint>(half2(float2(_105.x, 0.0))) | (as_type<uint>(half2(float2(_121.x, 0.0))) << 16u);
    _15[_157] = as_type<uint>(half2(float2(_105.y, 0.0))) | (as_type<uint>(half2(float2(_121.y, 0.0))) << 16u);
    _16[_157] = as_type<uint>(half2(float2(_105.z, 0.0))) | (as_type<uint>(half2(float2(_121.z, 0.0))) << 16u);
    uint _185 = _157 + 8u;
    _14[_185] = as_type<uint>(half2(float2(_137.x, 0.0))) | (as_type<uint>(half2(float2(_153.x, 0.0))) << 16u);
    _15[_185] = as_type<uint>(half2(float2(_137.y, 0.0))) | (as_type<uint>(half2(float2(_153.y, 0.0))) << 16u);
    _16[_185] = as_type<uint>(half2(float2(_137.z, 0.0))) | (as_type<uint>(half2(float2(_153.z, 0.0))) << 16u);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint _214 = _156 + (gl_LocalInvocationID.x << 1u);
    uint _217 = (_156 + gl_LocalInvocationID.x) + (gl_LocalInvocationID.x & 4u);
    uint _219 = _14[_217];
    uint _221 = _15[_217];
    uint _223 = _16[_217];
    float3 _240 = float3(float2(as_type<half2>(_219 >> 16u)).x, float2(as_type<half2>(_221 >> 16u)).x, float2(as_type<half2>(_223 >> 16u)).x);
    uint _241 = _217 + 1u;
    uint _243 = _14[_241];
    uint _245 = _15[_241];
    uint _247 = _16[_241];
    float3 _254 = float3(float2(as_type<half2>(_243)).x, float2(as_type<half2>(_245)).x, float2(as_type<half2>(_247)).x);
    float3 _264 = float3(float2(as_type<half2>(_243 >> 16u)).x, float2(as_type<half2>(_245 >> 16u)).x, float2(as_type<half2>(_247 >> 16u)).x);
    uint _265 = _217 + 2u;
    uint _267 = _14[_265];
    uint _269 = _15[_265];
    uint _271 = _16[_265];
    float3 _278 = float3(float2(as_type<half2>(_267)).x, float2(as_type<half2>(_269)).x, float2(as_type<half2>(_271)).x);
    float3 _288 = float3(float2(as_type<half2>(_267 >> 16u)).x, float2(as_type<half2>(_269 >> 16u)).x, float2(as_type<half2>(_271 >> 16u)).x);
    uint _289 = _217 + 3u;
    uint _291 = _14[_289];
    uint _293 = _15[_289];
    uint _295 = _16[_289];
    float3 _302 = float3(float2(as_type<half2>(_291)).x, float2(as_type<half2>(_293)).x, float2(as_type<half2>(_295)).x);
    float3 _312 = float3(float2(as_type<half2>(_291 >> 16u)).x, float2(as_type<half2>(_293 >> 16u)).x, float2(as_type<half2>(_295 >> 16u)).x);
    uint _313 = _217 + 4u;
    uint _315 = _14[_313];
    uint _317 = _15[_313];
    uint _319 = _16[_313];
    float3 _326 = float3(float2(as_type<half2>(_315)).x, float2(as_type<half2>(_317)).x, float2(as_type<half2>(_319)).x);
    float3 _349 = ((((_278 * 0.2734375) + ((_264 + _288) * 0.21875)) + ((_254 + _302) * 0.109375)) + ((_240 + _312) * 0.03125)) + ((float3(float2(as_type<half2>(_219)).x, float2(as_type<half2>(_221)).x, float2(as_type<half2>(_223)).x) + _326) * 0.00390625);
    _14[_214] = as_type<uint>(_349.x);
    _15[_214] = as_type<uint>(_349.y);
    _16[_214] = as_type<uint>(_349.z);
    uint _359 = _214 + 1u;
    float3 _372 = ((((_288 * 0.2734375) + ((_278 + _302) * 0.21875)) + ((_264 + _312) * 0.109375)) + ((_254 + _326) * 0.03125)) + ((_240 + float3(float2(as_type<half2>(_315 >> 16u)).x, float2(as_type<half2>(_317 >> 16u)).x, float2(as_type<half2>(_319 >> 16u)).x)) * 0.00390625);
    _14[_359] = as_type<uint>(_372.x);
    _15[_359] = as_type<uint>(_372.y);
    _16[_359] = as_type<uint>(_372.z);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint _384 = (gl_LocalInvocationID.y << 3u) + gl_LocalInvocationID.x;
    uint _393 = _384 + 8u;
    uint _402 = _384 + 16u;
    uint _411 = _384 + 24u;
    uint _420 = _384 + 32u;
    uint _429 = _384 + 40u;
    uint _438 = _384 + 48u;
    uint _447 = _384 + 56u;
    uint _456 = _384 + 64u;
    float3 _477 = ((((as_type<float3>(uint3(_14[_420], _15[_420], _16[_420])) * 0.2734375) + ((as_type<float3>(uint3(_14[_411], _15[_411], _16[_411])) + as_type<float3>(uint3(_14[_429], _15[_429], _16[_429]))) * 0.21875)) + ((as_type<float3>(uint3(_14[_402], _15[_402], _16[_402])) + as_type<float3>(uint3(_14[_438], _15[_438], _16[_438]))) * 0.109375)) + ((as_type<float3>(uint3(_14[_393], _15[_393], _16[_393])) + as_type<float3>(uint3(_14[_447], _15[_447], _16[_447]))) * 0.03125)) + ((as_type<float3>(uint3(_14[_384], _15[_384], _16[_384])) + as_type<float3>(uint3(_14[_456], _15[_456], _16[_456]))) * 0.00390625);
    _11.write((_477 * float(all(gl_GlobalInvocationID.xy < uint2(_13._m0.xy)))).xyzz, uint2(gl_GlobalInvocationID.xy));
}

