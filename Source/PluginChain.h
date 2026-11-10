#ifndef LIGHTHOST_PLUGIN_CHAIN_H
#define LIGHTHOST_PLUGIN_CHAIN_H

namespace PluginChain
{
const uint32 inputNodeId = 1000000;
const uint32 outputNodeId = inputNodeId + 1;

inline bool supportsStereo(const AudioProcessor& processor)
{
    return processor.getTotalNumInputChannels() >= 2 && processor.getTotalNumOutputChannels() >= 2;
}

inline bool connectStereo(AudioProcessorGraph& graph, uint32 sourceId, uint32 destinationId)
{
    const auto source = graph.getNodeForId(sourceId);
    const auto destination = graph.getNodeForId(destinationId);
    if (source == nullptr || destination == nullptr)
        return false;
    const int outputs = source->getProcessor()->getTotalNumOutputChannels();
    const int inputs = destination->getProcessor()->getTotalNumInputChannels();
    for (int channel = 0; channel < jmin(2, inputs); ++channel)
        if (outputs > 0 && !graph.addConnection(sourceId, jmin(channel, outputs - 1), destinationId, channel))
            return false;
    return true;
}

// Plugin node IDs correspond to their one-based positions in the active list.
// Missing nodes represent plugins that could not be loaded.
template <typename IsBypassed>
StringArray reconnect(AudioProcessorGraph& graph, int pluginCount, IsBypassed isBypassed)
{
    StringArray issues;
    bool routingFailed = false;
    for (int i = graph.getNumConnections(); --i >= 0;)
        graph.removeConnection(i);

    uint32 previousNodeId = inputNodeId;
    for (int i = 0; i < pluginCount; ++i)
    {
        const auto node = graph.getNodeForId(i + 1);
        if (node == nullptr)
            continue;

        const bool supported = supportsStereo(*node->getProcessor());
        if (!supported)
            issues.add("Plugin node " + String(i + 1) + " does not support stereo input and output.");
        const bool bypassed = isBypassed(i) || !supported;
        // This JUCE version processes disconnected nodes, so suspend them too.
        node->getProcessor()->suspendProcessing(bypassed);
        if (bypassed)
            continue;

        if (!connectStereo(graph, previousNodeId, node->nodeId))
        {
            issues.add("Could not connect plugin node " + String(i + 1) + ".");
            routingFailed = true;
            break;
        }
        previousNodeId = node->nodeId;
    }

    // Empty, bypassed, and unavailable chains all provide stereo passthrough.
    if (!connectStereo(graph, previousNodeId, outputNodeId))
    {
        issues.add("Could not connect the audio output.");
        routingFailed = true;
    }
    if (routingFailed)
    {
        // Never leave half of a stereo connection active after a routing failure.
        for (int i = graph.getNumConnections(); --i >= 0;)
            graph.removeConnection(i);
        for (int i = 0; i < pluginCount; ++i)
            if (const auto node = graph.getNodeForId(i + 1))
                node->getProcessor()->suspendProcessing(true);
        if (!connectStereo(graph, inputNodeId, outputNodeId))
            issues.add("Audio passthrough is unavailable for the selected device channels.");
    }
    return issues;
}
}

#endif
