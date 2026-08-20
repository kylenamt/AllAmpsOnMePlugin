// Standalone self-test for the AllAmpsOnMe morph engine (no JUCE).
//
// Verifies the M2-critical path end to end:
//   * bundle parse -> architecture matches the A2 schedule
//   * FiLM fold produces exactly the weight count NAM's WaveNet consumes
//   * fold is deterministic and embedding-sensitive (morph actually changes weights)
//   * fold is affine in the embedding -- the identity MorphEngine's corner-blend
//     rests on -- and the engine's blend agrees with folding a blended embedding
//   * the delta arch folds its low-rank residual onto the right weight slices,
//     and sibling archs are rejected instead of being reinterpreted
//   * get_dsp() builds a generic WaveNet from the folded stream (no throw)
//   * rendering audio yields finite output, and morphing changes the output
//   * the cab-IR FFT convolution matches a direct sum and adds no latency
//
// Build/run is driven by tools/run_selftest.sh (system Eigen + local NAM clone).

#include "FftConvolver.h"
#include "MorphEngine.h"
#include "MorphModel.h"
#include "Resampler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
int g_failures = 0;

void check(bool cond, const char* what)
{
    std::printf("  [%s] %s\n", cond ? "PASS" : "FAIL", what);
    if (!cond)
        ++g_failures;
}

std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool allFinite(const std::vector<NAM_SAMPLE>& v)
{
    for (NAM_SAMPLE x : v)
        if (!std::isfinite(static_cast<double>(x)))
            return false;
    return true;
}

// Deterministic synthetic embedding around `base`. The shipped bundle carries a
// single profile ("Table mean") -- the real catalogue lives in a separate file --
// so the morph tests build their own distinct points instead of silently skipping
// the moment profiles.size() < 2, which is what they used to do.
std::vector<float> perturbEmbedding(const std::vector<float>& base, unsigned seed, float amount)
{
    std::vector<float> e = base;
    unsigned x = seed * 2654435761u + 1u;
    for (float& v : e)
    {
        x = x * 1664525u + 1013904223u;
        v += amount * (static_cast<float>((x >> 8) & 0xFFFFu) / 32768.0f - 1.0f);
    }
    return e;
}

double rms(const std::vector<NAM_SAMPLE>& v)
{
    double s = 0.0;
    for (NAM_SAMPLE x : v)
        s += static_cast<double>(x) * static_cast<double>(x);
    return v.empty() ? 0.0 : std::sqrt(s / static_cast<double>(v.size()));
}

// --- synthetic delta bundle ---------------------------------------------------
// No delta_wavenet_a2 model exists yet, so the delta fold is checked against a
// hand-built miniature whose answer is known in closed form. readTensor() accepts
// plain JSON number arrays as well as base64, which keeps this readable.

std::vector<float> lcgFill(std::size_t n, unsigned seed)
{
    std::vector<float> v(n);
    unsigned x = seed * 2654435761u + 12345u;
    for (std::size_t i = 0; i < n; ++i)
    {
        x = x * 1664525u + 1013904223u;
        v[i] = static_cast<float>((x >> 8) & 0xFFFFu) / 32768.0f - 1.0f; // [-1,1)
    }
    return v;
}

std::string jarr(const std::vector<float>& v)
{
    std::string s = "[";
    char buf[48];
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        // %.9g round-trips float32 exactly, so the known-answer compare is exact.
        std::snprintf(buf, sizeof buf, "%.9g", static_cast<double>(v[i]));
        if (i)
            s += ',';
        s += buf;
    }
    return s + "]";
}

struct TinyDelta
{
    static constexpr int C = 4;
    static constexpr int E = 8;
    static constexpr int R = 2;
    static constexpr int headK = 3;
    static constexpr float headScale = 0.5f;
    int ks[2] = {2, 3};
    int dil[2] = {1, 2};

    struct Layer
    {
        std::vector<float> convW, convB, mixinW, coeffW, coeffB, basis, x1W, x1B;
        std::size_t nW = 0, nOut = 0;
    };

    std::vector<float> rechannel, headW, headB;
    Layer layers[2];

