#ifndef LIGHTHOST_PLUGIN_STATE_H
#define LIGHTHOST_PLUGIN_STATE_H

#include <vector>

namespace PluginState
{
inline String encode(const String& value)
{
    return MemoryBlock(value.toRawUTF8(), value.getNumBytesAsUTF8()).toBase64Encoding();
}

inline String key(const String& type, const PluginDescription& plugin)
{
    // Use full identity rather than a path hash. Name/version changes retain settings.
    return "plugin-v2-" + type.toLowerCase() + "-" + encode(plugin.pluginFormatName)
        + "-" + String::toHexString(plugin.uid) + "-" + encode(plugin.fileOrIdentifier);
}

inline String legacyKey(const String& type, const PluginDescription& plugin)
{
    if (type == "display-name")
        return "plugin-display-name-" + plugin.createIdentifierString();
    return "plugin-" + type + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
}

inline std::vector<PluginDescription> collectPlugins(const KnownPluginList& known,
                                                      const KnownPluginList& active)
{
    std::vector<PluginDescription> result;
    for (const auto* list : { &known, &active })
        for (int i = 0; i < list->getNumTypes(); ++i)
        {
            const PluginDescription& plugin = *list->getType(i);
            bool found = false;
            for (auto& existing : result)
                if (existing.isDuplicateOf(plugin) && existing.pluginFormatName == plugin.pluginFormatName)
                {
                    existing = plugin; // Prefer the active instance's metadata.
                    found = true;
                    break;
                }
            if (!found)
                result.push_back(plugin);
        }
    return result;
}

inline void migrateLegacySettings(PropertySet& settings, const std::vector<PluginDescription>& plugins)
{
    // Copy all entries before removing legacy keys: historic collisions must not
    // prevent the other plugin from retaining its last shared setting.
    for (const auto& plugin : plugins)
        for (const char* type : { "state", "bypass", "order", "display-name" })
        {
            const String oldKey = legacyKey(type, plugin);
            const String newKey = key(type, plugin);
            if (!settings.containsKey(newKey) && settings.containsKey(oldKey))
                settings.setValue(newKey, settings.getValue(oldKey));
        }
    for (const auto& plugin : plugins)
        for (const char* type : { "state", "bypass", "order", "display-name" })
            settings.removeValue(legacyKey(type, plugin));
}

inline void remapSettings(PropertySet& settings, const PluginDescription& before,
                          const PluginDescription& after)
{
    for (const char* type : { "state", "bypass", "order", "display-name" })
        for (bool legacy : { false, true })
        {
            const String oldKey = legacy ? legacyKey(type, before) : key(type, before);
            const String newKey = legacy ? legacyKey(type, after) : key(type, after);
            if (oldKey != newKey && settings.containsKey(oldKey))
            {
                settings.setValue(newKey, settings.getValue(oldKey));
                settings.removeValue(oldKey);
            }
        }
}

inline void capture(AudioProcessorGraph& graph, AudioProcessor& processor, MemoryBlock& state)
{
    // Match the audio callback's graph -> processor lock order, including plugins
    // that synchronously notify the host while their state is being captured.
    // Keep encoding and settings-file I/O outside this scope.
    const ScopedLock graphGuard(graph.getCallbackLock());
    const ScopedLock callbackGuard(processor.getCallbackLock());
    processor.getStateInformation(state);
}
}

#endif
