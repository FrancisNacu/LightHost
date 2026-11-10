#include "../JuceLibraryCode/JuceHeader.h"
#include "../Source/PluginChain.h"
#include "../Source/AudioSetup.h"
#include "../Source/AudioPlayer.h"
#include "../Source/PluginState.h"
#include "../Source/PluginBackup.h"
#include "../Source/PluginWindow.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <thread>

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

void checkIdentityAndMigration()
{
    PluginDescription first;
    first.name = "Same Name";
    first.version = "1.0";
    first.pluginFormatName = "VST3";
    first.fileOrIdentifier = "/plugins/first.vst3";
    first.uid = 1;
    PluginDescription second(first);
    second.fileOrIdentifier = "/plugins/second.vst3";
    second.uid = 2;
    KnownPluginList known, active;
    known.addType(first);
    active.addType(first);
    active.addType(second);
    const auto plugins = PluginState::collectPlugins(known, active);
    require(plugins.size() == 2, "Export omitted an active-only plugin or duplicated a known one");

    PropertySet settings;
    settings.setValue(PluginState::legacyKey("state", first), "old shared state");
    settings.setValue(PluginState::legacyKey("order", first), 7);
    settings.setValue(PluginState::legacyKey("bypass", first), true);
    settings.setValue(PluginState::legacyKey("display-name", first), "Custom name");
    PluginState::migrateLegacySettings(settings, plugins);
    require(PluginState::key("state", first) != PluginState::key("state", second), "State keys still collide");
    PluginDescription sameContainer(first);
    sameContainer.uid = 2;
    require(PluginState::key("state", first) != PluginState::key("state", sameContainer), "Shell plugin IDs share settings");
    PluginDescription otherFormat(first);
    otherFormat.pluginFormatName = "VST";
    require(PluginState::key("state", first) != PluginState::key("state", otherFormat), "Plugin formats share settings");
    require(settings.getValue(PluginState::key("state", first)) == "old shared state"
            && settings.getValue(PluginState::key("state", second)) == "old shared state",
            "Legacy collision migration lost existing settings");
    settings.setValue(PluginState::key("state", first), "independent first state");
    require(settings.getValue(PluginState::key("state", second)) == "old shared state", "Plugin settings are not independent");
    require(settings.getBoolValue(PluginState::key("bypass", first))
            && settings.getIntValue(PluginState::key("order", first)) == 7
            && settings.getValue(PluginState::key("display-name", first)) == "Custom name", "Legacy settings were not preserved");
    settings.removeValue(PluginState::key("state", first));
    PluginState::migrateLegacySettings(settings, plugins);
    require(!settings.containsKey(PluginState::key("state", first)), "Deleted state was resurrected from a legacy key");

    PluginDescription moved(second);
    moved.fileOrIdentifier = "/another-machine/second.vst3";
    settings.setValue(PluginState::key("display-name", second), "Portable name");
    PluginState::remapSettings(settings, second, moved);
    require(!settings.containsKey(PluginState::key("state", second))
            && settings.getValue(PluginState::key("state", moved)) == "old shared state"
            && settings.getValue(PluginState::key("display-name", moved)) == "Portable name", "Restored paths lost plugin settings");
    const String beforeUpdate = PluginState::key("state", moved);
    moved.name = "Updated Name";
    moved.version = "2.0";
    require(PluginState::key("state", moved) == beforeUpdate, "Updating metadata lost plugin identity");

    PropertySet alreadyMigrated;
    alreadyMigrated.setValue(PluginState::legacyKey("state", first), "obsolete");
    alreadyMigrated.setValue(PluginState::key("state", first), "current");
    PluginState::migrateLegacySettings(alreadyMigrated, { first });
    require(alreadyMigrated.getValue(PluginState::key("state", first)) == "current", "Migration overwrote newer state");

    PropertySet legacyBackup;
    legacyBackup.setValue(PluginState::legacyKey("state", second), "legacy backup state");
    legacyBackup.setValue(PluginState::legacyKey("display-name", second), "Legacy portable name");
    PluginDescription restored(second);
    restored.fileOrIdentifier = "/restored/second.vst3";
    PluginState::remapSettings(legacyBackup, second, restored);
    PluginState::migrateLegacySettings(legacyBackup, { restored });
    require(legacyBackup.getValue(PluginState::key("state", restored)) == "legacy backup state"
            && legacyBackup.getValue(PluginState::key("display-name", restored)) == "Legacy portable name",
            "An original backup lost state or display names after moving paths");
}

