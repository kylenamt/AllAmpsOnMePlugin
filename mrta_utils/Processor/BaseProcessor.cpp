#include <BaseProcessor.h>

namespace mrta
{

BaseProcessor::BaseProcessor(const std::vector<mrta::ParameterInfo>& params) :
    AudioProcessor(BusesProperties()
        .withInput("Input",  juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    paramMngr(*this, JucePlugin_Name "v" JucePlugin_VersionString, params)
{
}

BaseProcessor::~BaseProcessor()
{
}

void BaseProcessor::registerParameterCallback(const juce::String& parameterID, ParameterManager::Callback&& cb)
{
    paramMngr.registerParameterCallback(parameterID, std::move(cb));
}

void BaseProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    // All parameters should be updated before the audio processing starts
    paramMngr.updateParameters(true);

    prepare(sampleRate, samplesPerBlock);
}

void BaseProcessor::releaseResources()
{
    // Flush down parameter event queue
    paramMngr.clearParameterQueue();
}

void BaseProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Updates the parameters and call any callbacks necessary
    paramMngr.updateParameters();

    process(buffer, midi);
}

bool BaseProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Supports only stereo or mono I/O
    if (   layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // Number of input channels should be the same as output
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void BaseProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    paramMngr.getStateInformation(destData);
}

void BaseProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    paramMngr.setStateInformation(data, sizeInBytes);
}

//==============================================================================
bool BaseProcessor::hasEditor() const { return true; }
const juce::String BaseProcessor::getName() const { return JucePlugin_Name; }
bool BaseProcessor::acceptsMidi() const { return JucePlugin_IsSynth; }
bool BaseProcessor::producesMidi() const { return false; }
bool BaseProcessor::isMidiEffect() const { return false; }
double BaseProcessor::getTailLengthSeconds() const { return 0.0; }
int BaseProcessor::getNumPrograms() { return 1; }
int BaseProcessor::getCurrentProgram() { return 0; }
void BaseProcessor::setCurrentProgram(int) { }
const juce::String BaseProcessor::getProgramName(int) { return {}; }
void BaseProcessor::changeProgramName(int, const juce::String&) { }
//==============================================================================

}
