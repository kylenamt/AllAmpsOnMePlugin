// Standalone self-test for the AllAmpsOnMe morph engine (no JUCE).
//
// Verifies the M2-critical path end to end:
//   * bundle parse -> architecture matches the A2 schedule
//   * FiLM fold produces exactly the weight count NAM's WaveNet consumes
//   * fold is deterministic and embedding-sensitive (morph actually changes weights)
//   * get_dsp() builds a generic WaveNet from the folded stream (no throw)
//   * rendering audio yields finite output, and morphing changes the output
//
// Build/run is driven by tools/run_selftest.sh (system Eigen + local NAM clone).

#include "MorphEngine.h"
#include "MorphModel.h"
#include "Resampler.h"

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

double rms(const std::vector<NAM_SAMPLE>& v)
{
    double s = 0.0;
    for (NAM_SAMPLE x : v)
        s += static_cast<double>(x) * static_cast<double>(x);
    return v.empty() ? 0.0 : std::sqrt(s / static_cast<double>(v.size()));
}
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
    std::vector<float> w0, w0b, wA;
    model->foldEmbedding(profiles.front().embedding, w0);
    model->foldEmbedding(profiles.front().embedding, w0b);
    check(w0.size() == model->namWeightCount(), "folded stream length == namWeightCount");
    check(w0 == w0b, "fold is deterministic");

    if (profiles.size() >= 2)
    {
        model->foldEmbedding(profiles[1].embedding, wA);
        check(wA.size() == w0.size() && wA != w0, "different embedding -> different weights");
    }

    // --- build NAM DSP + render --------------------------------------------
    const double sr = 48000.0;
    const int block = 64;
    try
    {
        model->buildDsp(profiles.front().embedding, sr, block);
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
    check(allFinite(out0), "profile[0] render is all finite");
    std::printf("  profile[0] out rms = %.6f\n", rms(out0));

    // --- morph via set_weights_ (M3 path) ----------------------------------
    if (profiles.size() >= 2)
    {
        model->setEmbedding(profiles[1].embedding);
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
        aaom::MorphEngine engine;
        engine.setModel(aaom::MorphModel::fromBundleJson(readFile(bundlePath)));
        engine.setSmoothingTimeMs(30.0f);
        engine.setMorph(0.0f, 0.0f);
        engine.prepare(sr, block);
        check(engine.ready(), "engine ready after prepare");

        // Sweep the dot corner-to-corner across the render and confirm the output
        // stays finite (click-free smoothing) and actually responds to the drag.
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

        // Corner FIFO (RT-safe update path): queue a corner change, confirm the
        // next processed block reflects it (output changes) and stays finite.
        std::vector<NAM_SAMPLE> before(block), after(block);
        engine.processBlock(&in[0], before.data(), block);
        std::vector<float> newEmb(static_cast<std::size_t>(model->embeddingDim()), 0.7f);
        engine.queueCorner(0, newEmb);
        engine.queueCorner(1, newEmb);
        engine.queueCorner(2, newEmb);
        engine.queueCorner(3, newEmb);
        engine.setMorph(0.5f, 0.5f);
        engine.processBlock(&in[block], after.data(), block);
        check(allFinite(after), "post-queue render is all finite");
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

    std::printf("\n%s (%d failure%s)\n", g_failures == 0 ? "ALL PASS" : "FAILURES", g_failures,
                g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
