implementing PathTracing;

#ifndef GK_NEXT_BSDF_SLANG
#define GK_NEXT_BSDF_SLANG

#include "PreProcessor.slang"

// Shared material evaluation for tracing direct lighting. PerceptualRoughness follows
// glTF; GGX alpha is roughness^2 everywhere in this file.
public static const float BSDF_MIN_ROUGHNESS = 0.001f;
public static const float BSDF_DELTA_ROUGHNESS = 0.02f;

public static const uint BSDF_LOBE_NONE = 0u;
public static const uint BSDF_LOBE_DIFFUSE = 1u << 0;
public static const uint BSDF_LOBE_REFLECTION = 1u << 1;
public static const uint BSDF_LOBE_TRANSMISSION = 1u << 2;
public static const uint BSDF_LOBE_DELTA = 1u << 3;

public struct FBSDFContext
{
    public float3 Position;
    public float3 Normal;
    public float3 Wo;
    public float3 BaseColor;
    public float PerceptualRoughness;
    public float Metalness;
    public float EtaOutside;
    public float EtaInside;
    public uint MaterialModel;
    public bool FrontFace;
};

public struct FBSDFEval
{
    // Diffuse excludes BaseColor because RT_SINGLE_DIFFUSE is albedo-demodulated.
    public float3 DiffuseDemodulated;
    // Specular contains the complete BRDF/BTDF value.
    public float3 Specular;
    public float DiffusePdf;
    public float SpecularPdf;
    public uint Flags;

    public __init()
    {
        DiffuseDemodulated = float3(0, 0, 0);
        Specular = float3(0, 0, 0);
        DiffusePdf = 0.0f;
        SpecularPdf = 0.0f;
        Flags = BSDF_LOBE_NONE;
    }
};

public struct FDirectLighting
{
    public float3 DiffuseDemodulated;
    public float3 SpecularRadiance;

    public __init()
    {
        DiffuseDemodulated = float3(0, 0, 0);
        SpecularRadiance = float3(0, 0, 0);
    }
};

public float3 BSDFDielectricF0(float eta)
{
    const float e = max(eta, 1.0001f);
    const float f = (e - 1.0f) / (e + 1.0f);
    return float3(f * f);
}

public float3 BSDFFresnelSchlick(float cosTheta, float3 f0)
{
    const float m = 1.0f - saturate(cosTheta);
    const float m2 = m * m;
    return f0 + (1.0f - f0) * (m2 * m2 * m);
}

public float BSDFDistributionGGX(float NoH, float perceptualRoughness)
{
    const float alpha = max(perceptualRoughness * perceptualRoughness,
                            BSDF_MIN_ROUGHNESS * BSDF_MIN_ROUGHNESS);
    const float alpha2 = alpha * alpha;
    const float d = NoH * NoH * (alpha2 - 1.0f) + 1.0f;
    return alpha2 / max(M_PI * d * d, 1e-7f);
}

public float BSDFGeometrySmithG1(float NoX, float perceptualRoughness)
{
    if (NoX <= 0.0f)
    {
        return 0.0f;
    }
    const float alpha = max(perceptualRoughness * perceptualRoughness,
                            BSDF_MIN_ROUGHNESS * BSDF_MIN_ROUGHNESS);
    const float alpha2 = alpha * alpha;
    return (2.0f * NoX) /
           max(NoX + sqrt(alpha2 + (1.0f - alpha2) * NoX * NoX), 1e-7f);
}

// Height-correlated Smith G2 shadowing-masking function (Heitz 2014)
public float BSDFGeometrySmithCorrelated(float NoV, float NoL, float perceptualRoughness)
{
    if (NoV <= 0.0f || NoL <= 0.0f)
    {
        return 0.0f;
    }
    const float alpha = max(perceptualRoughness * perceptualRoughness,
                            BSDF_MIN_ROUGHNESS * BSDF_MIN_ROUGHNESS);
    const float alpha2 = alpha * alpha;
    const float lambdaV = NoL * sqrt(alpha2 + (1.0f - alpha2) * NoV * NoV);
    const float lambdaL = NoV * sqrt(alpha2 + (1.0f - alpha2) * NoL * NoL);
    return (2.0f * NoV * NoL) / max(lambdaV + lambdaL, 1e-7f);
}

