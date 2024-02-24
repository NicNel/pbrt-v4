// pbrt is Copyright(c) 1998-2020 Matt Pharr, Wenzel Jakob, and Greg Humphreys.
// The pbrt source code is licensed under the Apache License, Version 2.0.
// SPDX: Apache-2.0

#ifndef PBRT_BXDFS_H
#define PBRT_BXDFS_H

#include <pbrt/pbrt.h>

#include <pbrt/base/bxdf.h>
#include <pbrt/interaction.h>
#include <pbrt/media.h>
#include <pbrt/options.h>
#include <pbrt/util/math.h>
#include <pbrt/util/memory.h>
#include <pbrt/util/pstd.h>
#include <pbrt/util/scattering.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/taggedptr.h>
#include <pbrt/util/vecmath.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

namespace pbrt {

// DisneyBxDF Definition
// https://blog.selfshadow.com/publications/s2012-shading-course/burley/s2012_pbs_disney_brdf_notes_v3.pdf
class DisneyBxDF {
  public:
    // DisneyBxDF Public Methods
    DisneyBxDF() = default;
    PBRT_CPU_GPU
    DisneyBxDF(SampledSpectrum color, Float eta, Float roughness, Float specular,
               Float clearcoat, Float metallic, Float subsurface, Float sheen,
               Float sheenTint, Float clearcoatGloss, Float Lum, bool isSpecular)
        : Lum(Lum),
          color(color),
          eta(eta),
          roughness(roughness),
          specular(specular),
          sheen(sheen),
          clearcoat(clearcoat),
          subsurface(subsurface),
          metallic(metallic),
          sheenTint(sheenTint),
          clearcoatGloss(clearcoatGloss),
          isSpecular(isSpecular) {
        // absorption = SampledSpectrum(0.f);
        // metallic = 0.0f;
        //  subsurface = 0.0f;
        //  specular = 1.f;
        //  roughness = 0.2f;
        specularTint = 0.0f;
        anisotropic = 0.0f;
        // sheen = 0.0f;
        // sheenTint = 0.0f;
        // clearcoat = 0.0f;
        //clearcoatGloss = 1.0f;
        transmission = 0.0f;
        twoSided = true;
    }

    PBRT_CPU_GPU
    Float SchlickFresnel(Float u) const {
        Float m = Clamp(1.f - u, 0.0f, 1.0f);
        Float m2 = m * m;
        return m2 * m2 * m;
    }

    PBRT_CPU_GPU
    Float GTR1(Float cosTheta, Float a) const {
        if (a >= 1.f)
            return InvPi;
        Float a2 = a * a;
        Float t = 1.f + (a2 - 1) * cosTheta * cosTheta;
        return (a2 - 1.f) / (Pi * logf(a2) * t);
    }

    PBRT_CPU_GPU
    Float GTR2(Float cosTheta, Float a) const {
        Float a2 = a * a;
        Float t = 1.0f + (a2 - 1.0f) * cosTheta * cosTheta;
        return a2 / (Pi * t * t);
    }

    PBRT_CPU_GPU
    Float SmithGGXVN(Vector3f w, Float a) const {
        Float a2 = a * a;
        Float th = TanTheta(w);
        Float th2 = th * th;
        Float root = sqrtf(1.f + a2 * th2);
        return 2.0f / (1.f + root);
    }

    PBRT_CPU_GPU
    Float SchlickF0FromEta(Float eta) const { return Sqr(eta - 1.f) / Sqr(eta + 1.f); }

    // ggx vndf
    PBRT_CPU_GPU
    Vector3f Sample_wm(Vector3f w, Point2f u) const {
        float ax = roughness, ay = roughness;
        Vector3f V = w;
        float r1 = u.x, r2 = u.y;
        Vector3f Vh = Normalize(Vector3f(ax * V.x, ay * V.y, V.z));

        float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
        Vector3f T1 =
            lensq > 0. ? Vector3f(-Vh.y, Vh.x, 0) * InvSqrtf(lensq) : Vector3f(1, 0, 0);
        Vector3f T2 = Cross(Vh, T1);

        float r = sqrtf(r1);
        float phi = 2.0 * Pi * r2;
        float t1 = r * cos(phi);
        float t2 = r * sin(phi);
        float s = 0.5 * (1.0 + Vh.z);
        t2 = (1.0f - s) * sqrtf(1.0f - t1 * t1) + s * t2;

        Vector3f Nh =
            t1 * T1 + t2 * T2 + sqrtf(std::max<float>(0.0, 1.0 - t1 * t1 - t2 * t2)) * Vh;

        return Normalize(Vector3f(ax * Nh.x, ay * Nh.y, std::max<float>(0.0, Nh.z)));
    }

    PBRT_CPU_GPU
    Vector3f SampleCoating(Vector3f wo, Point2f u) const {
        Float gloss = Lerp(clearcoatGloss, 0.1f, 0.001f);
        Float alpha2 = gloss * gloss;
        Float cosTheta = sqrtf(std::max<Float>(
            0.0001f, (1.0f - powf(alpha2, 1.0f - u.x)) / (1.0f - alpha2)));
        Float sinTheta = sqrtf(std::max<Float>(0.0001f, 1.0f - cosTheta * cosTheta));
        Float phi = 2.0f * Pi * u.y;

        // half vector
        Vector3f wh = Vector3f(sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta);
        if (CosTheta(wo) * CosTheta(wh) <= 0.0f)
            wh *= -1.f;
        // reflect
        return Normalize(2.0f * Dot(wh, wo) * wh - wo);  
    }

    PBRT_CPU_GPU
    void ComputeWeights(Float &sr, Float &dr,
                        Float &cr) const {
        Float m = metallic;
        Float d = (1.0f - metallic);

        Float dw = d;
        Float sw = m + d;
        Float cw = clearcoat;
        Float norm = 1.0f / (sw + dw + cw);

        sr = sw * norm;
        dr = dw * norm;
        cr = cw * norm;
    }

    PBRT_CPU_GPU
    SampledSpectrum DisneyDiffuseF(Vector3f wo, Vector3f wi, Vector3f wh) const {
        Float rc = std::max<Float>(0.001f, roughness);
        Float Fo = SchlickFresnel(CosTheta(wo)), Fi = SchlickFresnel(CosTheta(wi));
        Float c2 = Dot(wi, wh);
        Float Fd90 = 0.5f + 2.f * rc * c2 * c2;
        Float Fd = Lerp(Fi, 1.0f, Fd90) * Lerp(Fo, 1.0f, Fd90);

        return color * InvPi * Fd * (1.0f - metallic);
    }

