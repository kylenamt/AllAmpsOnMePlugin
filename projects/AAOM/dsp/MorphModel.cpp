#include "MorphModel.h"
#include "Base64.h"

#include "NAM/get_dsp.h"
#include "NAM/wavenet/model.h"

#include "json.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace aaom
{

using json = nlohmann::json;

namespace
{
// Read a tensor node into a flat float vector. The expected on-disk form is
//   { "shape": [...], "data": "<base64 float32 little-endian, C-order>" }
// but we also accept a raw base64 string, or a plain JSON number array, so the
// parser is forgiving about small exporter variations. If expectedCount > 0 the
// decoded length is validated against it.
std::vector<float> readTensor(const json& node, const char* name, std::size_t expectedCount)
{
    std::vector<float> out;

    if (node.is_string())
    {
        out = base64DecodeFloat32(node.get<std::string>());
    }
    else if (node.is_array())
    {
        out.reserve(node.size());
        for (const auto& v : node)
            out.push_back(v.get<float>());
    }
    else if (node.is_object())
    {
        const char* keys[] = {"data", "b64", "base64"};
        const json* blob = nullptr;
        for (const char* k : keys)
        {
            if (node.contains(k)) { blob = &node.at(k); break; }
        }
        if (blob == nullptr || !blob->is_string())
            throw std::runtime_error(std::string("tensor '") + name + "' is missing a base64 'data' string");
        out = base64DecodeFloat32(blob->get<std::string>());
    }
    else
    {
        throw std::runtime_error(std::string("tensor '") + name + "' has an unsupported JSON type");
    }

    if (expectedCount != 0 && out.size() != expectedCount)
        throw std::runtime_error(std::string("tensor '") + name + "' has " + std::to_string(out.size())
                                 + " elements, expected " + std::to_string(expectedCount));
    return out;
}

std::vector<int> readIntArray(const json& node, const char* name)
{
    if (!node.is_array())
        throw std::runtime_error(std::string("'") + name + "' must be an array");
    std::vector<int> out;
    out.reserve(node.size());
    for (const auto& v : node)
        out.push_back(v.get<int>());
    return out;
}
} // namespace

const char* archTypeName(ArchType t)
{
    return t == ArchType::Delta ? "delta_wavenet_a2" : "film_wavenet_a2";
}

MorphModel::~MorphModel() = default;

std::unique_ptr<MorphModel> MorphModel::fromBundleJson(const std::string& jsonText)
{
    json j = json::parse(jsonText);

    if (!j.contains("aaom_bundle"))
        throw std::runtime_error("not an aaom bundle (missing 'aaom_bundle')");

    auto model = std::make_unique<MorphModel>();

    // --- run / sample rate --------------------------------------------------
    if (j.contains("run") && j["run"].is_object())
    {
        model->runName_ = j["run"].value("name", std::string{});
        model->runSha8_ = j["run"].value("sha8", std::string{});
    }
    model->sampleRate_ = j.value("sample_rate", 48000.0);

    // --- architecture -------------------------------------------------------
    const json& arch = j.at("arch");
    const int C = arch.at("channels").get<int>();
    const int E = arch.at("embedding_dim").get<int>();
    model->channels_ = C;
    model->embeddingDim_ = E;
    model->headKernel_ = arch.at("head_kernel").get<int>();
    model->headScale_ = arch.at("head_scale").get<float>();
    model->activation_ = arch.value("activation", std::string("LeakyReLU"));
    model->kernelSizes_ = readIntArray(arch.at("kernel_sizes"), "arch.kernel_sizes");
    model->dilations_ = readIntArray(arch.at("dilations"), "arch.dilations");

    const std::size_t numLayers = model->kernelSizes_.size();
    if (numLayers == 0)
        throw std::runtime_error("arch.kernel_sizes is empty");
    if (model->dilations_.size() != numLayers)
        throw std::runtime_error("arch.dilations length must match arch.kernel_sizes length");
    if (C <= 0 || E <= 0 || model->headKernel_ <= 0)
        throw std::runtime_error("arch has non-positive channels/embedding_dim/head_kernel");

    // Conditioning architecture. Bundles predating the field are FiLM. Anything
    // else (tabledelta_wavenet_a2, mlpfilm_wavenet_a2, ...) is rejected loudly
    // rather than reinterpreted -- the layer tensors would be misread silently.
    const std::string archName = arch.value("type", std::string("film_wavenet_a2"));
    if (archName == "delta_wavenet_a2")
    {
        model->archType_ = ArchType::Delta;
        model->deltaRank_ = arch.value("delta_rank", 0);
        if (model->deltaRank_ <= 0)
            throw std::runtime_error("delta bundle is missing a positive 'arch.delta_rank'");
    }
    else if (archName == "film_wavenet_a2")
    {
        model->archType_ = ArchType::Film;
    }
    else
    {
        throw std::runtime_error("unsupported arch.type '" + archName
                                 + "' (expected 'film_wavenet_a2' or 'delta_wavenet_a2')");
    }

    // --- weights ------------------------------------------------------------
    const json& w = j.at("weights");
    model->rechannelW_ = readTensor(w.at("rechannel_w"), "rechannel_w", static_cast<std::size_t>(C));

    const json& layersJson = w.at("layers");
    if (!layersJson.is_array() || layersJson.size() != numLayers)
        throw std::runtime_error("weights.layers length must match arch.kernel_sizes length");

    model->layers_.resize(numLayers);
    for (std::size_t i = 0; i < numLayers; ++i)
    {
        const json& lj = layersJson[i];
        Layer& L = model->layers_[i];
        L.kernel = model->kernelSizes_[i];
        const std::size_t Cc = static_cast<std::size_t>(C);
        const std::size_t k = static_cast<std::size_t>(L.kernel);
        L.convW = readTensor(lj.at("conv_w"), "conv_w", Cc * Cc * k);
        L.convB = readTensor(lj.at("conv_b"), "conv_b", Cc);
        L.mixinW = readTensor(lj.at("mixin_w"), "mixin_w", Cc);
        if (model->archType_ == ArchType::Delta)
        {
            const std::size_t R = static_cast<std::size_t>(model->deltaRank_);
            // basis rows span the layer's whole residual: dW [C,C,k], db [C], dm [C].
            const std::size_t nOut = Cc * Cc * k + 2 * Cc;
            L.coeffW = readTensor(lj.at("delta_coeff_w"), "delta_coeff_w", R * static_cast<std::size_t>(E));
            L.coeffB = readTensor(lj.at("delta_coeff_b"), "delta_coeff_b", R);
            L.basis = readTensor(lj.at("delta_basis"), "delta_basis", R * nOut);
            model->maxDeltaOut_ = std::max(model->maxDeltaOut_, nOut);
        }
        else
        {
            L.filmW = readTensor(lj.at("film_w"), "film_w", 2 * Cc * static_cast<std::size_t>(E));
            L.filmB = readTensor(lj.at("film_b"), "film_b", 2 * Cc);
        }
        L.x1W = readTensor(lj.at("x1_w"), "x1_w", Cc * Cc);
        L.x1B = readTensor(lj.at("x1_b"), "x1_b", Cc);
    }

    model->headW_ = readTensor(w.at("head_w"), "head_w",
                               static_cast<std::size_t>(C) * static_cast<std::size_t>(model->headKernel_));
    model->headB_ = readTensor(w.at("head_b"), "head_b", 1);

    // --- profiles -----------------------------------------------------------
    if (j.contains("profiles") && j["profiles"].is_array())
    {
        for (const auto& pj : j["profiles"])
        {
            Profile p;
            p.name = pj.value("name", std::string{});
            for (const auto& v : pj.at("embedding"))
                p.embedding.push_back(v.get<float>());
            if (static_cast<int>(p.embedding.size()) != E)
                throw std::runtime_error("profile '" + p.name + "' embedding length "
                                         + std::to_string(p.embedding.size()) + " != embedding_dim " + std::to_string(E));
            model->profiles_.push_back(std::move(p));
        }
    }

    return model;
}

std::size_t MorphModel::namWeightCount() const
{
    std::size_t n = rechannelW_.size();
    for (const Layer& L : layers_)
        n += L.convW.size() + L.convB.size() + L.mixinW.size() + L.x1W.size() + L.x1B.size();
    n += headW_.size() + headB_.size();
    n += 1; // head_scale trails the stream
    return n;
}

void MorphModel::foldEmbedding(const std::vector<float>& e, std::vector<float>& out) const
{
    if (static_cast<int>(e.size()) != embeddingDim_)
        throw std::runtime_error("foldEmbedding: embedding length " + std::to_string(e.size())
                                 + " != embedding_dim " + std::to_string(embeddingDim_));

    const int C = channels_;
    const int E = embeddingDim_;

    if (archType_ == ArchType::Delta)
    {
        if (static_cast<int>(coeffScratch_.size()) != deltaRank_)
            coeffScratch_.assign(deltaRank_, 0.0f);
        if (deltaScratch_.size() != maxDeltaOut_)
            deltaScratch_.assign(maxDeltaOut_, 0.0f);
    }
    else if (static_cast<int>(gammaScratch_.size()) != C)
    {
        gammaScratch_.assign(C, 0.0f);
        betaScratch_.assign(C, 0.0f);
    }

    out.clear();
    if (out.capacity() < namWeightCount())
        out.reserve(namWeightCount());

    // 1) rechannel (embedding-independent)
    out.insert(out.end(), rechannelW_.begin(), rechannelW_.end());

    // 2) per layer: folded conv (w,b), folded mixin (w), then unchanged layer1x1 (w,b)
    for (const Layer& L : layers_)
    {
        const int k = L.kernel;

        if (archType_ == ArchType::Delta)
        {
            const int R = deltaRank_;
            const std::size_t nW = static_cast<std::size_t>(C) * C * k;
            const std::size_t nOut = nW + 2 * static_cast<std::size_t>(C);

            // coeff = delta_coeff_w . e + delta_coeff_b; coeff_w is [R, E] row-major.
            for (int r = 0; r < R; ++r)
            {
                const float* row = &L.coeffW[static_cast<std::size_t>(r) * E];
                float acc = L.coeffB[r];
                for (int jdx = 0; jdx < E; ++jdx)
                    acc += row[jdx] * e[jdx];
                coeffScratch_[r] = acc;
            }

            // flat = coeff . delta_basis; basis is [R, nOut] row-major, already
            // scale-premultiplied and row-normalised by the exporter. Rank outer so
            // each basis row is one contiguous sweep.
            float* delta = deltaScratch_.data();
            std::fill(delta, delta + nOut, 0.0f);
            for (int r = 0; r < R; ++r)
            {
                const float cr = coeffScratch_[r];
                const float* row = &L.basis[static_cast<std::size_t>(r) * nOut];
                for (std::size_t n = 0; n < nOut; ++n)
                    delta[n] += cr * row[n];
            }

            // split(flat, (C*C*k, C, C)) -> dW, db, dm, added onto the shared weights.
            for (std::size_t t = 0; t < nW; ++t)
                out.push_back(L.convW[t] + delta[t]);
            for (int c = 0; c < C; ++c)
                out.push_back(L.convB[c] + delta[nW + static_cast<std::size_t>(c)]);
            for (int c = 0; c < C; ++c)
                out.push_back(L.mixinW[c] + delta[nW + static_cast<std::size_t>(C + c)]);
        }
        else
        {
            // gamma / beta = film(e); film_w is [2C, E] row-major, film_b is [2C].
            // gamma = rows [0, C), beta = rows [C, 2C).
            for (int c = 0; c < C; ++c)
            {
                const float* gRow = &L.filmW[static_cast<std::size_t>(c) * E];
                const float* bRow = &L.filmW[static_cast<std::size_t>(C + c) * E];
                float gamma = L.filmB[c];
                float beta = L.filmB[C + c];
                for (int jdx = 0; jdx < E; ++jdx)
                {
                    gamma += gRow[jdx] * e[jdx];
                    beta += bRow[jdx] * e[jdx];
                }

                // conv.weight[c] *= gamma  (layout [out=c][in][k], contiguous C*k block)
                const float* convRow = &L.convW[static_cast<std::size_t>(c) * C * k];
                for (int t = 0; t < C * k; ++t)
                    out.push_back(convRow[t] * gamma);
                // (conv bias and mixin need gamma/beta too — recomputed below to keep
                //  the stream in NAM order: all conv weights, then all conv biases.)
                gammaScratch_[c] = gamma;
                betaScratch_[c] = beta;
            }

            // conv.bias[c] = gamma*conv.bias[c] + beta
            for (int c = 0; c < C; ++c)
                out.push_back(gammaScratch_[c] * L.convB[c] + betaScratch_[c]);

            // mixin.weight[c] *= gamma  (layout [out=c][in=1])
            for (int c = 0; c < C; ++c)
                out.push_back(L.mixinW[c] * gammaScratch_[c]);
        }

        // layer1x1 (unchanged): weight [out][in], then bias [out]
        out.insert(out.end(), L.x1W.begin(), L.x1W.end());
        out.insert(out.end(), L.x1B.begin(), L.x1B.end());
    }

    // 3) head rechannel conv (embedding-independent), then head_scale
    out.insert(out.end(), headW_.begin(), headW_.end());
    out.insert(out.end(), headB_.begin(), headB_.end());
    out.push_back(headScale_);
}

std::string MorphModel::buildNamJson(const std::vector<float>& namWeights) const
{
    json layer;
    layer["input_size"] = 1;
    layer["condition_size"] = 1;
    layer["head"] = {{"out_channels", 1}, {"kernel_size", headKernel_}, {"bias", true}};
    layer["channels"] = channels_;
    layer["bottleneck"] = channels_;
    layer["kernel_sizes"] = kernelSizes_;
    layer["dilations"] = dilations_;
    // negative_slope is only consulted by NAM for LeakyReLU/PReLU/LeakyHardtanh
    // (see activations.cpp's ActivationConfig::from_json); harmless to include
    // for other types.
    layer["activation"] = {{"type", activation_}, {"negative_slope", 0.01}};
    layer["layer1x1"] = {{"active", true}, {"groups", 1}};

    json cfg;
    cfg["layers"] = json::array({layer});
    cfg["head"] = nullptr;
    cfg["head_scale"] = headScale_;
    cfg["in_channels"] = 1;

    json top;
    top["version"] = "0.7.0";
    top["architecture"] = "WaveNet";
    top["config"] = cfg;
    top["weights"] = namWeights;
    top["sample_rate"] = sampleRate_;
    top["metadata"] = {{"version", "0.7.0"}};
    return top.dump();
}

void MorphModel::buildDsp(const std::vector<float>& e, double sampleRate, int maxBufferSize)
{
    gammaScratch_.assign(channels_, 0.0f);
    betaScratch_.assign(channels_, 0.0f);
    if (archType_ == ArchType::Delta)
    {
        coeffScratch_.assign(deltaRank_, 0.0f);
        deltaScratch_.assign(maxDeltaOut_, 0.0f);
    }

    foldEmbedding(e, scratchWeights_);

    json top = json::parse(buildNamJson(scratchWeights_));

    nam::DspLoadOptions options;
    options.prewarm = false; // reset() below handles warm-up off the RT thread
    dsp_ = nam::get_dsp(top, options);
    wavenet_ = dynamic_cast<nam::wavenet::WaveNet*>(dsp_.get());

    reset(sampleRate, maxBufferSize);
}

void MorphModel::reset(double sampleRate, int maxBufferSize)
{
    if (dsp_)
        dsp_->Reset(sampleRate, maxBufferSize);
}

void MorphModel::setEmbedding(const std::vector<float>& e)
{
    if (wavenet_ == nullptr)
        return; // no live generic WaveNet to re-weight
    foldEmbedding(e, scratchWeights_);
    wavenet_->set_weights_(scratchWeights_);
}

void MorphModel::setWeights(std::vector<float>& namWeights)
{
    if (wavenet_ == nullptr)
        return; // no live generic WaveNet to re-weight
    if (namWeights.size() != namWeightCount())
        return; // caller handed us a stream for a different architecture
    wavenet_->set_weights_(namWeights);
}

void MorphModel::process(NAM_SAMPLE* in, NAM_SAMPLE* out, int numFrames)
{
    if (numFrames <= 0)
        return;

    if (dsp_ == nullptr)
    {
        if (in != out)
            std::memcpy(out, in, sizeof(NAM_SAMPLE) * static_cast<std::size_t>(numFrames));
        return;
    }

    NAM_SAMPLE* inPtrs[1] = {in};
    NAM_SAMPLE* outPtrs[1] = {out};
    dsp_->process(inPtrs, outPtrs, numFrames);
}

} // namespace aaom