public float BSDFGeometrySmith(float NoV, float NoL, float perceptualRoughness)
{
    return BSDFGeometrySmithCorrelated(NoV, NoL, perceptualRoughness);
}

// Generalized Schlick with automatic F82 conductor edge tint (Gulbrandsen / Kulla-Conty)
// For dielectrics (metalness == 0), evaluates exact Schlick with roughness attenuation.
// For conductors (metalness > 0), blends in the Gulbrandsen F82 edge tint (F82 = sqrt(F0)).
public float3 BSDFFresnelConductorF82(float cosTheta, float3 f0, float metalness, float perceptualRoughness)
{
    const float mu = clamp(cosTheta, 0.0f, 1.0f);
    const float m = 1.0f - mu;
    const float m2 = m * m;
    const float m5 = m2 * m2 * m;
    const float3 f90 = lerp(float3(1.0f), f0, perceptualRoughness * perceptualRoughness);
    const float3 schlick = f0 + (f90 - f0) * m5;

    if (metalness <= 0.0f)
    {
        return saturate(schlick);
    }

    const float3 autoF82 = sqrt(max(f0, 0.0f));
    const float3 a = (autoF82 - f0 - (f90 - f0) * 0.46518f) / max(0.07635f, 1e-5f);
    const float3 conductorF = schlick + a * mu * (m2 * m2);
    return saturate(lerp(schlick, conductorF, metalness));
}

public FBSDFContext BuildBSDFContext(Vertex vertex, Material material, float3 incomingRayDirection)
{
    FBSDFContext context;
    context.Position = vertex.Position;
    context.FrontFace = dot(incomingRayDirection, vertex.Normal) <= 0.0f;
    context.Normal = normalize(context.FrontFace ? vertex.Normal : -vertex.Normal);
    context.Wo = normalize(-incomingRayDirection);
    context.BaseColor = material.Diffuse.rgb;
    context.PerceptualRoughness = material.Fuzziness;
    context.Metalness = saturate(material.Metalness);
    context.EtaOutside = 1.0f;
    // Mixture is an opaque dielectric/metal blend and stores its surface IOR in
    // RefractionIndex. RefractionIndex2 is the transmitted medium used by Dielectric.
    const float surfaceIor = material.MaterialModel == MaterialDielectric
        ? material.RefractionIndex2
        : material.RefractionIndex;
    context.EtaInside = max(surfaceIor, 1.0001f);
    context.MaterialModel = material.MaterialModel;

    if (material.DiffuseTextureId >= 0)
    {
        context.BaseColor *= Bindless.GetSampleTexture(material.DiffuseTextureId)
                                     .SampleLevel(vertex.TexCoord, 0).rgb;
    }
    if (material.MRATextureId >= 0)
    {
        const float4 mra = Bindless.GetSampleTexture(material.MRATextureId)
                                   .SampleLevel(vertex.TexCoord, 0);
        context.PerceptualRoughness *= mra.g;
        context.Metalness *= mra.b;
    }
    if (material.MaterialModel == MaterialMetallic)
    {
        context.Metalness = 1.0f;
    }
    else if (material.MaterialModel == MaterialDielectric)
    {
        context.Metalness = 0.0f;
    }

    context.PerceptualRoughness = saturate(context.PerceptualRoughness);
    return context;
}