void checkBackupRoundTrip(const File& root, const String& name, bool bundle, bool legacy)
{
    const File original = root.getChildFile("source/" + name);
    if (bundle)
    {
        require(original.getChildFile("Contents").createDirectory().wasOk(), "Could not create bundle fixture");
        require(original.getChildFile("Contents/binary").replaceWithText("bundle data"), "Could not write bundle fixture");
    }
    else
    {
        require(original.getParentDirectory().createDirectory().wasOk(), "Could not create file fixture");
        require(original.replaceWithText("file data"), "Could not write file fixture");
    }
    ZipFile::Builder builder;
    require(PluginBackup::addFiles(builder, original, "plugins/0"), "Could not archive plugin fixture");
    MemoryOutputStream bytes;
    require(builder.writeToStream(bytes, nullptr), "Could not write fixture ZIP");
    MemoryInputStream input(bytes.getData(), bytes.getDataSize(), false);
    ZipFile archive(input);
    XmlElement manifest("PLUGIN");
    if (!legacy)
        manifest.setAttribute("isDirectory", bundle);
    const bool directory = PluginBackup::isDirectoryArchive(manifest, archive, "plugins/0/", name);
    require(directory == bundle, "Wrong layout inferred for backup");
    const File destination = root.getChildFile(legacy ? "legacy/" + name : "current/" + name);
    for (int i = 0; i < archive.getNumEntries(); ++i)
    {
        const String relative = archive.getEntry(i)->filename.substring(String("plugins/0/").length());
        const File target = PluginBackup::extractionTarget(destination, relative, directory);
        require(target != File() && target.getParentDirectory().createDirectory().wasOk(), "Invalid restore target");
        ScopedPointer<InputStream> stream(archive.createStreamForEntry(i));
        FileOutputStream output(target);
        require(stream != nullptr && !output.failedToOpen(), "Could not extract fixture");
        require(output.writeFromInputStream(*stream, -1) == archive.getEntry(i)->uncompressedSize, "Incomplete restore");
        output.flush();
        require(output.getStatus().wasOk(), "Restore write failed");
    }
    if (bundle)
        require(destination.isDirectory() && destination.getChildFile("Contents/binary").loadFileAsString() == "bundle data", "Bundle restore failed");
    else
        require(destination.existsAsFile() && destination.loadFileAsString() == "file data", "Single-file restore nested the binary in a directory");
    require(PluginBackup::extractionTarget(destination, "../escape", directory) == File(), "Unsafe archive path accepted");
}