    TinyDelta()
    {
        rechannel = lcgFill(C, 1u);
        headW = lcgFill(static_cast<std::size_t>(C) * headK, 2u);
        headB = lcgFill(1, 3u);

        for (int i = 0; i < 2; ++i)
        {
            Layer& L = layers[i];
            const std::size_t k = static_cast<std::size_t>(ks[i]);
            L.nW = static_cast<std::size_t>(C) * C * k;
            L.nOut = L.nW + 2 * static_cast<std::size_t>(C);

            L.convW = lcgFill(L.nW, 10u + static_cast<unsigned>(i));
            L.convB = lcgFill(C, 20u + static_cast<unsigned>(i));
            L.mixinW = lcgFill(C, 30u + static_cast<unsigned>(i));
            L.x1W = lcgFill(static_cast<std::size_t>(C) * C, 40u + static_cast<unsigned>(i));
            L.x1B = lcgFill(C, 50u + static_cast<unsigned>(i));

            // coeff row 0 reads embedding component 0 and nothing else; row 1 is
            // dead. So coeff = (0.5 + e[0], 0) and only basis row 0 may ever show
            // up in the fold -- row 1 is filled with a value that would be obvious.
            L.coeffW.assign(static_cast<std::size_t>(R) * E, 0.0f);
            L.coeffW[0] = 1.0f;
            L.coeffB = {0.5f, 0.0f};

            L.basis = lcgFill(L.nOut, 60u + static_cast<unsigned>(i));
            std::vector<float> dead(L.nOut, 9.0f);
            L.basis.insert(L.basis.end(), dead.begin(), dead.end());
        }
    }

    // `archType` and `rank` are overridable so the parser's rejection paths can be
    // exercised with the same generator.
    std::string json(const char* archType = "delta_wavenet_a2", int rank = R) const
    {
        std::string s = "{\"aaom_bundle\":1,\"sample_rate\":48000,\"arch\":{";
        s += "\"type\":\"" + std::string(archType) + "\",";
        s += "\"channels\":" + std::to_string(C) + ",";
        s += "\"embedding_dim\":" + std::to_string(E) + ",";
        s += "\"delta_rank\":" + std::to_string(rank) + ",";
        s += "\"head_kernel\":" + std::to_string(headK) + ",";
        s += "\"head_scale\":0.5,";
        s += "\"kernel_sizes\":[" + std::to_string(ks[0]) + "," + std::to_string(ks[1]) + "],";
        s += "\"dilations\":[" + std::to_string(dil[0]) + "," + std::to_string(dil[1]) + "]},";
        s += "\"weights\":{\"rechannel_w\":" + jarr(rechannel) + ",\"layers\":[";
        for (int i = 0; i < 2; ++i)
        {
            const Layer& L = layers[i];
            if (i)
                s += ',';
            s += "{\"conv_w\":" + jarr(L.convW);
            s += ",\"conv_b\":" + jarr(L.convB);
            s += ",\"mixin_w\":" + jarr(L.mixinW);
            s += ",\"delta_coeff_w\":" + jarr(L.coeffW);
            s += ",\"delta_coeff_b\":" + jarr(L.coeffB);
            s += ",\"delta_basis\":" + jarr(L.basis);
            s += ",\"x1_w\":" + jarr(L.x1W);
            s += ",\"x1_b\":" + jarr(L.x1B) + "}";
        }
        s += "],\"head_w\":" + jarr(headW) + ",\"head_b\":" + jarr(headB) + "},";
        s += "\"profiles\":[{\"name\":\"zero\",\"embedding\":" + jarr(std::vector<float>(E, 0.0f)) + "}]}";
        return s;
    }

    // The stream MorphModel::foldEmbedding must produce, built from the same
    // numbers by the definition rather than by the implementation.
    std::vector<float> expectedFold(const std::vector<float>& e) const
    {
        std::vector<float> out(rechannel);
        for (int i = 0; i < 2; ++i)
        {
            const Layer& L = layers[i];
            const float c0 = L.coeffB[0] + e[0]; // row 0 is one-hot on e[0]
            const float* b0 = L.basis.data();    // row 1 is multiplied by coeff[1] == 0

            for (std::size_t t = 0; t < L.nW; ++t)
                out.push_back(L.convW[t] + c0 * b0[t]);
            for (int c = 0; c < C; ++c)
                out.push_back(L.convB[static_cast<std::size_t>(c)] + c0 * b0[L.nW + static_cast<std::size_t>(c)]);
            for (int c = 0; c < C; ++c)
                out.push_back(L.mixinW[static_cast<std::size_t>(c)]
                              + c0 * b0[L.nW + static_cast<std::size_t>(C + c)]);
            out.insert(out.end(), L.x1W.begin(), L.x1W.end());
            out.insert(out.end(), L.x1B.begin(), L.x1B.end());
        }
        out.insert(out.end(), headW.begin(), headW.end());
        out.insert(out.end(), headB.begin(), headB.end());
        out.push_back(headScale);
        return out;
    }
};
} // namespace

