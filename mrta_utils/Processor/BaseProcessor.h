#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <ParameterManager.h>

namespace mrta
{

class BaseProcessor : public juce::AudioProcessor
{
public:
    BaseProcessor(const std::vector<mrta::ParameterInfo>& params);
    ~BaseProcessor() override;

    // Called before processing starts
    virtual void prepare(double newSampleRate, int maxBufferSize) = 0;

    // Audio stream callback
    virtual void process(juce::AudioBuffer<float>&, juce::MidiBuffer&) = 0;

    // Helper to access the parameter manager
    mrta::ParameterManager& getParameterManager() { return paramMngr; }


protected:
    // Register a parameter callback
    void registerParameterCallback(const juce::String& parameterID, ParameterManager::Callback&& cb);


private:
    mrta::ParameterManager paramMngr;

    // JUCE overriden methods
    //==============================================================================
    // Called before processing starts
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    // Called after process stops
    void releaseResources() override;
    // Audio stream callback
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // Select mono/stereo IO
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    // Save param state
    void getStateInformation(juce::MemoryBlock& destData) override;
    // Load param state
    void setStateInformation(const void* data, int sizeInBytes) override;
    //==============================================================================

    // JUCE boiler plate stuff...
    //==============================================================================
    bool hasEditor() const override;
    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;
    //==============================================================================

    BaseProcessor() = delete;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BaseProcessor)
};

}

#ifndef CREATE_PLUGIN
    #define CREATE_PLUGIN(processor) \
        juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new processor(); }
#endif
