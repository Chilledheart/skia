#include <metal_stdlib>
#include <simd/simd.h>
using namespace metal;
struct Inputs {
    float4 src;
    float4 dst;
};
struct Outputs {
    float4 sk_FragColor [[color(0)]];
};


fragment Outputs fragmentMain(Inputs _in [[stage_in]], bool _frontFacing [[front_facing]], float4 _fragCoord [[position]]) {
    Outputs _out;
    (void)_out;
    float _0_alpha = _in.dst.w * _in.src.w;
    float3 _1_sda = _in.src.xyz * _in.dst.w;
    float3 _2_dsa = _in.dst.xyz * _in.src.w;
    float3 _3_blend_set_color_luminance;
    float _4_lum = dot(float3(0.30000001192092896, 0.5899999737739563, 0.10999999940395355), _2_dsa);

    float3 _5_result = (_4_lum - dot(float3(0.30000001192092896, 0.5899999737739563, 0.10999999940395355), _1_sda)) + _1_sda;

    float _6_minComp = min(min(_5_result.x, _5_result.y), _5_result.z);
    float _7_maxComp = max(max(_5_result.x, _5_result.y), _5_result.z);
    if (_6_minComp < 0.0 && _4_lum != _6_minComp) {
        float _8_d = _4_lum - _6_minComp;
        _5_result = _4_lum + (_5_result - _4_lum) * (_4_lum / _8_d);

    }
    if (_7_maxComp > _0_alpha && _7_maxComp != _4_lum) {
        float3 _9_n = (_5_result - _4_lum) * (_0_alpha - _4_lum);
        float _10_d = _7_maxComp - _4_lum;
        _3_blend_set_color_luminance = _4_lum + _9_n / _10_d;

    } else {
        _3_blend_set_color_luminance = _5_result;
    }
    _out.sk_FragColor = float4((((_3_blend_set_color_luminance + _in.dst.xyz) - _2_dsa) + _in.src.xyz) - _1_sda, (_in.src.w + _in.dst.w) - _0_alpha);


    return _out;
}