    PBRT_CPU_GPU
    SampledSpectrum DisneySubsurfaceF(Vector3f wo, Vector3f wi, Vector3f wh) const {
        Float rc = std::max<Float>(0.001f, roughness);
        Float cosWo = AbsCosTheta(wo);
        Float cosWi = AbsCosTheta(wi);
        Float FL = SchlickFresnel(cosWi);
        Float FV = SchlickFresnel(cosWo);
        Float c2 = Dot(wi, wh);
        Float Fss90 = c2 * c2 * rc;
        Float Fss = Lerp(FL, 1.0f, Fss90) * Lerp(FV, 1.0f, Fss90);
        Float ss = 1.25f * (Fss * (1.0f / (cosWi + cosWo) - 0.5f) + 0.5f);

        return InvPi * ss * color * (1.f - metallic);
    }

    PBRT_CPU_GPU
    SampledSpectrum BRDF_F(Vector3f wo, Vector3f wi) const {
        if (twoSided && wo.z < 0) {
            wo = -wo;
            wi = -wi;
        }

        Vector3f wh = Normalize(wi + wo);
        wh = FaceForward(wh, Normal3f(0, 0, 1));
        Float cosWh = CosTheta(wh);

        if (!SameHemisphere(wo, wi)) {
            // transmittance
            if (subsurface > 0.0f) {
                SampledSpectrum subs = DisneySubsurfaceF(wo, wi, wh);
                return subs;
            } else
                return SampledSpectrum(0);
        } else {
            SampledSpectrum Ctint = Lum > 0 ? (color / Lum) : SampledSpectrum(1.);
            Float FH = SchlickFresnel(Dot(wi, wh));

            // main reflection
            Float D = GTR2(cosWh, roughness);
            Float FD = FrDielectric(Dot(wo, wh), eta);
            SampledSpectrum F = SampledSpectrum(FD);
            if (isSpecular) {
                F = Lerp(FH, specular*0.08f*SampledSpectrum(1.f), SampledSpectrum(1));
            }
            F = Lerp(metallic, F, color);
            Float G = SmithGGXVN(wo, roughness) * SmithGGXVN(wi, roughness);

            // coating
            Float Dc = GTR1(cosWh, Lerp(clearcoatGloss, .1f, .001f));
            Float Fc = Lerp(FH, .04f, 1.0f);
            Float Gc = SmithGGXVN(wo, .25f) * SmithGGXVN(wi, .25f);

            Float J = 1.f / (4.f * AbsCosTheta(wo) * AbsCosTheta(wi));
            SampledSpectrum spec = (D * F * G) * J;
            SampledSpectrum diffuse = DisneyDiffuseF(wo, wi, wh);
            Float coat = (Dc * Fc * Gc) * J;
            
            // sheen
            SampledSpectrum tintF = Lerp(sheenTint, SampledSpectrum(1), Ctint);
            SampledSpectrum sheenC = FH * sheen * tintF;

            return diffuse + sheenC + spec + SampledSpectrum(clearcoat * coat);
        }
    }

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        return BRDF_F(wo, wi);
    }

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(
        Vector3f wo, Float uc, Point2f u, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        bool flip = false;
        if (twoSided && wo.z < 0) {
            wo = -wo;
            flip = true;
        }

        // Declare _RNG_ for difftrans sampling
        RNG rng(Hash(GetOptions().seed, wo), Hash(uc, u));
        auto r = [&rng]() { return std::min<Float>(rng.Uniform<Float>(), OneMinusEpsilon); };

        Vector3f wi(0, 0, 0);
        BxDFFlags flag = BxDFFlags::Unset;

        Float sr, cr, dr;
        ComputeWeights(sr, dr, cr);

        Float coatingTh = sr + cr;
        Float diffuseTh = sr + cr + dr;

        if (uc <= sr) {
            // specular reflection sampling
            Vector3f wm = Sample_wm(wo, u);
            if (CosTheta(wo) * CosTheta(wm) <= 0.0f)
                wm = -wm;
            wi = Reflect(wo, wm);
            if (!SameHemisphere(wo, wi))
                return {};
            flag = BxDFFlags::GlossyReflection;
        } else if (uc > sr && uc <= coatingTh) {
            // coating reflection sampling
            wi = SampleCoating(wo, u);
            if (!SameHemisphere(wo, wi))
                return {};
            flag = BxDFFlags::GlossyReflection;
        } else if (uc > coatingTh && uc <= diffuseTh) {
            // not sure about transmittance sampling for now
            if (r() <= subsurface) {
                // diffuse Transmission sampling
                wi = SampleCosineHemisphere(u);
                if (wo.z > 0)
                    wi.z *= -1;
                flag = BxDFFlags::DiffuseTransmission;
            } else {
                // diffuse reflection sampling
                wi = SampleCosineHemisphere(u);
                if (wo.z < 0)
                    wi.z *= -1;
                flag = BxDFFlags::DiffuseReflection;
            }
        } else
            return {};

        Float pdf = BRDF_PDF(wo, wi);
        SampledSpectrum fd = BRDF_F(wo, wi);
        if (flip)
            wi = -wi;
        return BSDFSample(fd, wi, pdf, flag);
    }
    
    PBRT_CPU_GPU
    Float BRDF_PDF(Vector3f wo, Vector3f wi) const {
        if (twoSided && wo.z < 0) {
            wo = -wo;
            wi = -wi;
        }
        Vector3f wh = Normalize(wo + wi);
        wh = FaceForward(wh, Normal3f(0, 0, 1));

        Float sr, cr, dr;
        ComputeWeights(sr, dr, cr);

        Float absCosWh = AbsCosTheta(wh);
        Float G1 = SmithGGXVN(wo, roughness); 
        Float D = GTR2(absCosWh, roughness);
        Float J = 1.0f / (4.0f * AbsDot(wo, wh));
        Float pdfSpec = (G1 * AbsDot(wo, wh) * D * J) / AbsCosTheta(wo);
        Float pdfDiff = CosineHemispherePDF(AbsCosTheta(wi));

        Float Dc = GTR1(absCosWh, Lerp(clearcoatGloss, .1f, .001f));
        Float pdfCc = (Dc * absCosWh) * J;

        return pdfSpec * sr + pdfDiff * dr + pdfCc * cr;
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        return BRDF_PDF(wo, wi);
    }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "DisneyBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        BxDFFlags flags =
            (BxDFFlags::Reflection | BxDFFlags::Specular | BxDFFlags::GlossyReflection);
        return flags | (BxDFFlags::DiffuseReflection | BxDFFlags::DiffuseTransmission);
    }

  private:
    SampledSpectrum color;
    Float eta;
    bool twoSided;
    bool isSpecular;
    Float metallic, subsurface, specular, roughness, specularTint, anisotropic, sheen,
        sheenTint, clearcoat, clearcoatGloss, transmission;
    Float Lum;
};

