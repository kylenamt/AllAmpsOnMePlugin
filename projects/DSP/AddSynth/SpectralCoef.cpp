
#include "SpectralCoef.h"

#include <algorithm>
#include <cmath>
#include "HarmonicTable_Fl.h"
#include "HarmonicTable_TpC.h"
#include "HarmonicTable_Vn.h"

// find the "index" that contains the value close to "targest" in the array
int findNearestIndexLocal(const int* values, int count, int target)
{
	if (count <= 0)
		return 0;
	int bestIndex = 0;
	int bestDistance = std::abs(target - values[0]);
	for (int i = 1; i < count; ++i)
	{
		const int distance = std::abs(target - values[i]);
		if (distance < bestDistance)
		{
			bestIndex = i;
			bestDistance = distance;
		}
	}
	return bestIndex;
}
namespace DSP
{

SpectralCoefficients::SpectralCoefficients() = default;


// Main logic, combine harmonic coefficients from sources
//==================================================================================
void SpectralCoefficients::computeCoefficients(CoefficientArray& out, int numPartials, int midiNote, int velocity)
{

	// init manual buffer with manual amplitudes set by the user (0.0 by default)
	CoefficientArray manualBuf{};
	manualBuf.fill(0.0f);
	const int partialCount = std::max(0, std::min(numPartials, kMaxPartials));
	for (int i = 0; i < partialCount; ++i)
	{
		if (i < kManualPartials)
			manualBuf[static_cast<size_t>(i)] = manualAmps[static_cast<size_t>(i)];
	}



	// fill preset buffer with common waveform coef
	CoefficientArray presetBuf{};
	presetBuf.fill(0.0f);
	for (int i = 0; i < partialCount; ++i)
	{
		const int n = i + 1;
		float amp = 0.0f;

		switch (presetShape)
		{
				case PresetShape::Sine:
					amp = (n == 1) ? 1.0f : 0.0f; // A#0 = 1
					break;

				case PresetShape::Sawtooth:
					amp = 1.0f / static_cast<float>(n); // A#n = 1/n
					break;

				case PresetShape::Square:
					if ((n % 2) == 1)
						amp = 1.0f / static_cast<float>(n); // A#n = 1/n for odd harmonics
					break;

				case PresetShape::Triangle:
					if ((n % 2) == 1)
					{
						amp = 1.0f / (static_cast<float>(n) * static_cast<float>(n)); // A#n = 1/n^2 for odd harmonics
						// triangle wave Fourier series requires alternating signs for odd harmonics
						const int k = (n - 1) / 2;
						if (k % 2 == 1)
							amp = -amp; // not needed for audio but good for waveform drawing
					}
					break;
		}
		// set skewness of the harmonic slope for brightness
		float slope = std::pow(static_cast<float>(n), 1.0f - presetBrightness);
		amp = std::copysign(std::abs(amp) * slope , amp);
		presetBuf[static_cast<size_t>(i)] = amp;
	}



	// fill table buffer with precalculated coefficient save in the harmonic tables
	tableBuf.fill(0.0f);
	if (tableSource != TableSource::None)
	{
		const float* tablePtr = nullptr;
		// search the table for the harmonic coef corresponding to midiNote and velocity, though velocity is fix for most case
		switch (tableSource)
		{
			case TableSource::Flute:
				{
					const int noteIndex = findNearestIndexLocal(DSP::kFlTableNoteNumbers,
															DSP::kFlTableNumNotes,
															midiNote);
					const int velocityIndex = findNearestIndexLocal(DSP::kFlTableVelocities,
																DSP::kFlTableNumVelocities,
																velocity);
					tablePtr = DSP::kFlPartialTable[noteIndex][velocityIndex].data();
				}
				break;
			case TableSource::TrumpetC:
				{
					const int noteIndex = findNearestIndexLocal(DSP::kTpCTableNoteNumbers,
															DSP::kTpCTableNumNotes,
															midiNote);
					const int velocityIndex = findNearestIndexLocal(DSP::kTpCTableVelocities,
																DSP::kTpCTableNumVelocities,
																velocity);
					tablePtr = DSP::kTpCPartialTable[noteIndex][velocityIndex].data();
				}
				break;
			case TableSource::Violin:
				{
					const int noteIndex = findNearestIndexLocal(DSP::kVnTableNoteNumbers,
															DSP::kVnTableNumNotes,
															midiNote);
					const int velocityIndex = findNearestIndexLocal(DSP::kVnTableVelocities,
																DSP::kVnTableNumVelocities,
																velocity);
					tablePtr = DSP::kVnPartialTable[noteIndex][velocityIndex].data();
				}
				break;
			default:
				break;
		}

		if (tablePtr != nullptr)
		{
			for (int i = 0; i < DSP::kMaxPartials; ++i)
				tableBuf[static_cast<size_t>(i)] = tablePtr[static_cast<size_t>(i)];
		}
	}


	
	// total level of all sources
	const float m = std::max(0.0f, std::min(manualLevel, 1.0f));
	const float p = std::max(0.0f, std::min(presetLevel, 1.0f));
	const float t = std::max(0.0f, std::min(tableLevel, 1.0f));
	const float e = std::max(0.0f, std::min(externalLevel, 1.0f));
	const float total = m + p + t + e;

	// mix the coefficient across sources, no normalization
	if (total <= 0.0f)
	{
		out.fill(0.0f);
	}
	else
	{
		for (int i = 0; i < DSP::kMaxPartials; ++i)
		{
			out[static_cast<size_t>(i)] = (m * manualBuf[static_cast<size_t>(i)]
										 + p * presetBuf[static_cast<size_t>(i)]
										 + t * tableBuf[static_cast<size_t>(i)]
										 + e * externalBuf[static_cast<size_t>(i)]);
		}
	}
}

//Setters
//===================================================================================
void SpectralCoefficients::setManualAmp(int index, float amp)
{
	if (index < 0 || index >= kManualPartials)
		return;
	manualAmps[static_cast<size_t>(index)] = std::max(0.0f, std::min(amp, 1.0f));
}

void SpectralCoefficients::setManualLevel(float level)
{
	manualLevel = std::max(0.0f, std::min(level, 1.0f));
}

void SpectralCoefficients::setPresetShape(PresetShape s)
{
	presetShape = s;
}

void SpectralCoefficients::setPresetBrightness(float b)
{
	presetBrightness = std::max(0.1f, std::min(b, 4.0f));
}

void SpectralCoefficients::setPresetLevel(float level)
{
	presetLevel = std::max(0.0f, std::min(level, 1.0f));
}

void SpectralCoefficients::setTableSource(TableSource src)
{
	tableSource = src;
}

void SpectralCoefficients::setTableLevel(float level)
{
	tableLevel = std::max(0.0f, std::min(level, 1.0f));
}

void SpectralCoefficients::setExternalCoefficients(const CoefficientArray& coeffs)
{
	externalBuf = coeffs;
}

void SpectralCoefficients::setExternalLevel(float level)
{
	externalLevel = std::max(0.0f, std::min(level, 1.0f));
}
}