void checkBackups()
{
    const File oldApp = File::getSpecialLocation(File::tempDirectory).getChildFile("old-app");
    const File newApp = oldApp.getSiblingFile("new-app");
    PluginDescription portable;
    portable.name = "Portable";
    portable.pluginFormatName = "VST3";
    portable.uid = 42;
    portable.fileOrIdentifier = oldApp.getChildFile("VST Plugins/import/Portable.vst3").getFullPathName();
    KnownPluginList portableList;
    portableList.addType(portable);
    ScopedPointer<XmlElement> portableXml(portableList.createXml());
    PropertySet portableSettings;
    portableSettings.setValue("pluginList", portableXml);
    portableSettings.setValue("pluginListActive", portableXml);
    portableSettings.setValue("portableAppDirectory", oldApp.getFullPathName());
    portableSettings.setValue("lastPluginScanPath_VST3", oldApp.getChildFile("VST Plugins/import").getFullPathName());
    for (const char* type : { "state", "order", "bypass", "display-name" })
        portableSettings.setValue(PluginState::key(type, portable), "preserved");
    PluginState::relocatePortableSettings(portableSettings, newApp);
    PluginDescription relocated(portable);
    relocated.fileOrIdentifier = newApp.getChildFile("VST Plugins/import/Portable.vst3").getFullPathName();
    for (const char* type : { "state", "order", "bypass", "display-name" })
        require(portableSettings.getValue(PluginState::key(type, relocated)) == "preserved"
                && !portableSettings.containsKey(PluginState::key(type, portable)),
                "Moving app directory lost plugin settings");
    for (const char* listKey : { "pluginList", "pluginListActive" })
    {
        ScopedPointer<XmlElement> xml(XmlDocument::parse(portableSettings.getValue(listKey)));
        KnownPluginList movedList;
        movedList.recreateFromXml(*xml);
        require(movedList.getNumTypes() == 1
                && movedList.getType(0)->fileOrIdentifier == relocated.fileOrIdentifier,
                "Moving app directory did not relocate a plugin list");
    }
    require(portableSettings.getValue("lastPluginScanPath_VST3")
            == newApp.getChildFile("VST Plugins/import").getFullPathName(), "Scan path did not follow executable");
    PluginState::relocatePortableSettings(portableSettings, newApp);
    require(portableSettings.getValue(PluginState::key("state", relocated)) == "preserved",
            "Repeated portable startup lost state");
    PropertySet settings;
    require(PluginBackup::hasValidPluginLists(settings), "Empty installation rejected");
    settings.setValue("pluginListActive", "broken XML");
    require(!PluginBackup::hasValidPluginLists(settings), "Broken active list accepted");
    settings.setValue("pluginListActive", "<PROPERTIES/>");
    require(!PluginBackup::hasValidPluginLists(settings), "Wrong list root accepted");
    settings.setValue("pluginListActive", "<KNOWNPLUGINS/>");
    settings.setValue("icon", "white");
    settings.setValue("plugin-state-example", "binary+/= data");
    ScopedPointer<XmlElement> saved(settings.createXml("PROPERTIES"));
    ScopedPointer<XmlElement> parsed(XmlDocument::parse(saved->createDocument(String(), true)));
    PropertySet restored;
    require(parsed != nullptr, "Settings XML round trip failed");
    restored.restoreFromXml(*parsed);
    require(PluginBackup::hasValidPluginLists(restored)
            && restored.getValue("icon") == "white"
            && restored.getValue("plugin-state-example") == "binary+/= data",
            "Settings values lost in XML round trip");
    require(PluginBackup::supportsVersion(1) && PluginBackup::supportsVersion(2)
            && !PluginBackup::supportsVersion(3), "Backup version compatibility regressed");
    const File parent = File::getSpecialLocation(File::tempDirectory);
    const File root = parent.getNonexistentChildFile("LightHost-backup-tests", "", false);
    require(root.isAChildOf(parent) && root.createDirectory().wasOk(), "Invalid temporary test directory");
    for (bool legacy : { false, true })
    {
        checkBackupRoundTrip(root, "Example.dll", false, legacy);
        checkBackupRoundTrip(root, "Example.vst3", false, legacy);
        checkBackupRoundTrip(root, "Bundle.vst", true, legacy);
    }
    const File missing = root.getChildFile("missing/Example.dll");
    const File restoreRoot = root.getChildFile("restored");
    FileSearchPath paths;
    paths.add(root.getChildFile("system-folder"));
    const File fresh = PluginBackup::restoreDestination(missing, paths, restoreRoot);
    require(fresh.isAChildOf(restoreRoot) && fresh.getFileName() == "Example.dll",
            "Missing plugins must restore outside system scan folders");
    require(fresh.getParentDirectory().createDirectory().wasOk()
            && fresh.replaceWithText("partial"), "Could not create interrupted restore fixture");
    require(PluginBackup::restoreDestination(missing, paths, restoreRoot) != fresh,
            "Interrupted restore reused as a complete installation");
    const File installed = root.getChildFile("source/Example.dll");
    require(PluginBackup::restoreDestination(installed, paths, restoreRoot) == installed,
            "Original installation was not reused");
    paths.add(installed.getParentDirectory());
    require(PluginBackup::restoreDestination(missing, paths, restoreRoot) == installed,
            "Installation in another scan folder was not reused");
    require(!PluginBackup::isSafePath("folder/.. /escape")
            && !PluginBackup::isSafePath("/absolute")
            && !PluginBackup::isSafePath("C:/absolute"), "Unsafe path validation regressed");
    require(root.isAChildOf(parent) && root.deleteRecursively(), "Could not clean up the owned test directory");
}