// DiffuseBxDF Definition
class DiffuseBxDF {
  public:
    // DiffuseBxDF Public Methods
    DiffuseBxDF() = default;
    PBRT_CPU_GPU
    DiffuseBxDF(SampledSpectrum R) : R(R) {}

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        if (!SameHemisphere(wo, wi))
            return SampledSpectrum(0.f);
        return R * InvPi;
    }

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(
        Vector3f wo, Float uc, Point2f u, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return {};
        // Sample cosine-weighted hemisphere to compute _wi_ and _pdf_
        Vector3f wi = SampleCosineHemisphere(u);
        if (wo.z < 0)
            wi.z *= -1;
        Float pdf = CosineHemispherePDF(AbsCosTheta(wi));

        return BSDFSample(R * InvPi, wi, pdf, BxDFFlags::DiffuseReflection);
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection) || !SameHemisphere(wo, wi))
            return 0;
        return CosineHemispherePDF(AbsCosTheta(wi));
    }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "DiffuseBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return R ? BxDFFlags::DiffuseReflection : BxDFFlags::Unset;
    }

  private:
    SampledSpectrum R;
};

// DiffuseTransmissionBxDF Definition
class DiffuseTransmissionBxDF {
  public:
    // DiffuseTransmissionBxDF Public Methods
    DiffuseTransmissionBxDF() = default;
    PBRT_CPU_GPU
    DiffuseTransmissionBxDF(SampledSpectrum R, SampledSpectrum T) : R(R), T(T) {}

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        return SameHemisphere(wo, wi) ? (R * InvPi) : (T * InvPi);
    }

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(
        Vector3f wo, Float uc, Point2f u, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        // Compute reflection and transmission probabilities for diffuse BSDF
        Float pr = R.MaxComponentValue(), pt = T.MaxComponentValue();
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            pr = 0;
        if (!(sampleFlags & BxDFReflTransFlags::Transmission))
            pt = 0;
        if (pr == 0 && pt == 0)
            return {};

        // Randomly sample diffuse BSDF reflection or transmission
        if (uc < pr / (pr + pt)) {
            // Sample diffuse BSDF reflection
            Vector3f wi = SampleCosineHemisphere(u);
            if (wo.z < 0)
                wi.z *= -1;
            Float pdf = CosineHemispherePDF(AbsCosTheta(wi)) * pr / (pr + pt);
            return BSDFSample(f(wo, wi, mode), wi, pdf, BxDFFlags::DiffuseReflection);

        } else {
            // Sample diffuse BSDF transmission
            Vector3f wi = SampleCosineHemisphere(u);
            if (wo.z > 0)
                wi.z *= -1;
            Float pdf = CosineHemispherePDF(AbsCosTheta(wi)) * pt / (pr + pt);
            return BSDFSample(f(wo, wi, mode), wi, pdf, BxDFFlags::DiffuseTransmission);
        }
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        // Compute reflection and transmission probabilities for diffuse BSDF
        Float pr = R.MaxComponentValue(), pt = T.MaxComponentValue();
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            pr = 0;
        if (!(sampleFlags & BxDFReflTransFlags::Transmission))
            pt = 0;
        if (pr == 0 && pt == 0)
            return {};

        if (SameHemisphere(wo, wi))
            return pr / (pr + pt) * CosineHemispherePDF(AbsCosTheta(wi));
        else
            return pt / (pr + pt) * CosineHemispherePDF(AbsCosTheta(wi));
    }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "DiffuseTransmissionBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return ((R ? BxDFFlags::DiffuseReflection : BxDFFlags::Unset) |
                (T ? BxDFFlags::DiffuseTransmission : BxDFFlags::Unset));
    }

  private:
    // DiffuseTransmissionBxDF Private Members
    SampledSpectrum R, T;
};

// DielectricBxDF Definition
class DielectricBxDF {
  public:
    // DielectricBxDF Public Methods
    DielectricBxDF() = default;
    PBRT_CPU_GPU
    DielectricBxDF(Float eta, TrowbridgeReitzDistribution mfDistrib)
        : eta(eta), mfDistrib(mfDistrib) {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        BxDFFlags flags = (eta == 1) ? BxDFFlags::Transmission
                                     : (BxDFFlags::Reflection | BxDFFlags::Transmission);
        return flags |
               (mfDistrib.EffectivelySmooth() ? BxDFFlags::Specular : BxDFFlags::Glossy);
    }

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(
        Vector3f wo, Float uc, Point2f u, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const;

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const;
    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const;

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "DielectricBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() { mfDistrib.Regularize(); }

  private:
    // DielectricBxDF Private Members
    Float eta;
    TrowbridgeReitzDistribution mfDistrib;
};

// ThinDielectricBxDF Definition
class ThinDielectricBxDF {
  public:
    // ThinDielectricBxDF Public Methods
    ThinDielectricBxDF() = default;
    PBRT_CPU_GPU
    ThinDielectricBxDF(Float eta) : eta(eta) {}

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        return SampledSpectrum(0);
    }

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(Vector3f wo, Float uc, Point2f u,
                                        TransportMode mode,
                                        BxDFReflTransFlags sampleFlags) const {
        Float R = FrDielectric(AbsCosTheta(wo), eta), T = 1 - R;
        // Compute _R_ and _T_ accounting for scattering between interfaces
        if (R < 1) {
            R += Sqr(T) * R / (1 - Sqr(R));
            T = 1 - R;
        }

        // Compute probabilities _pr_ and _pt_ for sampling reflection and transmission
        Float pr = R, pt = T;
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            pr = 0;
        if (!(sampleFlags & BxDFReflTransFlags::Transmission))
            pt = 0;
        if (pr == 0 && pt == 0)
            return {};

        if (uc < pr / (pr + pt)) {
            // Sample perfect specular dielectric BRDF
            Vector3f wi(-wo.x, -wo.y, wo.z);
            SampledSpectrum fr(R / AbsCosTheta(wi));
            return BSDFSample(fr, wi, pr / (pr + pt), BxDFFlags::SpecularReflection);

        } else {
            // Sample perfect specular transmission at thin dielectric interface
            Vector3f wi = -wo;
            SampledSpectrum ft(T / AbsCosTheta(wi));
            return BSDFSample(ft, wi, pt / (pr + pt), BxDFFlags::SpecularTransmission);
        }
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const {
        return 0;
    }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "ThinDielectricBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() { /* TODO */
    }

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return (BxDFFlags::Reflection | BxDFFlags::Transmission | BxDFFlags::Specular);
    }

  private:
    Float eta;
};

