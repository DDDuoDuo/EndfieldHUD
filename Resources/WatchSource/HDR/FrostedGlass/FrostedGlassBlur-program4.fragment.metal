#include <metal_stdlib>
#include <simd/simd.h>

using namespace metal;

struct _5
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

struct _11
{
    float4 _m0;
    float4 _m1;
    float4 _m2;
    float4 _m3;
};

struct main0_out
{
    float4 m_4 [[color(0)]];
};

struct main0_in
{
    float2 m_3 [[user(locn0)]];
};

fragment main0_out main0(main0_in in [[stage_in]], constant _5& _6 [[buffer(0)]], constant _11& _12 [[buffer(1)]], texture2d<float> _10 [[texture(0)]], sampler _8 [[sampler(0)]])
{
    main0_out out = {};
    float2 _58 = float2(_12._m0.z * 4.0, 0.0);
    float4 _65 = _10.sample(_8, (in.m_3 - _58), bias(_6._m16));
    float2 _79 = float2(_12._m0.z * 3.0, 0.0);
    float4 _84 = _10.sample(_8, (in.m_3 - _79), bias(_6._m16));
    float2 _96 = float2(_12._m0.z * 2.0, 0.0);
    float4 _101 = _10.sample(_8, (in.m_3 - _96), bias(_6._m16));
    float2 _112 = float2(_12._m0.z, 0.0);
    float4 _117 = _10.sample(_8, (in.m_3 - _112), bias(_6._m16));
    float4 _131 = _10.sample(_8, in.m_3, bias(_6._m16));
    float4 _146 = _10.sample(_8, (in.m_3 + _112), bias(_6._m16));
    float4 _161 = _10.sample(_8, (in.m_3 + _96), bias(_6._m16));
    float4 _176 = _10.sample(_8, (in.m_3 + _79), bias(_6._m16));
    float4 _191 = _10.sample(_8, (in.m_3 + _58), bias(_6._m16));
    float3 _212 = ((((((_65.xyz / float3(precise::max(precise::max(precise::max(_65.x, _65.y), _65.z), _12._m1.x) / _12._m1.x)) * 0.01621622033417224884033203125) + ((_84.xyz / float3(precise::max(precise::max(precise::max(_84.x, _84.y), _84.z), _12._m1.x) / _12._m1.x)) * 0.0540540516376495361328125)) + ((_101.xyz / float3(precise::max(precise::max(precise::max(_101.x, _101.y), _101.z), _12._m1.x) / _12._m1.x)) * 0.12162162363529205322265625)) + ((_117.xyz / float3(precise::max(precise::max(precise::max(_117.x, _117.y), _117.z), _12._m1.x) / _12._m1.x)) * 0.1945945918560028076171875)) + ((_131.xyz / float3(precise::max(precise::max(precise::max(_131.x, _131.y), _131.z), _12._m1.x) / _12._m1.x)) * 0.2270270287990570068359375)) + ((_146.xyz / float3(precise::max(precise::max(precise::max(_146.x, _146.y), _146.z), _12._m1.x) / _12._m1.x)) * 0.1945945918560028076171875);
    float3 _218 = ((_212 + ((_161.xyz / float3(precise::max(precise::max(precise::max(_161.x, _161.y), _161.z), _12._m1.x) / _12._m1.x)) * 0.12162162363529205322265625)) + ((_176.xyz / float3(precise::max(precise::max(precise::max(_176.x, _176.y), _176.z), _12._m1.x) / _12._m1.x)) * 0.0540540516376495361328125)) + ((_191.xyz / float3(precise::max(precise::max(precise::max(_191.x, _191.y), _191.z), _12._m1.x) / _12._m1.x)) * 0.01621622033417224884033203125);
    out.m_4 = float4(_218, 1.0);
    return out;
}