int main(int argc, char** argv)
{
    const std::string bundlePath = argc > 1 ? argv[1] : "projects/AAOM/assets/bundle.json";
    std::printf("AAOM self-test, bundle = %s\n", bundlePath.c_str());

    std::unique_ptr<aaom::MorphModel> model;
    try
    {
        model = aaom::MorphModel::fromBundleJson(readFile(bundlePath));
    }
    catch (const std::exception& e)
    {
        std::printf("  [FAIL] bundle parse threw: %s\n", e.what());
        return 1;
    }

    std::printf("arch: C=%d E=%d layers=%d headK=%d sr=%.0f run=%s\n", model->channels(), model->embeddingDim(),
                model->numLayers(), model->headKernel(), model->bundleSampleRate(), model->runSha8().c_str());

    check(model->channels() == 8, "channels == 8");
    check(model->numLayers() == 23, "23 layers");
    check(model->headKernel() == 16, "head kernel == 16");
    check(model->profiles().size() >= 1, "at least one profile");
    // embedding_dim is per-model (the exporter/make_bundle.py --embed varies it),
    // so assert the invariant that actually matters rather than one run's value:
    // it must be positive and every profile must agree with it.
    check(model->embeddingDim() > 0, "embedding_dim > 0");
    check(!model->profiles().empty()
              && static_cast<int>(model->profiles()[0].embedding.size()) == model->embeddingDim(),
          "profile embedding length == embedding_dim");

    // --- analytic weight-count cross-check ---------------------------------
    // C(rechannel) + sum_i(C*C*k_i + C + C + C*C + C) + C*headK + head_b(1) + head_scale(1)
    const int C = model->channels();
    const int hk = model->headKernel();
    // Recompute k schedule from the A2 layout used by the generator.
    std::vector<int> ks;
    for (int i = 0; i < 14; ++i) ks.push_back(6);
    ks.push_back(15); ks.push_back(15);
    for (int i = 0; i < 7; ++i) ks.push_back(6);
    std::size_t expected = static_cast<std::size_t>(C);
    for (int k : ks)
        expected += static_cast<std::size_t>(C) * C * k + C + C + static_cast<std::size_t>(C) * C + C;
    expected += static_cast<std::size_t>(C) * hk + 1 + 1;
    std::printf("nam weight count: got=%zu expected=%zu\n", model->namWeightCount(), expected);
    check(model->namWeightCount() == expected, "namWeightCount matches analytic A2 count");

    // --- fold: determinism + sensitivity -----------------------------------
    const auto& profiles = model->profiles();
    const std::vector<float>& base = profiles.front().embedding;
    // A2_EMBEDDING_STD is 0.1, so perturbations of that size land in the same
    // neighbourhood the trained table occupies.
    const std::vector<float> eA = perturbEmbedding(base, 1u, 0.1f);

    std::vector<float> w0, w0b, wA;
    model->foldEmbedding(base, w0);
    model->foldEmbedding(base, w0b);
    check(w0.size() == model->namWeightCount(), "folded stream length == namWeightCount");
    check(w0 == w0b, "fold is deterministic");

    model->foldEmbedding(eA, wA);
    check(wA.size() == w0.size() && wA != w0, "different embedding -> different weights");

    // MorphEngine blends *folded corners* rather than folding a blended embedding.
    // That is only legal because fold() is affine in e, so check the identity it
    // rests on directly:  fold(a*e0 + (1-a)*e1) == a*fold(e0) + (1-a)*fold(e1)
    {
        const float alpha = 0.37f;
        std::vector<float> mix(base.size());
        for (std::size_t i = 0; i < mix.size(); ++i)
            mix[i] = alpha * base[i] + (1.0f - alpha) * eA[i];

        std::vector<float> wMix;
        model->foldEmbedding(mix, wMix);

        double maxErr = 0.0, maxMag = 0.0;
        for (std::size_t i = 0; i < wMix.size(); ++i)
        {
            const double blended =
                alpha * static_cast<double>(w0[i]) + (1.0 - alpha) * static_cast<double>(wA[i]);
            maxErr = std::max(maxErr, std::fabs(blended - static_cast<double>(wMix[i])));
            maxMag = std::max(maxMag, std::fabs(static_cast<double>(wMix[i])));
        }
        std::printf("  affine fold: max|blend - fold| = %.3e over max|w| = %.3f\n", maxErr, maxMag);
        check(maxErr < 1.0e-5 * std::max(1.0, maxMag), "fold is affine in the embedding");
    }

    // --- build NAM DSP + render --------------------------------------------
    const double sr = 48000.0;
    const int block = 64;
    try
    {
        model->buildDsp(base, sr, block);
    }
    catch (const std::exception& e)
    {
        std::printf("  [FAIL] buildDsp threw: %s\n", e.what());
        return 1;
    }
    check(model->hasDsp(), "DSP constructed");

    // 1 kHz sine, processed in blocks (also exercises the internal ring buffer).
    const int total = block * 200; // ~0.27 s
    std::vector<NAM_SAMPLE> in(total), out0(total), out1(total);
    for (int n = 0; n < total; ++n)
        in[n] = static_cast<NAM_SAMPLE>(0.2 * std::sin(2.0 * M_PI * 1000.0 * n / sr));

    for (int n = 0; n < total; n += block)
        model->process(&in[n], &out0[n], block);
    check(allFinite(out0), "base render is all finite");
    std::printf("  base render rms = %.6f\n", rms(out0));

    // --- morph via set_weights_ (M3 path) ----------------------------------
    {
        model->setEmbedding(eA);
        model->reset(sr, block);
        for (int n = 0; n < total; n += block)
            model->process(&in[n], &out1[n], block);
        check(allFinite(out1), "morphed render is all finite");
        bool differs = false;
        for (int n = 0; n < total; ++n)
            if (std::fabs(static_cast<double>(out0[n] - out1[n])) > 1e-9) { differs = true; break; }
        check(differs, "morph (setEmbedding) changes the output");
        std::printf("  morphed out rms = %.6f\n", rms(out1));
    }

    // --- MorphEngine: bilinear morph + smoothing over a dragged dot ---------
    {
        std::printf("MorphEngine:\n");
        // Four distinct corners, so the dot position actually selects something.
        std::vector<std::vector<float>> corners;
        for (unsigned i = 0; i < 4; ++i)
            corners.push_back(perturbEmbedding(base, 10u + i, 0.1f));

        aaom::MorphEngine engine;
        engine.setModel(aaom::MorphModel::fromBundleJson(readFile(bundlePath)));
        for (int i = 0; i < 4; ++i)
            engine.setCorner(i, corners[static_cast<std::size_t>(i)]);
        engine.setSmoothingTimeMs(30.0f);
        engine.setMorph(0.0f, 0.0f);
        engine.prepare(sr, block);
        check(engine.ready(), "engine ready after prepare");

        // Parked at (0,0) the dot sits on corner 0 alone.
        std::vector<NAM_SAMPLE> outCorner(total);
        for (int n = 0; n < total; n += block)
            engine.processBlock(&in[n], &outCorner[n], block);
        check(allFinite(outCorner), "corner-0 render is all finite");

        // Sweep the dot corner-to-corner and confirm the output stays finite
        // (click-free smoothing) and actually responds to the drag.
        std::vector<NAM_SAMPLE> outSweep(total);
        const int steps = total / block;
        for (int s = 0; s < steps; ++s)
        {
            const float t = static_cast<float>(s) / static_cast<float>(steps - 1);
            engine.setMorph(t, t); // drag from corner 0 toward corner 3
            engine.processBlock(&in[s * block], &outSweep[s * block], block);
        }
        check(allFinite(outSweep), "swept-morph render is all finite");
        std::printf("  swept out rms = %.6f\n", rms(outSweep));

        bool sweepDiffers = false;
        for (int n = 0; n < total; ++n)
            if (std::fabs(static_cast<double>(outSweep[n] - outCorner[n])) > 1e-9) { sweepDiffers = true; break; }
        check(sweepDiffers, "dragging the dot changes the render");

        // Blend equivalence, end to end: the engine sums folded corners while
        // MorphModel folds the blended embedding. Held at one dot position (so the
        // smoother never engages after prepare) the two must render the same audio.
        // This is what licenses caching corner folds instead of re-folding per block
        // -- and it is what makes the delta arch cost the same here as FiLM.
        {
            const float bx = 0.3f, by = 0.8f;
            aaom::MorphEngine eng2;
            eng2.setModel(aaom::MorphModel::fromBundleJson(readFile(bundlePath)));
            for (int i = 0; i < 4; ++i)
                eng2.setCorner(i, corners[static_cast<std::size_t>(i)]);
            eng2.setMorph(bx, by);
            eng2.prepare(sr, block);

            const float bw[4] = {(1.0f - bx) * (1.0f - by), bx * (1.0f - by), (1.0f - bx) * by, bx * by};
            std::vector<float> eBlend(base.size(), 0.0f);
            for (int i = 0; i < 4; ++i)
                for (std::size_t t = 0; t < eBlend.size(); ++t)
                    eBlend[t] += bw[i] * corners[static_cast<std::size_t>(i)][t];

            auto ref = aaom::MorphModel::fromBundleJson(readFile(bundlePath));
            ref->buildDsp(eBlend, sr, block);

            std::vector<NAM_SAMPLE> aEng(total), aRef(total);
            for (int n = 0; n < total; n += block)
            {
                eng2.processBlock(&in[n], &aEng[n], block);
                ref->process(&in[n], &aRef[n], block);
            }
            double maxErr = 0.0;
            for (int n = 0; n < total; ++n)
                maxErr = std::max(maxErr, std::fabs(static_cast<double>(aEng[n] - aRef[n])));
            std::printf("  blend vs direct fold: max|diff| = %.3e over rms %.6f\n", maxErr, rms(aRef));
            check(maxErr < 1.0e-4, "engine corner-blend == fold of blended embedding");
        }

        // Corner FIFO (RT-safe update path): queue corner changes and confirm the
        // following blocks reflect them and stay finite. Queued corners are
        // re-folded one per block, so give the drain a few blocks to catch up.
        std::vector<NAM_SAMPLE> after(block);
        const std::vector<float> newEmb = perturbEmbedding(base, 99u, 0.1f);
        for (int i = 0; i < 4; ++i)
            engine.queueCorner(i, newEmb);
        engine.setMorph(0.5f, 0.5f);
        for (int i = 0; i < 6; ++i)
            engine.processBlock(&in[0], after.data(), block);
        check(allFinite(after), "post-queue render is all finite");
    }

    // --- delta arch: low-rank weight residual -------------------------------
    {
        std::printf("DeltaWaveNet:\n");
        const TinyDelta td;

        std::unique_ptr<aaom::MorphModel> dm;
        try
        {
            dm = aaom::MorphModel::fromBundleJson(td.json());
        }
        catch (const std::exception& e)
        {
            std::printf("  [FAIL] delta bundle parse threw: %s\n", e.what());
            return 1;
        }
        check(dm->archType() == aaom::ArchType::Delta, "arch.type delta_wavenet_a2 -> ArchType::Delta");
        check(dm->deltaRank() == TinyDelta::R, "delta_rank parsed");
        check(dm->channels() == TinyDelta::C && dm->numLayers() == 2, "delta arch dims parsed");

        // Known-answer fold at two embeddings: e[0] drives coeff[0] linearly on top
        // of the coeff bias, so this pins the matvec, the [R, nOut] row indexing and
        // the (dW, db, dm) split offsets all at once.
        for (float probe : {0.0f, 2.0f})
        {
            std::vector<float> e(TinyDelta::E, 0.0f);
            e[0] = probe;

            std::vector<float> got;
            dm->foldEmbedding(e, got);
            const std::vector<float> want = td.expectedFold(e);

            bool sized = (got.size() == want.size() && got.size() == dm->namWeightCount());
            double maxErr = 0.0;
            if (sized)
                for (std::size_t i = 0; i < got.size(); ++i)
                    maxErr = std::max(maxErr, std::fabs(static_cast<double>(got[i] - want[i])));
            std::printf("  e[0]=%.1f: stream %zu floats, max|got - want| = %.3e\n", probe, got.size(), maxErr);
            check(sized && maxErr == 0.0, "delta fold matches the closed-form residual");
        }

        // Same affine identity the engine's corner-blend depends on, on the delta path.
        {
            std::vector<float> e0(TinyDelta::E, 0.0f), e1(TinyDelta::E, 0.0f), mix(TinyDelta::E, 0.0f);
            e0[0] = 1.0f;
            e1[0] = -3.0f;
            const float alpha = 0.25f;
            for (int i = 0; i < TinyDelta::E; ++i)
                mix[static_cast<std::size_t>(i)] = alpha * e0[static_cast<std::size_t>(i)]
                                                   + (1.0f - alpha) * e1[static_cast<std::size_t>(i)];

            std::vector<float> f0, f1, fm;
            dm->foldEmbedding(e0, f0);
            dm->foldEmbedding(e1, f1);
            dm->foldEmbedding(mix, fm);
            double maxErr = 0.0;
            for (std::size_t i = 0; i < fm.size(); ++i)
                maxErr = std::max(maxErr, std::fabs(alpha * static_cast<double>(f0[i])
                                                    + (1.0 - alpha) * static_cast<double>(f1[i])
                                                    - static_cast<double>(fm[i])));
            std::printf("  affine fold (delta): max|blend - fold| = %.3e\n", maxErr);
            check(maxErr < 1.0e-5, "delta fold is affine in the embedding");
        }

        // The folded stream is a stock A2 capture, so NAM must build and render it.
        try
        {
            std::vector<float> e(TinyDelta::E, 0.0f);
            dm->buildDsp(e, sr, block);
            std::vector<NAM_SAMPLE> dOut(block * 20);
            for (int n = 0; n < static_cast<int>(dOut.size()); n += block)
                dm->process(&in[n], &dOut[n], block);
            check(dm->hasDsp() && allFinite(dOut), "delta stream builds a NAM WaveNet and renders finite");
        }
        catch (const std::exception& e)
        {
            std::printf("  [FAIL] delta buildDsp/process threw: %s\n", e.what());
            ++g_failures;
        }

        // Rejection paths: a sibling arch must fail loudly rather than have its
        // layer tensors reinterpreted as FiLM.
        auto rejects = [](const std::string& text) {
            try
            {
                aaom::MorphModel::fromBundleJson(text);
            }
            catch (const std::exception&)
            {
                return true;
            }
            return false;
        };
        check(rejects(td.json("tabledelta_wavenet_a2")), "unknown arch.type is rejected");
        check(rejects(td.json("delta_wavenet_a2", 0)), "delta bundle without delta_rank is rejected");
        check(rejects(td.json("film_wavenet_a2")), "film arch.type over delta tensors is rejected");
    }

    // --- Resampler48k: exact host-block length + finite round-trip ----------
    {
        std::printf("Resampler48k:\n");
        for (double hostRate : {44100.0, 48000.0, 88200.0, 96000.0})
        {
            aaom::Resampler48k rs;
            const int hb = 512;
            rs.prepare(hostRate, hb);

            std::vector<float> hin(hb), hout(hb);
            std::vector<float> fullIn, fullOut; // full stream for delay-aligned error
            bool finite = true, exactLen = true;
            const int blocks = static_cast<int>(hostRate * 0.5 / hb);
            long phase = 0;
            for (int b = 0; b < blocks; ++b)
            {
                for (int i = 0; i < hb; ++i, ++phase)
                    hin[static_cast<std::size_t>(i)] =
                        static_cast<float>(std::sin(2.0 * M_PI * 500.0 * phase / hostRate));
                int producedInner = -1;
                rs.process(hin.data(), hout.data(), hb, [&](float* buf, int m) { producedInner = m; (void)buf; });
                if (rs.active() && producedInner < 0)
                    exactLen = false; // callback must fire
                for (int i = 0; i < hb; ++i)
                    if (!std::isfinite(hout[static_cast<std::size_t>(i)]))
                        finite = false;
                if (b > 3) // skip warm-up
                {
                    fullIn.insert(fullIn.end(), hin.begin(), hin.end());
                    fullOut.insert(fullOut.end(), hout.begin(), hout.end());
                }
            }

            // Round-trip adds a few samples of latency; find the best integer delay
            // and measure the residual there (true distortion, not phase offset).
            double bestErr = 1e9;
            const int N = static_cast<int>(fullIn.size());
            for (int d = 0; d <= 32; ++d)
            {
                double m = 0.0;
                for (int i = 0; i + d < N; ++i)
                    m = std::max(m, static_cast<double>(std::fabs(fullOut[static_cast<std::size_t>(i + d)]
                                                                  - fullIn[static_cast<std::size_t>(i)])));
                bestErr = std::min(bestErr, m);
            }
            std::printf("  host=%.0f active=%d maxInner=%d aligned_err=%.4f\n", hostRate, (int)rs.active(),
                        rs.maxInnerBlock(), bestErr);
            check(finite, "resampler output finite");
            check(exactLen, "resampler always fills the host block");
            // Delay-aligned residual should be small (clean cubic round-trip).
            check(bestErr < 0.05, "resampler round-trip is a clean delayed copy");
        }
    }

    // --- FftConvolver: matches direct convolution, and adds no latency -------
    {
        std::printf("FftConvolver:\n");

        // Decaying noise, the shape of a real cabinet IR, at lengths that put
        // the split either side of a partition boundary and force P to grow.
        auto makeIr = [](int n) {
            std::vector<float> h(static_cast<std::size_t>(n));
            unsigned seed = 22222u;
            for (int i = 0; i < n; ++i)
            {
                seed = seed * 1664525u + 1013904223u;
                const float r = static_cast<float>(seed >> 8) / static_cast<float>(1 << 24) * 2.0f - 1.0f;
                h[static_cast<std::size_t>(i)] = r * std::exp(-3.0f * i / n);
            }
            return h;
        };

        const int total = 6000;
        std::vector<float> x(static_cast<std::size_t>(total));
        for (int n = 0; n < total; ++n)
            x[static_cast<std::size_t>(n)] = static_cast<float>(0.6 * std::sin(0.017 * n) + 0.3 * std::sin(0.31 * n));

        // Block sizes the convolver must survive: shorter than a partition,
        // longer than one, and never a round multiple.
        const int blockSizes[] = {1, 7, 64, 333, 1024};

        for (int irLen : {64, 128, 129, 1000, 9000})
        {
            const std::vector<float> h = makeIr(irLen);

            std::vector<float> want(static_cast<std::size_t>(total), 0.0f);
            for (int n = 0; n < total; ++n)
            {
                double acc = 0.0;
                for (int j = 0; j < irLen && j <= n; ++j)
                    acc += static_cast<double>(h[static_cast<std::size_t>(j)]) * x[static_cast<std::size_t>(n - j)];
                want[static_cast<std::size_t>(n)] = static_cast<float>(acc);
            }

            aaom::FftConvolver conv;
            conv.setImpulseResponse(h.data(), irLen);

            // Processed in place, which is how the plugin calls it.
            std::vector<float> got = x;
            int pos = 0, bi = 0;
            while (pos < total)
            {
                const int n = std::min(blockSizes[bi++ % 5], total - pos);
                conv.process(&got[static_cast<std::size_t>(pos)], &got[static_cast<std::size_t>(pos)], n);
                pos += n;
            }

            double err = 0.0, ref = 0.0;
            for (int n = 0; n < total; ++n)
            {
                err = std::max(err, std::fabs(static_cast<double>(got[static_cast<std::size_t>(n)]
                                                                  - want[static_cast<std::size_t>(n)])));
                ref = std::max(ref, std::fabs(static_cast<double>(want[static_cast<std::size_t>(n)])));
            }
            std::printf("  irLen=%d P=%d parts=%d peak=%.3f err=%.2e\n", irLen, conv.partitionSize(),
                        conv.numPartitions(), ref, err);
            check(err < 1e-4 * std::max(1.0, ref), "convolution matches the direct sum");
        }

        // Zero latency is the whole point of splitting head from tail: a delta
        // at tap 0 must come straight back out, sample-aligned.
        {
            std::vector<float> h(2000, 0.0f);
            h[0] = 1.0f;
            aaom::FftConvolver conv;
            conv.setImpulseResponse(h.data(), static_cast<int>(h.size()));

            std::vector<float> y(static_cast<std::size_t>(total), 0.0f);
            conv.process(x.data(), y.data(), total);

            double err = 0.0;
            for (int n = 0; n < total; ++n)
                err = std::max(err, std::fabs(static_cast<double>(y[static_cast<std::size_t>(n)]
                                                                  - x[static_cast<std::size_t>(n)])));
            check(err < 1e-6, "unit-impulse IR is a sample-aligned pass-through (no added latency)");
        }
    }

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