// ConductorBxDF Definition
class ConductorBxDF {
  public:
    // ConductorBxDF Public Methods
    ConductorBxDF() = default;
    PBRT_CPU_GPU
    ConductorBxDF(const TrowbridgeReitzDistribution &mfDistrib, SampledSpectrum eta,
                  SampledSpectrum k)
        : mfDistrib(mfDistrib), eta(eta), k(k) {}

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return mfDistrib.EffectivelySmooth() ? BxDFFlags::SpecularReflection
                                             : BxDFFlags::GlossyReflection;
    }

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(
        Vector3f wo, Float uc, Point2f u, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return {};
        if (mfDistrib.EffectivelySmooth()) {
            // Sample perfect specular conductor BRDF
            Vector3f wi(-wo.x, -wo.y, wo.z);
            SampledSpectrum f = FrComplex(AbsCosTheta(wi), eta, k) / AbsCosTheta(wi);
            return BSDFSample(f, wi, 1, BxDFFlags::SpecularReflection);
        }
        // Sample rough conductor BRDF
        // Sample microfacet normal $\wm$ and reflected direction $\wi$
        if (wo.z == 0)
            return {};
        Vector3f wm = mfDistrib.Sample_wm(wo, u);
        Vector3f wi = Reflect(wo, wm);
        if (!SameHemisphere(wo, wi))
            return {};

        // Compute PDF of _wi_ for microfacet reflection
        Float pdf = mfDistrib.PDF(wo, wm) / (4 * AbsDot(wo, wm));

        Float cosTheta_o = AbsCosTheta(wo), cosTheta_i = AbsCosTheta(wi);
        if (cosTheta_i == 0 || cosTheta_o == 0)
            return {};
        // Evaluate Fresnel factor _F_ for conductor BRDF
        SampledSpectrum F = FrComplex(AbsDot(wo, wm), eta, k);

        SampledSpectrum f =
            mfDistrib.D(wm) * F * mfDistrib.G(wo, wi) / (4 * cosTheta_i * cosTheta_o);
        return BSDFSample(f, wi, pdf, BxDFFlags::GlossyReflection);
    }

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        if (!SameHemisphere(wo, wi))
            return {};
        if (mfDistrib.EffectivelySmooth())
            return {};
        // Evaluate rough conductor BRDF
        // Compute cosines and $\wm$ for conductor BRDF
        Float cosTheta_o = AbsCosTheta(wo), cosTheta_i = AbsCosTheta(wi);
        if (cosTheta_i == 0 || cosTheta_o == 0)
            return {};
        Vector3f wm = wi + wo;
        if (LengthSquared(wm) == 0)
            return {};
        wm = Normalize(wm);

        // Evaluate Fresnel factor _F_ for conductor BRDF
        SampledSpectrum F = FrComplex(AbsDot(wo, wm), eta, k);

        return mfDistrib.D(wm) * F * mfDistrib.G(wo, wi) / (4 * cosTheta_i * cosTheta_o);
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return 0;
        if (!SameHemisphere(wo, wi))
            return 0;
        if (mfDistrib.EffectivelySmooth())
            return 0;
        // Evaluate sampling PDF of rough conductor BRDF
        Vector3f wm = wo + wi;
        CHECK_RARE(1e-5f, LengthSquared(wm) == 0);
        if (LengthSquared(wm) == 0)
            return 0;
        wm = FaceForward(Normalize(wm), Normal3f(0, 0, 1));
        return mfDistrib.PDF(wo, wm) / (4 * AbsDot(wo, wm));
    }

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "ConductorBxDF"; }
    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() { mfDistrib.Regularize(); }

  private:
    // ConductorBxDF Private Members
    TrowbridgeReitzDistribution mfDistrib;
    SampledSpectrum eta, k;
};

// TopOrBottomBxDF Definition
template <typename TopBxDF, typename BottomBxDF>
class TopOrBottomBxDF {
  public:
    // TopOrBottomBxDF Public Methods
    TopOrBottomBxDF() = default;
    PBRT_CPU_GPU
    TopOrBottomBxDF &operator=(const TopBxDF *t) {
        top = t;
        bottom = nullptr;
        return *this;
    }
    PBRT_CPU_GPU
    TopOrBottomBxDF &operator=(const BottomBxDF *b) {
        bottom = b;
        top = nullptr;
        return *this;
    }

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        return top ? top->f(wo, wi, mode) : bottom->f(wo, wi, mode);
    }

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(
        Vector3f wo, Float uc, Point2f u, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        return top ? top->Sample_f(wo, uc, u, mode, sampleFlags)
                   : bottom->Sample_f(wo, uc, u, mode, sampleFlags);
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        return top ? top->PDF(wo, wi, mode, sampleFlags)
                   : bottom->PDF(wo, wi, mode, sampleFlags);
    }

    PBRT_CPU_GPU
    BxDFFlags Flags() const { return top ? top->Flags() : bottom->Flags(); }

  private:
    const TopBxDF *top = nullptr;
    const BottomBxDF *bottom = nullptr;
};

// LayeredBxDF Definition
template <typename TopBxDF, typename BottomBxDF, bool twoSided>
class LayeredBxDF {
  public:
    // LayeredBxDF Public Methods
    LayeredBxDF() = default;
    PBRT_CPU_GPU
    LayeredBxDF(TopBxDF top, BottomBxDF bottom, Float thickness,
                const SampledSpectrum &albedo, Float g, int maxDepth, int nSamples)
        : top(top),
          bottom(bottom),
          thickness(std::max(thickness, std::numeric_limits<Float>::min())),
          g(g),
          albedo(albedo),
          maxDepth(maxDepth),
          nSamples(nSamples) {}

    std::string ToString() const;

    PBRT_CPU_GPU
    void Regularize() {
        top.Regularize();
        bottom.Regularize();
    }

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        BxDFFlags topFlags = top.Flags(), bottomFlags = bottom.Flags();
        CHECK(IsTransmissive(topFlags) ||
              IsTransmissive(bottomFlags));  // otherwise, why bother?

        BxDFFlags flags = BxDFFlags::Reflection;
        if (IsSpecular(topFlags))
            flags = flags | BxDFFlags::Specular;

        if (IsDiffuse(topFlags) || IsDiffuse(bottomFlags) || albedo)
            flags = flags | BxDFFlags::Diffuse;
        else if (IsGlossy(topFlags) || IsGlossy(bottomFlags))
            flags = flags | BxDFFlags::Glossy;

        if (IsTransmissive(topFlags) && IsTransmissive(bottomFlags))
            flags = flags | BxDFFlags::Transmission;