// Build a BSDF context from an already-resolved Primary Surface. Identical to BuildBSDFContext
// except that base color, roughness and metalness are supplied rather than sampled: a surface
// reconstructed from depth has no TexCoord, and those three values are precisely what the texture
// samples in BuildBSDFContext produce. The material is still needed for the IOR and the model.
public FBSDFContext BuildBSDFContextFromSurface(
    float3 position, float3 normal, float3 incomingRayDirection, Material material,
    float3 baseColor, float perceptualRoughness, float metalness)
{
    FBSDFContext context;
    context.Position = position;
    context.FrontFace = dot(incomingRayDirection, normal) <= 0.0f;
    context.Normal = normalize(context.FrontFace ? normal : -normal);
    context.Wo = normalize(-incomingRayDirection);
    context.BaseColor = baseColor;
    context.PerceptualRoughness = saturate(perceptualRoughness);
    context.Metalness = saturate(metalness);
    context.EtaOutside = 1.0f;
    const float surfaceIor = material.MaterialModel == MaterialDielectric
        ? material.RefractionIndex2
        : material.RefractionIndex;
    context.EtaInside = max(surfaceIor, 1.0001f);
    context.MaterialModel = material.MaterialModel;
    return context;
}

public float3 BSDFMaterialF0(FBSDFContext context)
{
    const float3 dielectricF0 = BSDFDielectricF0(context.EtaInside / context.EtaOutside);
    return lerp(dielectricF0, context.BaseColor, context.Metalness);
}

public FBSDFEval EvaluateSurfaceBSDF(FBSDFContext context, float3 wi)
{
    FBSDFEval result = FBSDFEval();
    const float3 N = context.Normal;
    const float3 V = normalize(context.Wo);
    const float3 L = normalize(wi);
    const float NoV = max(dot(N, V), 0.0f);
    const float NoL = dot(N, L);
    if (NoV <= 0.0f || NoL <= 0.0f)
    {
        return result;
    }

    if (context.MaterialModel == MaterialLambertian)
    {
        result.DiffuseDemodulated = float3(M_1_PI);
        result.DiffusePdf = NoL * M_1_PI;
        result.Flags = BSDF_LOBE_DIFFUSE;
        return result;
    }
    if (context.MaterialModel == MaterialDiffuseLight ||
        context.MaterialModel == MaterialIsotropic)
    {
        return result;
    }

    const float3 H = normalize(V + L);
    const float NoH = max(dot(N, H), 0.0f);
    const float VoH = max(dot(V, H), 0.0f);
    const float3 F = BSDFFresnelConductorF82(VoH, BSDFMaterialF0(context), context.Metalness, context.PerceptualRoughness);
    const float D = BSDFDistributionGGX(NoH, context.PerceptualRoughness);
    const float G = BSDFGeometrySmithCorrelated(NoV, NoL, context.PerceptualRoughness);

    result.Specular = (D * G * F) / max(4.0f * NoV * NoL, 1e-7f);
    result.SpecularPdf = D * NoH / max(4.0f * VoH, 1e-7f);
    result.Flags |= BSDF_LOBE_REFLECTION;
    if (context.PerceptualRoughness <= BSDF_DELTA_ROUGHNESS)
    {
        result.Flags |= BSDF_LOBE_DELTA;
    }

    if (context.MaterialModel == MaterialMixture)
    {
        const float3 kD = (1.0f - F) * (1.0f - context.Metalness);
        result.DiffuseDemodulated = kD * M_1_PI;
        result.DiffusePdf = NoL * M_1_PI;
        result.Flags |= BSDF_LOBE_DIFFUSE;
    }
    return result;
}

public float PowerHeuristic(float pdfA, float pdfB)
{
    const float a2 = pdfA * pdfA;
    const float b2 = pdfB * pdfB;
    return a2 / max(a2 + b2, 1e-12f);
}

/*
public float MIS(float pdfA, float pdfB)
{
    return PowerHeuristic(pdfA, pdfB);
}
*/

static const float M_LOG_1EM6 = 13.815510558f; // -ln(1e-6), scale matched to the natural log()
static const float M_INV_LOG_1EM6 = 1.0f / M_LOG_1EM6;

