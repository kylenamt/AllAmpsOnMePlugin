#pragma once

namespace DSP
{
static constexpr int kMaxPartials = 512;
static constexpr int kManualPartials = 64;
static constexpr int kWavetableSize = 2048;
static constexpr int kNumBands = 5;            // per-band ADSR frequency bands
static constexpr int kNumCrossovers = kNumBands - 1;
}