        return flags;
    }

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        SampledSpectrum f(0.);
        // Estimate _LayeredBxDF_ value _f_ using random sampling
        // Set _wo_ and _wi_ for layered BSDF evaluation
        if (twoSided && wo.z < 0) {
            wo = -wo;
            wi = -wi;
        }

        // Determine entrance interface for layered BSDF
        TopOrBottomBxDF<TopBxDF, BottomBxDF> enterInterface;
        bool enteredTop = twoSided || wo.z > 0;
        if (enteredTop)
            enterInterface = &top;
        else
            enterInterface = &bottom;

        // Determine exit interface and exit $z$ for layered BSDF
        TopOrBottomBxDF<TopBxDF, BottomBxDF> exitInterface, nonExitInterface;
        if (SameHemisphere(wo, wi) ^ enteredTop) {
            exitInterface = &bottom;
            nonExitInterface = &top;
        } else {
            exitInterface = &top;
            nonExitInterface = &bottom;
        }
        Float exitZ = (SameHemisphere(wo, wi) ^ enteredTop) ? 0 : thickness;

        // Account for reflection at the entrance interface
        if (SameHemisphere(wo, wi))
            f = nSamples * enterInterface.f(wo, wi, mode);

        // Declare _RNG_ for layered BSDF evaluation
        RNG rng(Hash(GetOptions().seed, wo), Hash(wi));
        auto r = [&rng]() {
            return std::min<Float>(rng.Uniform<Float>(), OneMinusEpsilon);
        };

        for (int s = 0; s < nSamples; ++s) {
            // Sample random walk through layers to estimate BSDF value
            // Sample transmission direction through entrance interface
            Float uc = r();
            pstd::optional<BSDFSample> wos = enterInterface.Sample_f(
                wo, uc, Point2f(r(), r()), mode, BxDFReflTransFlags::Transmission);
            if (!wos || !wos->f || wos->pdf == 0 || wos->wi.z == 0)
                continue;

            // Sample BSDF for virtual light from _wi_
            uc = r();
            pstd::optional<BSDFSample> wis = exitInterface.Sample_f(
                wi, uc, Point2f(r(), r()), !mode, BxDFReflTransFlags::Transmission);
            if (!wis || !wis->f || wis->pdf == 0 || wis->wi.z == 0)
                continue;

            // Declare state for random walk through BSDF layers
            SampledSpectrum beta = wos->f * AbsCosTheta(wos->wi) / wos->pdf;
            Float z = enteredTop ? thickness : 0;
            Vector3f w = wos->wi;
            HGPhaseFunction phase(g);

            for (int depth = 0; depth < maxDepth; ++depth) {
                // Sample next event for layered BSDF evaluation random walk
                PBRT_DBG("beta: %f %f %f %f, w: %f %f %f, f: %f %f %f %f\n", beta[0],
                         beta[1], beta[2], beta[3], w.x, w.y, w.z, f[0], f[1], f[2],
                         f[3]);
                // Possibly terminate layered BSDF random walk with Russian roulette
                if (depth > 3 && beta.MaxComponentValue() < 0.25f) {
                    Float q = std::max<Float>(0, 1 - beta.MaxComponentValue());
                    if (r() < q)
                        break;
                    beta /= 1 - q;
                    PBRT_DBG("After RR with q = %f, beta: %f %f %f %f\n", q, beta[0],
                             beta[1], beta[2], beta[3]);
                }

                // Account for media between layers and possibly scatter
                if (!albedo) {
                    // Advance to next layer boundary and update _beta_ for transmittance
                    z = (z == thickness) ? 0 : thickness;
                    beta *= Tr(thickness, w);

                } else {
                    // Sample medium scattering for layered BSDF evaluation
                    Float sigma_t = 1;
                    Float dz = SampleExponential(r(), sigma_t / std::abs(w.z));
                    Float zp = w.z > 0 ? (z + dz) : (z - dz);
                    DCHECK_RARE(1e-5, z == zp);
                    if (z == zp)
                        continue;
                    if (0 < zp && zp < thickness) {
                        // Handle scattering event in layered BSDF medium
                        // Account for scattering through _exitInterface_ using _wis_
                        Float wt = 1;
                        if (!IsSpecular(exitInterface.Flags()))
                            wt = PowerHeuristic(1, wis->pdf, 1, phase.PDF(-w, -wis->wi));
                        f += beta * albedo * phase.p(-w, -wis->wi) * wt *
                             Tr(zp - exitZ, wis->wi) * wis->f / wis->pdf;

                        // Sample phase function and update layered path state
                        Point2f u{r(), r()};
                        pstd::optional<PhaseFunctionSample> ps = phase.Sample_p(-w, u);
                        if (!ps || ps->pdf == 0 || ps->wi.z == 0)
                            continue;
                        beta *= albedo * ps->p / ps->pdf;
                        w = ps->wi;
                        z = zp;

                        // Possibly account for scattering through _exitInterface_
                        if (((z < exitZ && w.z > 0) || (z > exitZ && w.z < 0)) &&
                            !IsSpecular(exitInterface.Flags())) {
                            // Account for scattering through _exitInterface_
                            SampledSpectrum fExit = exitInterface.f(-w, wi, mode);
                            if (fExit) {
                                Float exitPDF = exitInterface.PDF(
                                    -w, wi, mode, BxDFReflTransFlags::Transmission);
                                Float wt = PowerHeuristic(1, ps->pdf, 1, exitPDF);
                                f += beta * Tr(zp - exitZ, ps->wi) * fExit * wt;
                            }
                        }

                        continue;
                    }
                    z = Clamp(zp, 0, thickness);
                }

                // Account for scattering at appropriate interface
                if (z == exitZ) {
                    // Account for reflection at _exitInterface_
                    Float uc = r();
                    pstd::optional<BSDFSample> bs = exitInterface.Sample_f(
                        -w, uc, Point2f(r(), r()), mode, BxDFReflTransFlags::Reflection);
                    if (!bs || !bs->f || bs->pdf == 0 || bs->wi.z == 0)
                        break;
                    beta *= bs->f * AbsCosTheta(bs->wi) / bs->pdf;
                    w = bs->wi;

                } else {
                    // Account for scattering at _nonExitInterface_
                    if (!IsSpecular(nonExitInterface.Flags())) {
                        // Add NEE contribution along presampled _wis_ direction
                        Float wt = 1;
                        if (!IsSpecular(exitInterface.Flags()))
                            wt = PowerHeuristic(1, wis->pdf, 1,
                                                nonExitInterface.PDF(-w, -wis->wi, mode));
                        f += beta * nonExitInterface.f(-w, -wis->wi, mode) *
                             AbsCosTheta(wis->wi) * wt * Tr(thickness, wis->wi) * wis->f /
                             wis->pdf;
                    }
                    // Sample new direction using BSDF at _nonExitInterface_
                    Float uc = r();
                    Point2f u(r(), r());
                    pstd::optional<BSDFSample> bs = nonExitInterface.Sample_f(
                        -w, uc, u, mode, BxDFReflTransFlags::Reflection);
                    if (!bs || !bs->f || bs->pdf == 0 || bs->wi.z == 0)
                        break;
                    beta *= bs->f * AbsCosTheta(bs->wi) / bs->pdf;
                    w = bs->wi;

                    if (!IsSpecular(exitInterface.Flags())) {
                        // Add NEE contribution along direction from BSDF sample
                        SampledSpectrum fExit = exitInterface.f(-w, wi, mode);
                        if (fExit) {
                            Float wt = 1;
                            if (!IsSpecular(nonExitInterface.Flags())) {
                                Float exitPDF = exitInterface.PDF(
                                    -w, wi, mode, BxDFReflTransFlags::Transmission);
                                wt = PowerHeuristic(1, bs->pdf, 1, exitPDF);
                            }
                            f += beta * Tr(thickness, bs->wi) * fExit * wt;
                        }
                    }
                }
            }
        }

        return f / nSamples;
    }

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(
        Vector3f wo, Float uc, Point2f u, TransportMode mode,
        BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        CHECK(sampleFlags == BxDFReflTransFlags::All);  // for now
        // Set _wo_ for layered BSDF sampling
        bool flipWi = false;
        if (twoSided && wo.z < 0) {
            wo = -wo;
            flipWi = true;
        }

        // Sample BSDF at entrance interface to get initial direction _w_
        bool enteredTop = twoSided || wo.z > 0;
        pstd::optional<BSDFSample> bs =
            enteredTop ? top.Sample_f(wo, uc, u, mode) : bottom.Sample_f(wo, uc, u, mode);
        if (!bs || !bs->f || bs->pdf == 0 || bs->wi.z == 0)
            return {};
        if (bs->IsReflection()) {
            if (flipWi)
                bs->wi = -bs->wi;
            bs->pdfIsProportional = true;
            return bs;
        }
        Vector3f w = bs->wi;
        bool specularPath = bs->IsSpecular();

        // Declare _RNG_ for layered BSDF sampling
        RNG rng(Hash(GetOptions().seed, wo), Hash(uc, u));
        auto r = [&rng]() {
            return std::min<Float>(rng.Uniform<Float>(), OneMinusEpsilon);
        };

        // Declare common variables for layered BSDF sampling
        SampledSpectrum f = bs->f * AbsCosTheta(bs->wi);
        Float pdf = bs->pdf;
        Float z = enteredTop ? thickness : 0;
        HGPhaseFunction phase(g);

        for (int depth = 0; depth < maxDepth; ++depth) {
            // Follow random walk through layers to sample layered BSDF
            // Possibly terminate layered BSDF sampling with Russian Roulette
            Float rrBeta = f.MaxComponentValue() / pdf;
            if (depth > 3 && rrBeta < 0.25f) {
                Float q = std::max<Float>(0, 1 - rrBeta);
                if (r() < q)
                    return {};
                pdf *= 1 - q;
            }
            if (w.z == 0)
                return {};

            if (albedo) {
                // Sample potential scattering event in layered medium
                Float sigma_t = 1;
                Float dz = SampleExponential(r(), sigma_t / AbsCosTheta(w));
                Float zp = w.z > 0 ? (z + dz) : (z - dz);
                CHECK_RARE(1e-5, zp == z);
                if (zp == z)
                    return {};
                if (0 < zp && zp < thickness) {
                    // Update path state for valid scattering event between interfaces
                    pstd::optional<PhaseFunctionSample> ps =
                        phase.Sample_p(-w, Point2f(r(), r()));
                    if (!ps || ps->pdf == 0 || ps->wi.z == 0)
                        return {};
                    f *= albedo * ps->p;
                    pdf *= ps->pdf;
                    specularPath = false;
                    w = ps->wi;
                    z = zp;

                    continue;
                }
                z = Clamp(zp, 0, thickness);
                if (z == 0)
                    DCHECK_LT(w.z, 0);
                else
                    DCHECK_GT(w.z, 0);

            } else {
                // Advance to the other layer interface
                z = (z == thickness) ? 0 : thickness;
                f *= Tr(thickness, w);
            }
            // Initialize _interface_ for current interface surface
#ifdef interface  // That's enough out of you, Windows.
#undef interface
#endif
            TopOrBottomBxDF<TopBxDF, BottomBxDF> interface;
            if (z == 0)
                interface = &bottom;
            else
                interface = &top;

            // Sample interface BSDF to determine new path direction
            Float uc = r();
            Point2f u(r(), r());
            pstd::optional<BSDFSample> bs = interface.Sample_f(-w, uc, u, mode);
            if (!bs || !bs->f || bs->pdf == 0 || bs->wi.z == 0)
                return {};
            f *= bs->f;
            pdf *= bs->pdf;
            specularPath &= bs->IsSpecular();
            w = bs->wi;

            // Return _BSDFSample_ if path has left the layers
            if (bs->IsTransmission()) {
                BxDFFlags flags = SameHemisphere(wo, w) ? BxDFFlags::Reflection
                                                        : BxDFFlags::Transmission;
                flags |= specularPath ? BxDFFlags::Specular : BxDFFlags::Glossy;
                if (flipWi)
                    w = -w;
                return BSDFSample(f, w, pdf, flags, 1.f, true);
            }

            // Scale _f_ by cosine term after scattering at the interface
            f *= AbsCosTheta(bs->wi);
        }
        return {};
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags = BxDFReflTransFlags::All) const {
        CHECK(sampleFlags == BxDFReflTransFlags::All);  // for now
        // Set _wo_ and _wi_ for layered BSDF evaluation
        if (twoSided && wo.z < 0) {
            wo = -wo;
            wi = -wi;
        }

        // Declare _RNG_ for layered PDF evaluation
        RNG rng(Hash(GetOptions().seed, wi), Hash(wo));
        auto r = [&rng]() {
            return std::min<Float>(rng.Uniform<Float>(), OneMinusEpsilon);
        };

        // Update _pdfSum_ for reflection at the entrance layer
        bool enteredTop = twoSided || wo.z > 0;
        Float pdfSum = 0;
        if (SameHemisphere(wo, wi)) {
            auto reflFlag = BxDFReflTransFlags::Reflection;
            pdfSum += enteredTop ? nSamples * top.PDF(wo, wi, mode, reflFlag)
                                 : nSamples * bottom.PDF(wo, wi, mode, reflFlag);
        }

        for (int s = 0; s < nSamples; ++s) {
            // Evaluate layered BSDF PDF sample
            if (SameHemisphere(wo, wi)) {
                // Evaluate TRT term for PDF estimate
                TopOrBottomBxDF<TopBxDF, BottomBxDF> rInterface, tInterface;
                if (enteredTop) {
                    rInterface = &bottom;
                    tInterface = &top;
                } else {
                    rInterface = &top;
                    tInterface = &bottom;
                }
                // Sample _tInterface_ to get direction into the layers
                auto trans = BxDFReflTransFlags::Transmission;
                pstd::optional<BSDFSample> wos, wis;
                wos = tInterface.Sample_f(wo, r(), {r(), r()}, mode, trans);
                wis = tInterface.Sample_f(wi, r(), {r(), r()}, !mode, trans);

                // Update _pdfSum_ accounting for TRT scattering events
                if (wos && wos->f && wos->pdf > 0 && wis && wis->f && wis->pdf > 0) {
                    if (!IsNonSpecular(tInterface.Flags()))
                        pdfSum += rInterface.PDF(-wos->wi, -wis->wi, mode);
                    else {
                        // Use multiple importance sampling to estimate PDF product
                        pstd::optional<BSDFSample> rs =
                            rInterface.Sample_f(-wos->wi, r(), {r(), r()}, mode);
                        if (rs && rs->f && rs->pdf > 0) {
                            if (!IsNonSpecular(rInterface.Flags()))
                                pdfSum += tInterface.PDF(-rs->wi, wi, mode);
                            else {
                                // Compute MIS-weighted estimate of Equation
                                // (\ref{eq:pdf-triple-canceled-one})
                                Float rPDF = rInterface.PDF(-wos->wi, -wis->wi, mode);
                                Float wt = PowerHeuristic(1, wis->pdf, 1, rPDF);
                                pdfSum += wt * rPDF;

                                Float tPDF = tInterface.PDF(-rs->wi, wi, mode);
                                wt = PowerHeuristic(1, rs->pdf, 1, tPDF);
                                pdfSum += wt * tPDF;
                            }
                        }
                    }
                }

            } else {
                // Evaluate TT term for PDF estimate
                TopOrBottomBxDF<TopBxDF, BottomBxDF> toInterface, tiInterface;
                if (enteredTop) {
                    toInterface = &top;
                    tiInterface = &bottom;
                } else {
                    toInterface = &bottom;
                    tiInterface = &top;
                }

                Float uc = r();
                Point2f u(r(), r());
                pstd::optional<BSDFSample> wos = toInterface.Sample_f(wo, uc, u, mode);
                if (!wos || !wos->f || wos->pdf == 0 || wos->wi.z == 0 ||
                    wos->IsReflection())
                    continue;

                uc = r();
                u = Point2f(r(), r());
                pstd::optional<BSDFSample> wis = tiInterface.Sample_f(wi, uc, u, !mode);
                if (!wis || !wis->f || wis->pdf == 0 || wis->wi.z == 0 ||
                    wis->IsReflection())
                    continue;

                if (IsSpecular(toInterface.Flags()))
                    pdfSum += tiInterface.PDF(-wos->wi, wi, mode);
                else if (IsSpecular(tiInterface.Flags()))
                    pdfSum += toInterface.PDF(wo, -wis->wi, mode);
                else
                    pdfSum += (toInterface.PDF(wo, -wis->wi, mode) +
                               tiInterface.PDF(-wos->wi, wi, mode)) /
                              2;
            }
        }
        // Return mixture of PDF estimate and constant PDF
        return Lerp(0.9f, 1 / (4 * Pi), pdfSum / nSamples);
    }

  private:
    // LayeredBxDF Private Methods
    PBRT_CPU_GPU
    static Float Tr(Float dz, Vector3f w) {
        if (std::abs(dz) <= std::numeric_limits<Float>::min())
            return 1;
        return FastExp(-std::abs(dz / w.z));
    }

    // LayeredBxDF Private Members
    TopBxDF top;
    BottomBxDF bottom;
    Float thickness, g;
    SampledSpectrum albedo;
    int maxDepth, nSamples;
};