// Generalized power heuristic with a variable exponent β, rather than a
// blend of two different formulas. β moves smoothly between 2 (divergent pdfs --
// Veach's classic power heuristic, suppresses fireflies from a rarely
// hitting technique) and 1 (comparable pdfs -- balance heuristic, proven to be
// minimum-variance in exactly this regime). The sum MIS(a,b)+MIS(b,a)=1 holds
// automatically for any β -- it is the same property as in the original
// power heuristic, just generalized, not a separate proof for a hybrid.
public float MIS(float a, float b)
{
    float maxVal = max(a, b);
    if(maxVal <= 1e-10f) 
        return 0.5f; // Return 0.5f instead of 0.0f to keep rare events unbiased

    float minVal = min(a, b);
    float ratio  = minVal / maxVal;              // 0 -- strongly divergent pdfs; 1 -- identical

    // t approaches 0 linearly in log(ratio), not in ratio itself --
    // pdfs that differ by orders of magnitude (the typical path tracing case)
    // now get a smooth, rather than abrupt, transition to β=2
    float logRatio  = log(max(ratio, 1e-6f));
    float t         = saturate(1.0f + logRatio * M_INV_LOG_1EM6);
    float beta      = lerp(2.0f, 1.0f, t);

    float weightedRatio = pow(ratio, beta);

    return (a >= b) ? (rcp(1.0f + weightedRatio)) : (weightedRatio / (1.0f + weightedRatio));
}

// GGX VNDF directional probability density function for MIS (Heitz 2018)
public float EvaluateGlossyProposalPdf(FBSDFContext context, float3 wi)
{
    if (context.PerceptualRoughness <= BSDF_DELTA_ROUGHNESS)
    {
        return 0.0f;
    }
    const float3 N = context.Normal;
    const float3 V = normalize(context.Wo);
    const float3 L = normalize(wi);
    const float NoV = max(dot(N, V), 0.0f);
    const float NoL = dot(N, L);
    if (NoV <= 0.0f || NoL <= 0.0f)
    {
        return 0.0f;
    }
    const float3 H = normalize(V + L);
    const float NoH = max(dot(N, H), 0.0f);
    const float VoH = max(dot(V, H), 0.0f);
    if (NoH <= 0.0f || VoH <= 0.0f)
    {
        return 0.0f;
    }
    const float alpha = max(context.PerceptualRoughness * context.PerceptualRoughness,
                            BSDF_MIN_ROUGHNESS * BSDF_MIN_ROUGHNESS);
    const float alpha2 = alpha * alpha;
    const float D = BSDFDistributionGGX(NoH, context.PerceptualRoughness);
    // Stable VNDF PDF: G1(NoV) / (4 * NoV) = 0.5 / (NoV + sqrt(alpha2 + (1 - alpha2) * NoV^2))
    const float denom = NoV + sqrt(alpha2 + (1.0f - alpha2) * NoV * NoV);
    return (0.5f * D) / max(denom, 1e-7f);
}

public float3 SampleGlossyProposal(inout uint4 randomSeed, FBSDFContext context, out float pdf)
{
    pdf = 0.0f;
    const float3 V = normalize(context.Wo);
    const float3 N = context.Normal;
    if (context.PerceptualRoughness <= BSDF_DELTA_ROUGHNESS)
    {
        return normalize(reflect(-V, N));
    }
    float3 tangent, bitangent;
    ONB(N, tangent, bitangent);
    const float alpha = max(context.PerceptualRoughness * context.PerceptualRoughness, BSDF_MIN_ROUGHNESS * BSDF_MIN_ROUGHNESS);
    const float3 localV = to_local(V, tangent, bitangent, N);
    const float3 localH = ggx_sample_vndf(float2(alpha), localV, RandomFloat2(randomSeed));
    const float3 H = normalize(to_world(localH, tangent, bitangent, N));
    const float3 wi = normalize(reflect(-V, H));
    if (dot(N, wi) <= 0.0f)
    {
        return float3(0, 0, 0);
    }
    pdf = EvaluateGlossyProposalPdf(context, wi);
    return wi;
}

#endif
