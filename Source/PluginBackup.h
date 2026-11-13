#ifndef LIGHTHOST_PLUGIN_BACKUP_H
#define LIGHTHOST_PLUGIN_BACKUP_H

namespace PluginBackup
{
// The worker owns snapshots only; plugins and settings stay on the message thread.
class ExportTask : public ThreadWithProgressWindow
{
public:
    explicit ExportTask(const File& output)
        : ThreadWithProgressWindow("Export Backup", true, true, -1), destination(output)
    {
        setProgress(-1.0);
    }

    ~ExportTask() override { stopThread(-1); }

    Array<File> plugins;
    String settingsText, manifestText, error;
    bool succeeded = false;

    void run() override
    {
        ZipFile::Builder archive;
        for (int i = 0; i < plugins.size(); ++i)
        {
            if (threadShouldExit())
                return;
            const File plugin = plugins[i];
            const String root = "plugins/" + String(i) + "/";
            setStatusMessage("Reading " + plugin.getFileName());
            bool added = false;
            if (plugin.isDirectory())
            {
                DirectoryIterator iterator(plugin, true, "*", File::findFiles);
                while (iterator.next())
                {
                    if (threadShouldExit())
                        return;
                    const File file = iterator.getFile();
                    if (!addFile(archive, file, root + file.getRelativePathFrom(plugin).replaceCharacter('\\', '/')))
                        return;
                    added = true;
                }
            }
            else
                added = addFile(archive, plugin, root + plugin.getFileName());
            if (!added)
            {
                error = "Could not read the plugin file " + plugin.getFullPathName();
                return;
            }
        }
        archive.addEntry(new MemoryInputStream(settingsText.toRawUTF8(), settingsText.getNumBytesAsUTF8(), true),
                         6, "settings.xml", Time::getCurrentTime());
        archive.addEntry(new MemoryInputStream(manifestText.toRawUTF8(), manifestText.getNumBytesAsUTF8(), true),
                         6, "manifest.xml", Time::getCurrentTime());
        setStatusMessage("Compressing backup...");
        FileOutputStream output(destination);
        if (!output.failedToOpen() && !threadShouldExit())
        {
            const bool written = archive.writeToStream(output, nullptr);
            output.flush();
            succeeded = written && !threadShouldExit() && !output.getStatus().failed();
        }
        if (!succeeded && !threadShouldExit())
            error = "Could not write the backup archive.";
    }

private:
    // This JUCE ZIP builder compresses each entry in memory before writing it.
    // Check cancellation while reading, so even a large entry can be cancelled.
    class FileSource : public InputStream
    {
    public:
        FileSource(const File& file, Thread& task) : source(file), worker(task) {}
        int64 getTotalLength() override { return source.getSize(); }
        int64 getPosition() override { return input == nullptr ? 0 : input->getPosition(); }
        bool setPosition(int64 position) override { return open() && input->setPosition(position); }
        bool isExhausted() override { return input != nullptr && input->isExhausted(); }
        int read(void* data, int bytes) override
        {
            return open() ? input->read(data, bytes) : -1;
        }
    private:
        bool open()
        {
            if (worker.threadShouldExit())
                return false;
            if (input == nullptr)
                input = source.createInputStream();
            return input != nullptr;
        }
        File source;
        Thread& worker;
        ScopedPointer<FileInputStream> input;
    };

    bool addFile(ZipFile::Builder& archive, const File& file, const String& path)
    {
        // Open lazily: large bundles must not keep thousands of handles open.
        if (!file.existsAsFile())
        {
            error = "Could not read " + file.getFullPathName();
            return false;
        }
        archive.addEntry(new FileSource(file, *this), 6, path, file.getLastModificationTime());
        return true;
    }

    File destination;
};

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