void checkUnsupportedLayoutAndEditor()
{
    AudioProcessorGraph graph;
    graph.setPlayConfigDetails(2, 2, 48000, 64);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), PluginChain::inputNodeId);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(
        AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), PluginChain::outputNodeId);
    auto* mono = new GainProcessor(2.0f);
    mono->setPlayConfigDetails(1, 1, 48000, 64);
    const auto node = graph.addNode(mono, 1);
    const StringArray issues = PluginChain::reconnect(graph, 1, [](int) { return false; });
    require(!issues.isEmpty(), "Unsupported layout was silently accepted");
    checkAudio(graph, 0.25f);
    require(mono->calls == 0 && mono->isSuspended(), "Unsupported plugin was still processed");
    PluginWindow* first = PluginWindow::getWindowFor(node, PluginWindow::Normal);
    require(first != nullptr && !first->isVisible(), "Editor must be constructed before presentation");
    PluginWindow* second = PluginWindow::getWindowFor(node, PluginWindow::Normal);
    const bool reused = first == second;
    if (!reused)
        delete second;
    delete first;
    require(reused && !PluginWindow::containsActiveWindows(), "Fallback editor was duplicated or leaked");
    graph.releaseResources();
}

class StateCaptureProcessor : public GainProcessor
{
public:
    StateCaptureProcessor() : GainProcessor(1.0f) { delayMs = 40; }
    void processBlock(AudioBuffer<float>& audio, MidiBuffer& midi) override
    {
        processing.store(true);
        entered.signal();
        GainProcessor::processBlock(audio, midi);
        processing.store(false);
    }
    void getStateInformation(MemoryBlock& state) override
    {
        overlapped = processing.load();
        state.append("saved", 5);
    }
    WaitableEvent entered;
    std::atomic<bool> processing { false };
    bool overlapped = false;
};

void checkSynchronizedStateCapture()
{
    AudioProcessorGraph graph;
    StateCaptureProcessor processor;
    std::thread audio([&]
    {
        const ScopedLock graphGuard(graph.getCallbackLock());
        const ScopedLock callbackGuard(processor.getCallbackLock());
        AudioBuffer<float> buffer(2, 64);
        buffer.clear();
        MidiBuffer midi;
        processor.processBlock(buffer, midi);
    });
    const bool entered = processor.entered.wait(2000);
    MemoryBlock state;
    if (entered)
        PluginState::capture(graph, processor, state);
    audio.join();
    require(entered && !processor.overlapped && state.getSize() == 5,
            "State capture overlapped processing or lost the snapshot");
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
        checkIdentityAndMigration();
        checkBackups();
        checkUnsupportedLayoutAndEditor();
        checkSynchronizedStateCapture();
        std::cout << "PASS: stereo routing, bypass CPU work, resume, instance preservation, missing plugins\n";
        std::cout << "PASS: stereo channel migration, invalid-output containment, device diagnostics\n";
        std::cout << "PASS: plugin identity migration, backup round trips, active-only backups, editor reuse, unsupported layouts, state synchronization\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
