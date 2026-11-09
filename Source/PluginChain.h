#ifndef LIGHTHOST_PLUGIN_CHAIN_H
#define LIGHTHOST_PLUGIN_CHAIN_H

namespace PluginChain
{
const uint32 inputNodeId = 1000000;
const uint32 outputNodeId = inputNodeId + 1;

// Plugin node IDs correspond to their one-based positions in the active list.
// Missing nodes represent plugins that could not be loaded.
template <typename IsBypassed>
void reconnect(AudioProcessorGraph& graph, int pluginCount, IsBypassed isBypassed)
{
    for (int i = graph.getNumConnections(); --i >= 0;)
        graph.removeConnection(i);

    uint32 previousNodeId = inputNodeId;
    for (int i = 0; i < pluginCount; ++i)
    {
        const auto node = graph.getNodeForId(i + 1);
        if (node == nullptr)
            continue;

        const bool bypassed = isBypassed(i);
        // This JUCE version processes disconnected nodes, so suspend them too.
        node->getProcessor()->suspendProcessing(bypassed);
        if (bypassed)
            continue;

        for (int channel = 0; channel < 2; ++channel)
            graph.addConnection(previousNodeId, channel, node->nodeId, channel);
        previousNodeId = node->nodeId;
    }

    // Empty, bypassed, and unavailable chains all provide stereo passthrough.
    for (int channel = 0; channel < 2; ++channel)
        graph.addConnection(previousNodeId, channel, outputNodeId, channel);
}
}

#endif
