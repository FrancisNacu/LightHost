#include "../JuceLibraryCode/JuceHeader.h"
#include "../Source/PluginChain.h"
#include "../Source/AudioSetup.h"
#include "../Source/AudioPlayer.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>

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
        if (delayMs > 0)
            Thread::sleep(delayMs);
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
    int delayMs = 0;

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

void checkChannelSetup()
{
    XmlElement settings("DEVICESETUP");
    settings.setAttribute("audioDeviceInChans", "11111100");
    settings.setAttribute("audioDeviceOutChans", "1111");
    settings.setAttribute("audioDeviceRate", 48000);
    settings.setAttribute("audioDeviceBufferSize", 160);
    AudioSetup::limitSavedChannelsToStereo(settings);
    require(settings.getStringAttribute("audioDeviceInChans") == "1100",
            "Keep the selected input pair rather than switching to channels 1/2");
    require(settings.getStringAttribute("audioDeviceOutChans") == "11", "Limit output channels to stereo");
    require(settings.getIntAttribute("audioDeviceRate") == 48000
            && settings.getIntAttribute("audioDeviceBufferSize") == 160,
            "Channel migration changed rate or latency");
    settings.setAttribute("audioDeviceInChans", "100000");
    settings.setAttribute("audioDeviceOutChans", "0");
    AudioSetup::limitSavedChannelsToStereo(settings);
    require(settings.getStringAttribute("audioDeviceInChans") == "100000"
            && settings.getStringAttribute("audioDeviceOutChans") == "0", "Preserve mono and disabled channels");
    XmlElement defaults("DEVICESETUP");
    AudioSetup::limitSavedChannelsToStereo(defaults);
    require(!defaults.hasAttribute("audioDeviceInChans"), "Keep default channel selection implicit");
}

class TestDevice : public AudioIODevice
{
public:
    TestDevice() : AudioIODevice("Test device", "In-memory") {}
    StringArray getOutputChannelNames() override { return { "Left", "Right" }; }
    StringArray getInputChannelNames() override { return { "Left", "Right" }; }
    Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    Array<int> getAvailableBufferSizes() override { return { 64 }; }
    int getDefaultBufferSize() override { return 64; }
    String open(const BigInteger&, const BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return blockSize; }
    double getCurrentSampleRate() override { return sampleRate; }
    int getCurrentBitDepth() override { return 32; }
    BigInteger getActiveOutputChannels() const override { return BigInteger(3); }
    BigInteger getActiveInputChannels() const override { return BigInteger(3); }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
    int blockSize = 64;
    double sampleRate = 48000.0;
};

void checkPlayerDiagnostics()
{
    const File logFile = File::createTempFile(".log");
    {
        TestDevice device;
        GainProcessor processor(std::numeric_limits<float>::infinity());
        GainProcessor normalProcessor(2.0f);
        AudioPlayer player(logFile);
        processor.delayMs = 10; // Deliberately miss the 64-sample deadline.
        player.audioDeviceAboutToStart(&device);
        player.setProcessor(&processor);
        AudioBuffer<float> input(2, 64), output(2, 64);
        input.clear();
        output.clear();
        // Infinity times zero generates NaNs; they must not reach the device.
        player.audioDeviceIOCallback(input.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 64);
        require(output.getMagnitude(0, 64) == 0.0f, "Invalid output was not silenced");
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 64; ++sample)
                require(std::isfinite(output.getSample(channel, sample)), "NaN escaped into the output");

        player.setProcessor(&normalProcessor);
        for (int channel = 0; channel < 2; ++channel)
            for (int sample = 0; sample < 64; ++sample)
                input.setSample(channel, sample, 0.25f);
        player.audioDeviceIOCallback(input.getArrayOfReadPointers(), 2, output.getArrayOfWritePointers(), 2, 64);
        require(output.getSample(0, 0) == 0.5f && output.getSample(1, 63) == 0.5f,
                "Diagnostics changed valid audio");

        player.audioDeviceStopped();
        device.sampleRate = 96000.0;
        device.blockSize = 128;
        player.audioDeviceAboutToStart(&device);
        require(normalProcessor.getSampleRate() == 96000.0 && normalProcessor.getBlockSize() == 128,
                "Device restart did not propagate the new rate and buffer size");
        player.audioDeviceError("Test device failure");
        player.audioDeviceStopped();
        player.setProcessor(nullptr);
    }
    const String log = logFile.loadFileAsString();
    require(log.contains("96000") && log.contains("128 samples"), "Missing stream format diagnostics");
    require(log.contains("Device error: Test device failure"), "Device errors were lost");
    require(log.contains("buffers containing non-finite output: 1"), "Invalid-output diagnostic was lost");
    require(log.contains("Callbacks exceeding buffer duration:")
            && !log.contains("Callbacks exceeding buffer duration: 0"), "Missed processing deadline was not logged");
    logFile.deleteFile();
}
}

int main()
{
    ScopedJuceInitialiser_GUI juceInitialiser;
    try
    {
        checkChain();
        checkChannelSetup();
        checkPlayerDiagnostics();
        std::cout << "PASS: stereo routing, bypass CPU work, resume, instance preservation, missing plugins\n";
        std::cout << "PASS: stereo channel migration, invalid-output containment, device diagnostics\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
