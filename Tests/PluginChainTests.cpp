#include "../JuceLibraryCode/JuceHeader.h"
#include "../Source/PluginChain.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

class GainProcessor : public AudioProcessor
{
public:
    explicit GainProcessor(float gain_) : gain(gain_)
    {
        setPlayConfigDetails(2, 2, 48000, 64);
    }

    const String getName() const override { return "Test gain"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    void processBlock(AudioBuffer<float>& audio, MidiBuffer&) override
    {
        ++calls;
        audio.applyGain(gain);
    }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override {}
    void getStateInformation(MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    int calls = 0;

private:
    float gain;
};

void checkAudio(AudioProcessorGraph& graph, float expected)
{
    // Build the pending graph changes synchronously, without opening an audio device.
    graph.prepareToPlay(48000, 64);
    AudioBuffer<float> audio(2, 64);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 64; ++sample)
            audio.setSample(channel, sample, 0.25f);
    MidiBuffer midi;
    graph.processBlock(audio, midi);
    for (int channel = 0; channel < 2; ++channel)
        for (int sample = 0; sample < 64; ++sample)
            require(std::abs(audio.getSample(channel, sample) - expected) < 0.0001f,
                    "Unexpected audio output");
}

void checkChain()
{
    AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000, 64);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), PluginChain::inputNodeId);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), PluginChain::outputNodeId);

    PluginChain::reconnect(graph, 0, [](int) { return false; });
    checkAudio(graph, 0.25f);

    auto* first = new GainProcessor(2.0f);
    auto* second = new GainProcessor(3.0f);
    graph.addNode(first, 1);
    graph.addNode(second, 2);
    PluginChain::reconnect(graph, 2, [](int) { return false; });
    checkAudio(graph, 1.5f);
    require(first->calls == 1 && second->calls == 1, "Enabled plugins must process audio");

    PluginChain::reconnect(graph, 2, [](int index) { return index == 0; });
    checkAudio(graph, 0.75f);
    require(first->calls == 1 && second->calls == 2, "Bypassed plugin consumed processing calls");

    PluginChain::reconnect(graph, 2, [](int) { return true; });
    checkAudio(graph, 0.25f);
    require(first->calls == 1 && second->calls == 2, "Bypassed chain consumed processing calls");

    PluginChain::reconnect(graph, 2, [](int) { return false; });
    checkAudio(graph, 1.5f);
    require(first->calls == 2 && second->calls == 3, "Plugins did not resume processing");
    require(graph.getNodeForId(1)->getProcessor() == first,
            "Bypass must preserve the plugin instance");

    graph.removeNode(1);
    PluginChain::reconnect(graph, 2, [](int) { return false; });
    checkAudio(graph, 0.75f);
    graph.removeNode(2);
    PluginChain::reconnect(graph, 2, [](int) { return false; });
    checkAudio(graph, 0.25f);
    graph.releaseResources();
}
}

int main()
{
    ScopedJuceInitialiser_GUI juceInitialiser;
    try
    {
        checkChain();
        std::cout << "PASS: stereo routing, bypass CPU work, resume, instance preservation, missing plugins\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
