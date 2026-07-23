// This shader library is part of orkige (orkitec Game engine).
// Derived from OGRE's SGXLib_CookTorrance.glsl (code adapted from Google
// Filament), SPDX-License-Identifier: Apache-2.0.
//
// The classic flavor's metal-rough lighting library, consumed by the engine's
// OrkigeMetalRoughLighting sub-render-state (MetalRoughLightingSrs.cpp). It
// reproduces the EXACT per-light response of the default render backend's PBS
// pixel shader so one authored material/light reads the same on both flavors:
//   - albedo (flat colour, vertex colour and sampled texture) is consumed RAW
//     (linear), never pow(2.2)-decoded - the engine-wide colour convention
//     (the other backend samples texels raw and takes material colours linear);
//   - the Lambert diffuse term carries the renormalised-diffuse factor
//     (energy bias/factor + light/view scatter over perceptual roughness);
//   - the specular fresnel uses VdotH with f90 = 1 and NO multi-scatter energy
//     compensation;
//   - the lit output is display-encoded with sqrt() at the end of the pixel
//     shader (Orkige_DisplayTransfer below) - the same cheap gamma transfer
//     the default backend applies when rendering to a non-sRGB target.
// The D (GGX) and V (height-correlated Smith) terms are shared by both
// backends' formulas already and stay verbatim.

// RTSLib_IBL.glsl (included later in the program when image lighting is
// active) branches on USE_LINEAR_COLOURS: without it, it would treat the lit
// colour as display-encoded and wrap the environment add in a decode/encode
// pair. Our lighting keeps the colour LINEAR until the final display transfer,
// which is exactly the USE_LINEAR_COLOURS contract - define it here so the
// environment contribution adds linearly. The texturing/colour macros of
// RTSLib_Colour.glsl were already frozen as no-ops at ITS inclusion point
// (before this file), so this define does NOT re-enable the albedo decode.
#ifndef USE_LINEAR_COLOURS
#define USE_LINEAR_COLOURS 1
#endif

#include "RTSLib_Lighting.glsl"

#ifdef OGRE_GLSLES
    // min roughness such that (MIN_PERCEPTUAL_ROUGHNESS^4) > 0 in fp16 (i.e. 2^(-14/4), rounded up)
    #define MIN_PERCEPTUAL_ROUGHNESS 0.089
#else
    #define MIN_PERCEPTUAL_ROUGHNESS 0.045
#endif

#define MEDIUMP_FLT_MAX    65504.0
#define saturateMediump(x) min(x, MEDIUMP_FLT_MAX)

#define MIN_N_DOT_V 1e-4

struct PixelParams
{
    vec3 baseColor;
    vec3 diffuseColor;
    float perceptualRoughness;
    float roughness;
    vec3  f0;
    vec3  dfg;
    vec3  energyCompensation;
    float ambientOcclusion;
};

float clampNoV(float NoV) {
    // Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
    return max(NoV, MIN_N_DOT_V);
}

// Computes x^5 using only multiply operations.
float pow5(float x) {
    float x2 = x * x;
    return x2 * x2 * x;
}

// https://google.github.io/filament/Filament.md.html#materialsystem/diffusebrdf
float Fd_Lambert() {
    return 1.0 / M_PI;
}

// https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/fresnel(specularf)
vec3 F_Schlick(const vec3 f0, float f90, float VoH) {
    // Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"
    return f0 + (f90 - f0) * pow5(1.0 - VoH);
}

vec3 computeDiffuseColor(const vec3 baseColor, float metallic) {
    return baseColor.rgb * (1.0 - metallic);
}

vec3 computeF0(const vec3 baseColor, float metallic, float reflectance) {
    return baseColor.rgb * metallic + (reflectance * (1.0 - metallic));
}

float perceptualRoughnessToRoughness(float perceptualRoughness) {
    return perceptualRoughness * perceptualRoughness;
}

// https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/geometricshadowing(specularg)
float V_SmithGGXCorrelated(float roughness, float NoV, float NoL) {
    // Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
    float a2 = roughness * roughness;
    float lambdaV = NoL * sqrt((NoV - a2 * NoV) * NoV + a2);
    float lambdaL = NoV * sqrt((NoL - a2 * NoL) * NoL + a2);
    float v = 0.5 / (lambdaV + lambdaL);
    // a2=0 => v = 1 / 4*NoL*NoV   => min=1/4, max=+inf
    // a2=1 => v = 1 / 2*(NoL+NoV) => min=1/4, max=+inf
    // clamp to the maximum value representable in mediump
    return saturateMediump(v);
}