// CoatedDiffuseBxDF Definition
class CoatedDiffuseBxDF : public LayeredBxDF<DielectricBxDF, DiffuseBxDF, true> {
  public:
    // CoatedDiffuseBxDF Public Methods
    using LayeredBxDF::LayeredBxDF;
    PBRT_CPU_GPU
    static constexpr const char *Name() { return "CoatedDiffuseBxDF"; }
};

// CoatedConductorBxDF Definition
class CoatedConductorBxDF : public LayeredBxDF<DielectricBxDF, ConductorBxDF, true> {
  public:
    // CoatedConductorBxDF Public Methods
    PBRT_CPU_GPU
    static constexpr const char *Name() { return "CoatedConductorBxDF"; }
    using LayeredBxDF::LayeredBxDF;
};

// HairBxDF Definition
class HairBxDF {
  public:
    // HairBxDF Public Methods
    HairBxDF() = default;
    PBRT_CPU_GPU
    HairBxDF(Float h, Float eta, const SampledSpectrum &sigma_a, Float beta_m,
             Float beta_n, Float alpha);
    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const;
    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(Vector3f wo, Float uc, Point2f u,
                                        TransportMode mode,
                                        BxDFReflTransFlags sampleFlags) const;
    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const;

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "HairBxDF"; }
    std::string ToString() const;

    PBRT_CPU_GPU
    BxDFFlags Flags() const { return BxDFFlags::GlossyReflection; }

    PBRT_CPU_GPU
    static RGBUnboundedSpectrum SigmaAFromConcentration(Float ce, Float cp);
    PBRT_CPU_GPU
    static SampledSpectrum SigmaAFromReflectance(const SampledSpectrum &c, Float beta_n,
                                                 const SampledWavelengths &lambda);

  private:
    // HairBxDF Constants
    static constexpr int pMax = 3;

    // HairBxDF Private Methods
    PBRT_CPU_GPU static Float Mp(Float cosTheta_i, Float cosTheta_o, Float sinTheta_i,
                                 Float sinTheta_o, Float v) {
        Float a = cosTheta_i * cosTheta_o / v, b = sinTheta_i * sinTheta_o / v;
        Float mp = (v <= .1)
                       ? (FastExp(LogI0(a) - b - 1 / v + 0.6931f + std::log(1 / (2 * v))))
                       : (FastExp(-b) * I0(a)) / (std::sinh(1 / v) * 2 * v);
        DCHECK(!IsInf(mp) && !IsNaN(mp));
        return mp;
    }

    PBRT_CPU_GPU static pstd::array<SampledSpectrum, pMax + 1> Ap(Float cosTheta_o,
                                                                  Float eta, Float h,
                                                                  SampledSpectrum T) {
        pstd::array<SampledSpectrum, pMax + 1> ap;
        // Compute $p=0$ attenuation at initial cylinder intersection
        Float cosGamma_o = SafeSqrt(1 - Sqr(h));
        Float cosTheta = cosTheta_o * cosGamma_o;
        Float f = FrDielectric(cosTheta, eta);
        ap[0] = SampledSpectrum(f);

        // Compute $p=1$ attenuation term
        ap[1] = Sqr(1 - f) * T;

        // Compute attenuation terms up to $p=_pMax_$
        for (int p = 2; p < pMax; ++p)
            ap[p] = ap[p - 1] * T * f;

        // Compute attenuation term accounting for remaining orders of scattering
        if (1 - T * f)
            ap[pMax] = ap[pMax - 1] * f * T / (1 - T * f);

        return ap;
    }

    PBRT_CPU_GPU static inline Float Phi(int p, Float gamma_o, Float gamma_t) {
        return 2 * p * gamma_t - 2 * gamma_o + p * Pi;
    }

    PBRT_CPU_GPU static inline Float Np(Float phi, int p, Float s, Float gamma_o,
                                        Float gamma_t) {
        Float dphi = phi - Phi(p, gamma_o, gamma_t);
        // Remap _dphi_ to $[-\pi,\pi]$
        while (dphi > Pi)
            dphi -= 2 * Pi;
        while (dphi < -Pi)
            dphi += 2 * Pi;

        return TrimmedLogistic(dphi, s, -Pi, Pi);
    }

    PBRT_CPU_GPU
    pstd::array<Float, pMax + 1> ApPDF(Float cosTheta_o) const;

    // HairBxDF Private Members
    Float h, eta;
    SampledSpectrum sigma_a;
    Float beta_m, beta_n;
    Float v[pMax + 1];
    Float s;
    Float sin2kAlpha[pMax], cos2kAlpha[pMax];
};

