#include "BasicShaderHeader.hlsli"

Output BasicVS(
    float4 pos : POSITION,
    float4 normal : NORMAL,
    float2 uv : TEXCOORD,
    min16uint2 boneno : BONE_NO,
    min16uint2 weight : WEIGHT
)
{
    Output output;
    output.svpos = mul(mat, pos);
    output.uv = uv;
    return output;
}