// https://google.github.io/filament/Filament.md.html#materialsystem/specularbrdf/normaldistributionfunction(speculard)
float D_GGX(float roughness, float32_t NoH, const vec3 h, const vec3 n) {
    // Walter et al. 2007, "Microfacet Models for Refraction through Rough Surfaces"

    // In mediump, there are two problems computing 1.0 - NoH^2
    // 1) 1.0 - NoH^2 suffers floating point cancellation when NoH^2 is close to 1 (highlights)
    // 2) NoH doesn't have enough precision around 1.0
    // Both problem can be fixed by computing 1-NoH^2 in highp and providing NoH in highp as well

    // However, we can do better using Lagrange's identity:
    //      ||a x b||^2 = ||a||^2 ||b||^2 - (a . b)^2
    // since N and H are unit vectors: ||N x H||^2 = 1.0 - NoH^2
    // This computes 1.0 - NoH^2 directly (which is close to zero in the highlights and has
    // enough precision).
    // Overall this yields better performance, keeping all computations in mediump
#ifdef OGRE_GLSLES
    vec3 NxH = cross(n, h);
    float oneMinusNoHSquared = dot(NxH, NxH);
#else
    float oneMinusNoHSquared = 1.0 - NoH * NoH;
#endif

    float a = NoH * roughness;
    float k = roughness / (oneMinusNoHSquared + a * a);
    float d = k * k * (1.0 / M_PI);
    return saturateMediump(d);
}

vec3 evaluateLight(
                in f32vec3 vNormal,
                in vec3 viewPos,
                in f32vec4 lightPos,
                in vec3 lightColor,
                in vec4 pointParams,
                in vec4 vLightDirView,
                in vec4 spotParams,
                in PixelParams pixel)
{
    f32vec3 vLightView = lightPos.xyz;
    float fLightD = 0.0;

    if (lightPos.w != 0.0)
    {
        vLightView -= viewPos; // to light
        fLightD     = length(vLightView);

        if(fLightD > pointParams.x)
            return vec3_splat(0.0);
    }

	vLightView		   = normalize(vLightView);

	f32vec3 vNormalView = normalize(vNormal);
	float NoL		 = saturate(dot(vNormalView, vLightView));

    if(NoL <= 0.0)
        return vec3_splat(0.0); // not lit by this light

	vec3 vView       = -normalize(viewPos);

    // https://google.github.io/filament/Filament.md.html#materialsystem/standardmodelsummary
    vec3 h    = normalize(vView + vLightView);
    float NoH = saturate(dot(vNormalView, h));
    float VoH = saturate(dot(vView, h));
    float NoV = clampNoV(abs(dot(vNormalView, vView)));

    float V = V_SmithGGXCorrelated(pixel.roughness, NoV, NoL);
    // the default backend's specular fresnel: VdotH with f90 = 1 (no
    // dielectric f90 cap, no multi-scatter energy compensation)
    vec3 F  = F_Schlick(pixel.f0, 1.0, VoH);
    float D = D_GGX(pixel.roughness, NoH, h, vNormalView);

    vec3 Fr = (D * V) * F;

    // the default backend's renormalised Lambert diffuse ("Moving Frostbite
    // to Physically Based Rendering", Lagarde & de Rousiers): a roughness-
    // driven energy factor plus grazing light/view scatter over the
    // PERCEPTUAL roughness - identical maths to its PBS pixel shader
    float energyBias    = 0.5 * pixel.perceptualRoughness;
    float energyFactor  = mix(1.0, 1.0 / 1.51, pixel.perceptualRoughness);
    float fd90          = energyBias + 2.0 * VoH * VoH * pixel.perceptualRoughness;
    float lightScatter  = 1.0 + (fd90 - 1.0) * pow5(1.0 - NoL);
    float viewScatter   = 1.0 + (fd90 - 1.0) * pow5(1.0 - NoV);
    vec3 Fd = pixel.diffuseColor * Fd_Lambert()
        * (lightScatter * viewScatter * energyFactor);

    vec3 color = NoL * lightColor * (Fr + Fd);

    // the default backend's point/spot falloff (its clustered-forward light
    // loop): the authored constant term is replaced by a fixed 0.5 and a
    // linear fade to zero at the range end multiplies in - mirrored exactly
    // so a lamp reads the same on both flavors. pointParams = (range,
    // constant, linear, quadratic); directional lights skip attenuation on
    // both backends.
    if (lightPos.w != 0.0)
    {
        color *= 1.0 / (0.5 + (pointParams.z + pointParams.w * fLightD)
            * fLightD);
        color *= max((pointParams.x - fLightD) / pointParams.x, 0.0);
    }

    if(spotParams.w != 0.0)
    {
        color *= getAngleAttenuation(spotParams.xyz, vLightDirView.xyz, vLightView);
    }

    return color;
}