// MeasuredBxDF Definition
class MeasuredBxDF {
  public:
    // MeasuredBxDF Public Methods
    MeasuredBxDF() = default;
    PBRT_CPU_GPU
    MeasuredBxDF(const MeasuredBxDFData *brdf, const SampledWavelengths &lambda)
        : brdf(brdf), lambda(lambda) {}

    static MeasuredBxDFData *BRDFDataFromFile(const std::string &filename,
                                              Allocator alloc);

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const;

    PBRT_CPU_GPU
    pstd::optional<BSDFSample> Sample_f(Vector3f wo, Float uc, Point2f u,
                                        TransportMode mode,
                                        BxDFReflTransFlags sampleFlags) const;
    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const;

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "MeasuredBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    BxDFFlags Flags() const { return (BxDFFlags::Reflection | BxDFFlags::Glossy); }

  private:
    // MeasuredBxDF Private Methods
    PBRT_CPU_GPU
    static Float theta2u(Float theta) { return std::sqrt(theta * (2 / Pi)); }
    PBRT_CPU_GPU
    static Float phi2u(Float phi) { return phi * (1 / (2 * Pi)) + .5f; }

    PBRT_CPU_GPU
    static Float u2theta(Float u) { return Sqr(u) * (Pi / 2.f); }
    PBRT_CPU_GPU
    static Float u2phi(Float u) { return (2.f * u - 1.f) * Pi; }

