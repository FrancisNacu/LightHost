#ifndef LIGHTHOST_PLUGIN_BACKUP_H
#define LIGHTHOST_PLUGIN_BACKUP_H

namespace PluginBackup
{
inline bool supportsVersion(int version) { return version == 1 || version == 2; }

inline File restoreDestination(const File& source, const FileSearchPath& scanPaths,
                               const File& restoreRoot)
{
    if (source.exists())
        return source;
    for (int i = 0; i < scanPaths.getNumPaths(); ++i)
    {
        const File candidate = scanPaths[i].getChildFile(source.getFileName());
        if (candidate.exists())
            return candidate;
    }
    // Keep missing binaries out of protected system folders. Use a fresh
    // container so an interrupted restore cannot be mistaken for an installation.
    return restoreRoot.getNonexistentChildFile("import", "", false)
        .getChildFile(source.getFileName());
}

inline bool hasValidPluginLists(const PropertySet& settings)
{
    for (const char* key : { "pluginList", "pluginListActive" })
    {
        if (!settings.containsKey(key))
            continue; // Empty installations need not have saved a list yet.
        ScopedPointer<XmlElement> list(XmlDocument::parse(settings.getValue(key)));
        if (list == nullptr || !list->hasTagName("KNOWNPLUGINS"))
            return false;
        forEachXmlChildElementWithTagName(*list, plugin, "PLUGIN")
        {
            PluginDescription description;
            if (!description.loadFromXml(*plugin) || description.fileOrIdentifier.isEmpty())
                return false;
        }
    }
    return true;
}

inline bool addFiles(ZipFile::Builder& archive, const File& pluginFile, const String& archiveRoot)
{
    Array<File> files;
    const bool directory = pluginFile.isDirectory();
    if (directory)
        pluginFile.findChildFiles(files, File::findFiles, true);
    else if (pluginFile.existsAsFile())
        files.add(pluginFile);
    if (files.isEmpty())
        return false;
    for (const auto& file : files)
    {
        const String relative = directory
            ? file.getRelativePathFrom(pluginFile).replaceCharacter('\\', '/') : file.getFileName();
        archive.addFile(file, 6, archiveRoot + "/" + relative);
    }
    return true;
}

inline bool isSafePath(const String& path)
{
    if (path.isEmpty() || path.startsWithChar('/') || path.containsChar('\\') || path.containsChar(':'))
        return false;
    StringArray components;
    components.addTokens(path, "/", "");
    for (const auto& component : components)
        if (component.isEmpty() || component == "." || component == ".."
            || component.trimEnd() != component || component.endsWithChar('.'))
            return false;
    return true;
}

inline bool isDirectoryArchive(const XmlElement& manifest, const ZipFile& archive,
                               const String& prefix, const String& fileName)
{
    if (manifest.hasAttribute("isDirectory"))
        return manifest.getBoolAttribute("isDirectory");
    // Original backups had no layout flag. A single entry named after the
    // source binary is the legacy representation of a file, not a bundle.
    int count = 0;
    bool matchingFile = false;
    for (int i = 0; i < archive.getNumEntries(); ++i)
        if (const auto* entry = archive.getEntry(i))
            if (entry->filename.startsWith(prefix))
            {
                ++count;
                matchingFile = entry->filename == prefix + fileName;
            }
    return !(count == 1 && matchingFile);
}

inline File extractionTarget(const File& plugin, const String& relativePath, bool directory)
{
    if (!isSafePath(relativePath))
        return File();
    if (!directory)
        return relativePath == plugin.getFileName() ? plugin : File();
    const File target = plugin.getChildFile(relativePath);
    return target.isAChildOf(plugin) ? target : File();
}
}

#endif
