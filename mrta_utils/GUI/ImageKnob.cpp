#include "ImageKnob.h"

namespace mrta
{

ImageKnob::ImageKnob(const juce::String& paramID, juce::AudioProcessorValueTreeState& apvts, const juce::Image& image) :
    ParameterSlider(paramID, apvts),
    knobImage(image)
{
}

ImageKnob::~ImageKnob()
{
}

void ImageKnob::paint(juce::Graphics& g)
{
    const auto normRange { getNormalisableRange() };
    const auto normValue { normRange.convertTo0to1(getValue()) };
    const auto bounds { getLocalBounds() };
    const auto centre { bounds.getCentre() };
    g.addTransform(juce::AffineTransform::rotation(normValue * 2.0 * M_PI, centre.x, centre.y));
    g.drawImage(knobImage, getLocalBounds().toFloat());
}

void ImageKnob::resized()
{
}


}
