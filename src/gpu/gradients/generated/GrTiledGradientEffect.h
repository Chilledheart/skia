/*
 * Copyright 2018 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/**************************************************************************************************
 *** This file was autogenerated from GrTiledGradientEffect.fp; do not modify.
 **************************************************************************************************/
#ifndef GrTiledGradientEffect_DEFINED
#define GrTiledGradientEffect_DEFINED

#include "include/core/SkM44.h"
#include "include/core/SkTypes.h"

#include "src/gpu/GrFragmentProcessor.h"

class GrTiledGradientEffect : public GrFragmentProcessor {
public:
    static std::unique_ptr<GrFragmentProcessor> Make(
            std::unique_ptr<GrFragmentProcessor> colorizer,
            std::unique_ptr<GrFragmentProcessor> gradLayout,
            bool mirror,
            bool makePremul,
            bool colorsAreOpaque) {
        bool layoutPreservesOpacity = gradLayout->preservesOpaqueInput();
        return std::unique_ptr<GrFragmentProcessor>(
                new GrTiledGradientEffect(std::move(colorizer), std::move(gradLayout), mirror,
                                          makePremul, colorsAreOpaque, layoutPreservesOpacity));
    }
    GrTiledGradientEffect(const GrTiledGradientEffect& src);
    std::unique_ptr<GrFragmentProcessor> clone() const override;
    const char* name() const override { return "TiledGradientEffect"; }
    bool mirror;
    bool makePremul;
    bool colorsAreOpaque;
    bool layoutPreservesOpacity;

private:
    GrTiledGradientEffect(std::unique_ptr<GrFragmentProcessor> colorizer,
                          std::unique_ptr<GrFragmentProcessor> gradLayout,
                          bool mirror,
                          bool makePremul,
                          bool colorsAreOpaque,
                          bool layoutPreservesOpacity)
            : INHERITED(kGrTiledGradientEffect_ClassID,
                        (OptimizationFlags)kCompatibleWithCoverageAsAlpha_OptimizationFlag |
                                (colorsAreOpaque && layoutPreservesOpacity
                                         ? kPreservesOpaqueInput_OptimizationFlag
                                         : kNone_OptimizationFlags))
            , mirror(mirror)
            , makePremul(makePremul)
            , colorsAreOpaque(colorsAreOpaque)
            , layoutPreservesOpacity(layoutPreservesOpacity) {
        this->registerChild(std::move(colorizer), SkSL::SampleUsage::Explicit());
        this->registerChild(std::move(gradLayout), SkSL::SampleUsage::PassThrough());
    }
    std::unique_ptr<GrGLSLFragmentProcessor> onMakeProgramImpl() const override;
    void onGetGLSLProcessorKey(const GrShaderCaps&, GrProcessorKeyBuilder*) const override;
    bool onIsEqual(const GrFragmentProcessor&) const override;
#if GR_TEST_UTILS
    SkString onDumpInfo() const override;
#endif
    GR_DECLARE_FRAGMENT_PROCESSOR_TEST
    using INHERITED = GrFragmentProcessor;
};
#endif