void PBR_MakeParams(in vec3 baseColor, in vec3 ormParam, inout PixelParams pixel)
{
    pixel.baseColor = baseColor;

    pixel.ambientOcclusion = saturate(ormParam.x);
    float perceptualRoughness = ormParam.y;
    // Clamp the roughness to a minimum value to avoid divisions by 0 during lighting
    pixel.perceptualRoughness = clamp(perceptualRoughness, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
    // Remaps the roughness to a perceptually linear roughness (roughness^2)
    pixel.roughness = perceptualRoughnessToRoughness(pixel.perceptualRoughness);

    float metallic = saturate(ormParam.z);
    pixel.f0 = computeF0(baseColor, metallic, 0.04); // using fixed IOR of 1.5 as per glTF spec
    pixel.diffuseColor = computeDiffuseColor(baseColor, metallic);

    // dfg/energyCompensation stay in the struct for the image-based-lighting
    // stage (RTSLib_IBL reads pixel.dfg); the direct-light path above does not
    // consume them (no multi-scatter compensation, matching the default
    // backend's response)
    pixel.dfg = vec3_splat(0.5);
    pixel.energyCompensation = vec3_splat(1.0);
}

//! the image-based-lighting stage's environment fill, consumed by the engine's
//! OrkigeImageLighting sub-render-state (ImageLightingSrs.cpp). It reproduces
//! the default backend's LIVE env term (its BRDF env block with the constant
//! envBRDF (1,0,1) and diffuse fresnel 1 - that backend loads no LTC lookup
//! table and keeps the default BRDF, so those are the terms that actually run):
//!   fresnelS = f0 + (1-NoV)^5 * (max(1-perceptualRoughness, f0) - f0)
//!   Rs = envS(lod = pr * mipCount * (2 - pr)) * fresnelS   [kS = 1 on
//!        generated materials on both flavors]
//!   Rd = envD(last mip) * albedo * (1 - metalness)
//!   colour += envScale * (Rs + Rd)
//! The chain cubemap stores the sky model's CLAMPED LINEAR radiance and is
//! sampled RAW (never sRGB-decoded), and the fill adds LINEARLY - the one
//! sqrt display transfer at the end of the pixel shader encodes it together
//! with the direct and ambient terms, exactly like the default backend's
//! single linear accumulation. envScale carries the authored intensity times
//! the shared fill weight (core_util/IblPreset.h fillScale), the SAME number
//! the default backend's envmapScale lane carries.
void Orkige_ImageLighting(
                in PixelParams pixel,
                in f32vec3 vNormal,
                in vec3 viewPos,
                in mat4 invViewMat,
                in samplerCube envMap,
                in float envExtraMips,
                in float envScale,
                inout vec3 vOutColour)
{
    vec3 n = normalize(vNormal);
    vec3 v = -normalize(viewPos);
    float NoV = saturate(dot(n, v));
    vec3 r = reflect(-v, n);
    // view -> world for cubemap sampling, with the stock stage's z-flip
    // convention (the selfcheck's mirror-face leg pins this: the +X view
    // reflects the -X face on both flavors)
    r = normalize(mul(invViewMat, vec4(r, 0.0)).xyz);
    r.z *= -1.0;
    vec3 nWorld = normalize(mul(invViewMat, vec4(n, 0.0)).xyz);
    nWorld.z *= -1.0;
    // the default backend's roughness->lod map counts mips INCLUDING the
    // base level; the classic texture-size autoparam excludes it
    float mipCount = envExtraMips + 1.0;
    float lodS = pixel.perceptualRoughness * mipCount
        * (2.0 - pixel.perceptualRoughness);
    vec3 envS = textureCubeLod(envMap, r, lodS).rgb;
    vec3 envD = textureCubeLod(envMap, nWorld, envExtraMips).rgb;
    vec3 fresnelS = pixel.f0 + pow5(1.0 - NoV)
        * (max(vec3_splat(1.0 - pixel.perceptualRoughness), pixel.f0) - pixel.f0);
    vOutColour += envScale * (envS * fresnelS + envD * pixel.diffuseColor);
}

//! the shared display transfer of the lit output: the default backend renders
//! linear and applies sqrt() when the target is not an sRGB surface - the
//! classic window is exactly that, so the SAME transfer keeps a lit pixel
//! byte-comparable across flavors. Called once at the post-process stage.
void Orkige_DisplayTransfer(inout vec4 colour)
{
    colour.rgb = sqrt(max(colour.rgb, vec3_splat(0.0)));
}

#if LIGHT_COUNT > 0
void PBR_Lights(
#ifdef SHADOWLIGHT_COUNT
                in float shadowFactor[SHADOWLIGHT_COUNT],
#endif
                in vec3 vNormal,
                in vec3 viewPos,
                in vec4 ambient,
                in f32vec4 lightPos[LIGHT_COUNT],
                in f32vec4 lightColor[LIGHT_COUNT],
                in f32vec4 pointParams[LIGHT_COUNT],
                in f32vec4 vLightDirView[LIGHT_COUNT],
                in f32vec4 spotParams[LIGHT_COUNT],
                in PixelParams pixel,
                inout vec3 vOutColour)
{
    for(int i = 0; i < LIGHT_COUNT; i++)
    {
        vec3 lightVal = evaluateLight(vNormal, viewPos, lightPos[i], lightColor[i].xyz, pointParams[i], vLightDirView[i], spotParams[i],
                        pixel);

#ifdef SHADOWLIGHT_COUNT
        if(i < SHADOWLIGHT_COUNT)
            lightVal *= shadowFactor[i];
#endif
        vOutColour += lightVal;
    }
    // apply ambient occlusion to the indirect (ambient) term only
    vOutColour += pixel.baseColor * ambient.rgb * pixel.ambientOcclusion;
}
#endif
