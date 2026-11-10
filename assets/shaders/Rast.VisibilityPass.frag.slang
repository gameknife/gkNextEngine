import Shader.Types;

struct FragmentInput
{
    float4 position : SV_Position;
    [[vk::location(0)]] nointerpolation uint2 g_visibility_id : VISIBILITY_ID;
    [[vk::location(1)]] nointerpolation uint g_material_model : MATERIAL_MODEL;
    [[vk::location(2)]] nointerpolation float main_opacity : MAIN_OPACITY;
    bool is_front_face : SV_IsFrontFace;
};

struct FragmentOutput
{
    [[vk::location(0)]] uint g_out_instance : SV_Target0;
    [[vk::location(1)]] uint g_out_triangle : SV_Target1;
};

[shader("fragment")]
FragmentOutput main(FragmentInput input)
{
    if (input.g_material_model == MaterialDiffuseLight && !input.is_front_face)
    {
        discard;
    }

    if (input.main_opacity < 1.0f)
    {
        // Fixed 8x8 Bayer coverage. Every part and both faces use the same screen
        // sample, so deeper layers cannot independently fill a discarded hole.
        const uint2 pixel = uint2(input.position.xy);
        uint rank = 0u;
        [unroll]
        for (uint bit = 0u; bit < 3u; ++bit)
        {
            const uint x = (pixel.x >> bit) & 1u;
            const uint y = (pixel.y >> bit) & 1u;
            rank |= (((x ^ y) << 1u) | y) << (4u - bit * 2u);
        }
        if (input.main_opacity <= (float(rank) + 0.5f) / 64.0f)
        {
            discard;
        }
    }

    FragmentOutput output;
    output.g_out_instance = input.g_visibility_id.x;
    output.g_out_triangle = input.g_visibility_id.y;
    return output;
}