    // MeasuredBxDF Private Members
    const MeasuredBxDFData *brdf;
    SampledWavelengths lambda;
};

// NormalizedFresnelBxDF Definition
class NormalizedFresnelBxDF {
  public:
    // NormalizedFresnelBxDF Public Methods
    NormalizedFresnelBxDF() = default;
    PBRT_CPU_GPU
    NormalizedFresnelBxDF(Float eta) : eta(eta) {}

    PBRT_CPU_GPU
    BSDFSample Sample_f(Vector3f wo, Float uc, Point2f u, TransportMode mode,
                        BxDFReflTransFlags sampleFlags) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return {};

        // Cosine-sample the hemisphere, flipping the direction if necessary
        Vector3f wi = SampleCosineHemisphere(u);
        if (wo.z < 0)
            wi.z *= -1;
        return BSDFSample(f(wo, wi, mode), wi, PDF(wo, wi, mode, sampleFlags),
                          BxDFFlags::DiffuseReflection);
    }

    PBRT_CPU_GPU
    Float PDF(Vector3f wo, Vector3f wi, TransportMode mode,
              BxDFReflTransFlags sampleFlags) const {
        if (!(sampleFlags & BxDFReflTransFlags::Reflection))
            return 0;
        return SameHemisphere(wo, wi) ? AbsCosTheta(wi) * InvPi : 0;
    }

    PBRT_CPU_GPU
    void Regularize() {}

    PBRT_CPU_GPU
    static constexpr const char *Name() { return "NormalizedFresnelBxDF"; }

    std::string ToString() const;

    PBRT_CPU_GPU
    BxDFFlags Flags() const {
        return BxDFFlags(BxDFFlags::Reflection | BxDFFlags::Diffuse);
    }

    PBRT_CPU_GPU
    SampledSpectrum f(Vector3f wo, Vector3f wi, TransportMode mode) const {
        if (!SameHemisphere(wo, wi))
            return SampledSpectrum(0.f);
        // Compute $\Sw$ factor for BSSRDF value
        Float c = 1 - 2 * FresnelMoment1(1 / eta);
        SampledSpectrum f((1 - FrDielectric(CosTheta(wi), eta)) / (c * Pi));

        // Update BSSRDF transmission term to account for adjoint light transport
        if (mode == TransportMode::Radiance)
            f *= Sqr(eta);

        return f;
    }

  private:
    Float eta;
};

inline SampledSpectrum BxDF::f(Vector3f wo, Vector3f wi, TransportMode mode) const {
    auto f = [&](auto ptr) -> SampledSpectrum { return ptr->f(wo, wi, mode); };
    return Dispatch(f);
}

inline pstd::optional<BSDFSample> BxDF::Sample_f(Vector3f wo, Float uc, Point2f u,
                                                 TransportMode mode,
                                                 BxDFReflTransFlags sampleFlags) const {
    auto sample_f = [&](auto ptr) -> pstd::optional<BSDFSample> {
        return ptr->Sample_f(wo, uc, u, mode, sampleFlags);
    };
    return Dispatch(sample_f);
}

inline Float BxDF::PDF(Vector3f wo, Vector3f wi, TransportMode mode,
                       BxDFReflTransFlags sampleFlags) const {
    auto pdf = [&](auto ptr) { return ptr->PDF(wo, wi, mode, sampleFlags); };
    return Dispatch(pdf);
}

inline BxDFFlags BxDF::Flags() const {
    auto flags = [&](auto ptr) { return ptr->Flags(); };
    return Dispatch(flags);
}

inline void BxDF::Regularize() {
    auto regularize = [&](auto ptr) { ptr->Regularize(); };
    return Dispatch(regularize);
}

extern template class LayeredBxDF<DielectricBxDF, DiffuseBxDF, true>;
extern template class LayeredBxDF<DielectricBxDF, ConductorBxDF, true>;

}  // namespace pbrt

#endif  // PBRT_BXDFS_H
