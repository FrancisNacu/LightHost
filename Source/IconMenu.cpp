//
//  IconMenu.cpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"
#include "PluginWindow.h"
#include <ctime>
#include <limits.h>
#include <string>
#if JUCE_WINDOWS
#include "Windows.h"
#endif

namespace
{
bool addPluginFilesToArchive(ZipFile::Builder& archive, const File& pluginFile,
	const String& archiveRoot)
{
	Array<File> files;
	if (pluginFile.isDirectory())
		pluginFile.findChildFiles(files, File::findFiles, true);
	else if (pluginFile.existsAsFile())
		files.add(pluginFile);

	if (files.isEmpty())
		return false;

	for (int i = 0; i < files.size(); ++i)
	{
		const String relativePath = pluginFile.isDirectory()
			? files[i].getRelativePathFrom(pluginFile).replaceCharacter('\\', '/')
			: files[i].getFileName();
		archive.addFile(files[i], 6, archiveRoot + "/" + relativePath);
	}

	return true;
}

bool isSafePluginArchivePath(const String& path)
{
	if (path.isEmpty() || path.startsWithChar('/') || path.containsChar('\\')
		|| path.containsChar(':'))
		return false;

	StringArray components;
	components.addTokens(path, "/", "");
	for (int i = 0; i < components.size(); ++i)
		if (components[i].isEmpty() || components[i] == "." || components[i] == "..")
			return false;

	return true;
}

void remapPluginPaths(PropertySet& settings, const String& key,
	const StringArray& oldPaths, const StringArray& newPaths)
{
	ScopedPointer<XmlElement> pluginList(XmlDocument::parse(settings.getValue(key)));
	if (pluginList == nullptr)
		return;

	forEachXmlChildElementWithTagName(*pluginList, plugin, "PLUGIN")
	{
		const String oldPath = plugin->getStringAttribute("file");
		for (int i = 0; i < oldPaths.size(); ++i)
			if (oldPath.equalsIgnoreCase(oldPaths[i]))
			{
				plugin->setAttribute("file", newPaths[i]);
				break;
			}
	}

	settings.setValue(key, pluginList->createDocument(String(), true));
}

#if JUCE_WINDOWS
const wchar_t* const startupRegistryPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const startupRegistryValue = L"Light Host";

void showStartupRegistryError(LSTATUS error)
{
	AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Run on startup",
		"Could not access or update the Windows startup setting. Error code: "
		+ String((int) error));
}

bool getRunOnStartupEnabled(bool& enabled)
{
	enabled = false;
	HKEY key = nullptr;
	LSTATUS result = RegOpenKeyExW(HKEY_CURRENT_USER, startupRegistryPath, 0,
		KEY_QUERY_VALUE, &key);
	if (result == ERROR_FILE_NOT_FOUND)
		return true;
	if (result != ERROR_SUCCESS)
	{
		showStartupRegistryError(result);
		return false;
	}

	DWORD type = 0;
	DWORD size = 0;
	result = RegQueryValueExW(key, startupRegistryValue, nullptr, &type, nullptr, &size);
	RegCloseKey(key);
	if (result == ERROR_FILE_NOT_FOUND)
		return true;
	if (result != ERROR_SUCCESS)
	{
		showStartupRegistryError(result);
		return false;
	}

	enabled = true;
	return true;
}

void toggleRunOnStartup()
{
	bool enabled = false;
	if (!getRunOnStartupEnabled(enabled))
		return;

	HKEY key = nullptr;
	DWORD disposition = 0;
	LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, startupRegistryPath, 0, nullptr,
		REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, &disposition);
	if (result != ERROR_SUCCESS)
	{
		showStartupRegistryError(result);
		return;
	}

	if (enabled)
		result = RegDeleteValueW(key, startupRegistryValue);
	else
	{
		const String executablePath =
			File::getSpecialLocation(File::currentExecutableFile).getFullPathName();
		if (executablePath.isEmpty())
		{
			RegCloseKey(key);
			AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Run on startup",
				"Could not determine the Light Host executable path.");
			return;
		}

		std::wstring commandLine(executablePath.toWideCharPointer());
		commandLine.insert(commandLine.begin(), L'"');
		commandLine.push_back(L'"');
		result = RegSetValueExW(key, startupRegistryValue, 0, REG_SZ,
			reinterpret_cast<const BYTE*>(commandLine.c_str()),
			static_cast<DWORD>((commandLine.length() + 1) * sizeof(wchar_t)));
	}

	RegCloseKey(key);
	if (result != ERROR_SUCCESS && !(enabled && result == ERROR_FILE_NOT_FOUND))
		showStartupRegistryError(result);
}
#endif
}

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
	PluginListWindow(IconMenu& owner_, AudioPluginFormatManager& pluginFormatManager)
		: DocumentWindow("Available Plugins", Colours::white,
			DocumentWindow::minimiseButton | DocumentWindow::closeButton),
		owner(owner_)
	{
		const File deadMansPedalFile(getAppProperties().getUserSettings()
			->getFile().getSiblingFile("RecentlyCrashedPluginsList"));

		setContentOwned(new PluginListComponent(pluginFormatManager,
			owner.knownPluginList,
			deadMansPedalFile,
			getAppProperties().getUserSettings()), true);

		setUsingNativeTitleBar(true);
		setResizable(true, false);
		setResizeLimits(300, 400, 800, 1500);
		setTopLeftPosition(60, 60);

		restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("listWindowPos"));
		setVisible(true);
	}

	~PluginListWindow()
	{
		if (!owner.settingsImported)
			getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());

		clearContentComponent();
	}

	void closeButtonPressed()
	{
        owner.removePluginsLackingInputOutput();
        #if JUCE_MAC
        Process::setDockIconVisible(false);
        #endif
		owner.pluginListWindow = nullptr;
	}

private:
	IconMenu& owner;
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000), INDEX_MOVE_UP(4000000), INDEX_MOVE_DOWN(5000000)
{
    // Initiialization
	settingsImported = false;
    formatManager.addDefaultFormats();
	#if JUCE_WINDOWS
	x = y = 0;
	#endif
    // Audio device
    ScopedPointer<XmlElement> savedAudioState (getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    deviceManager.initialise(256, 256, savedAudioState, true);
    player.setProcessor(&graph);
    deviceManager.addAudioCallback(&player);
    // Plugins - all
    ScopedPointer<XmlElement> savedPluginList(getAppProperties().getUserSettings()->getXmlValue("pluginList"));
    if (savedPluginList != nullptr)
        knownPluginList.recreateFromXml(*savedPluginList);
    pluginSortMethod = KnownPluginList::sortByManufacturer;
    knownPluginList.addChangeListener(this);
    // Plugins - active
    ScopedPointer<XmlElement> savedPluginListActive(getAppProperties().getUserSettings()->getXmlValue("pluginListActive"));
    if (savedPluginListActive != nullptr)
        activePluginList.recreateFromXml(*savedPluginListActive);
    loadActivePlugins();
    activePluginList.addChangeListener(this);
	setIcon();
	setIconTooltip(JUCEApplication::getInstance()->getApplicationName());
};

IconMenu::~IconMenu()
{
	savePluginStates();
}

void IconMenu::setIcon()
{
	// Set menu icon
	#if JUCE_MAC
		if (exec("defaults read -g AppleInterfaceStyle").compare("Dark") == 1)
			setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize));
		else
			setIconImage(ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
	#else
		String defaultColor;
	#if JUCE_WINDOWS
		defaultColor = "white";
	#elif JUCE_LINUX
		defaultColor = "black";
	#endif
		if (!getAppProperties().getUserSettings()->containsKey("icon"))
			getAppProperties().getUserSettings()->setValue("icon", defaultColor);
		String color = getAppProperties().getUserSettings()->getValue("icon");
		Image icon;
		if (color.equalsIgnoreCase("white"))
			icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_white_png, BinaryData::menu_icon_white_pngSize);
		else if (color.equalsIgnoreCase("black"))
			icon = ImageFileFormat::loadFrom(BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize);
		setIconImage(icon);
	#endif
}

void IconMenu::loadActivePlugins()
{
	const int INPUT = 1000000;
	const int OUTPUT = INPUT + 1;
	const int CHANNEL_ONE = 0;
	const int CHANNEL_TWO = 1;
	PluginWindow::closeAllCurrentlyOpenWindows();
    graph.clear();
    inputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), INPUT);
    outputNode = graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), OUTPUT);
    if (activePluginList.getNumTypes() == 0)
    {
        graph.addConnection(INPUT, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
        graph.addConnection(INPUT, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
    }
	int pluginTime = 0;
	int lastId = 0;
	bool hasInputConnected = false;
	// NOTE: Node ids cannot begin at 0.
    for (int i = 1; i <= activePluginList.getNumTypes(); i++)
    {
        PluginDescription plugin = getNextPluginOlderThanTime(pluginTime);
        String errorMessage;
        AudioPluginInstance* instance = formatManager.createPluginInstance(plugin, graph.getSampleRate(), graph.getBlockSize(), errorMessage);
		String pluginUid = getKey("state", plugin);
        String savedPluginState = getAppProperties().getUserSettings()->getValue(pluginUid);
        MemoryBlock savedPluginBinary;
        savedPluginBinary.fromBase64Encoding(savedPluginState);
        instance->setStateInformation(savedPluginBinary.getData(), savedPluginBinary.getSize());
        graph.addNode(instance, i);
		String key = getKey("bypass", plugin);
		bool bypass = getAppProperties().getUserSettings()->getBoolValue(key, false);
        // Input to plugin
        if ((!hasInputConnected) && (!bypass))
        {
            graph.addConnection(INPUT, CHANNEL_ONE, i, CHANNEL_ONE);
            graph.addConnection(INPUT, CHANNEL_TWO, i, CHANNEL_TWO);
			hasInputConnected = true;
        }
        // Connect previous plugin to current
        else if (!bypass)
        {
            graph.addConnection(lastId, CHANNEL_ONE, i, CHANNEL_ONE);
            graph.addConnection(lastId, CHANNEL_TWO, i, CHANNEL_TWO);
        }
		if (!bypass)
			lastId = i;
    }
	if (lastId > 0)
	{
		// Last active plugin to output
		graph.addConnection(lastId, CHANNEL_ONE, OUTPUT, CHANNEL_ONE);
		graph.addConnection(lastId, CHANNEL_TWO, OUTPUT, CHANNEL_TWO);
	}
}

PluginDescription IconMenu::getNextPluginOlderThanTime(int &time)
{
	int timeStatic = time;
	PluginDescription closest;
	int diff = INT_MAX;
	for (int i = 0; i < activePluginList.getNumTypes(); i++)
	{
		PluginDescription plugin = *activePluginList.getType(i);
		String key = getKey("order", plugin);
		String pluginTimeString = getAppProperties().getUserSettings()->getValue(key);
		int pluginTime = atoi(pluginTimeString.toStdString().c_str());
		if (pluginTime > timeStatic && abs(timeStatic - pluginTime) < diff)
		{
			diff = abs(timeStatic - pluginTime);
			closest = plugin;
			time = pluginTime;
		}
	}
	return closest;
}

void IconMenu::changeListenerCallback(ChangeBroadcaster* changed)
{
    if (changed == &knownPluginList)
    {
        ScopedPointer<XmlElement> savedPluginList (knownPluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginList", savedPluginList);
            getAppProperties().saveIfNeeded();
        }
    }
    else if (changed == &activePluginList)
    {
        ScopedPointer<XmlElement> savedPluginList (activePluginList.createXml());
        if (savedPluginList != nullptr)
        {
            getAppProperties().getUserSettings()->setValue ("pluginListActive", savedPluginList);
            getAppProperties().saveIfNeeded();
        }
    }
}

#if JUCE_MAC
std::string IconMenu::exec(const char* cmd)
{
    std::shared_ptr<FILE> pipe(popen(cmd, "r"), pclose);
    if (!pipe) return "ERROR";
    char buffer[128];
    std::string result = "";
    while (!feof(pipe.get()))
    {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            result += buffer;
    }
    return result;
}
#endif

void IconMenu::timerCallback()
{
    stopTimer();
    menu.clear();
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());
    if (menuIconLeftClicked) {
        menu.addItem(1, "Preferences");
        menu.addItem(2, "Edit Plugins");
        menu.addSeparator();
        menu.addItem(4, "Import Backup...");
        menu.addItem(5, "Export Backup...");
#if JUCE_WINDOWS
		bool runOnStartup = false;
		getRunOnStartupEnabled(runOnStartup);
		menu.addSeparator();
		menu.addItem(6, "Run on startup", true, runOnStartup);
#endif
        menu.addSeparator();
		menu.addSectionHeader("Active Plugins");
        // Active plugins
		int time = 0;
        for (int i = 0; i < activePluginList.getNumTypes(); i++)
        {
            PopupMenu options;
            options.addItem(INDEX_EDIT + i, "Edit");
			std::vector<PluginDescription> timeSorted = getTimeSortedList();
			String key = getKey("bypass", timeSorted[i]);
			bool bypass = getAppProperties().getUserSettings()->getBoolValue(key);
			options.addItem(INDEX_BYPASS + i, "Bypass", true, bypass);
			options.addSeparator();
			options.addItem(INDEX_MOVE_UP + i, "Move Up", i > 0);
			options.addItem(INDEX_MOVE_DOWN + i, "Move Down", i < timeSorted.size() - 1);
			options.addSeparator();
            options.addItem(INDEX_DELETE + i, "Delete");
			PluginDescription plugin = getNextPluginOlderThanTime(time);
            menu.addSubMenu(plugin.name, options);
        }
        menu.addSeparator();
		menu.addSectionHeader("Avaliable Plugins");
        // All plugins
        knownPluginList.addToMenu(menu, pluginSortMethod);
    }
    else
    {
        menu.addItem(1, "Quit");
		menu.addSeparator();
		menu.addItem(2, "Delete Plugin States");
		#if !JUCE_MAC
			menu.addItem(3, "Invert Icon Color");
		#endif
    }
	#if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
	#else
	if (x == 0 || y == 0)
	{
		POINT iconLocation;
		iconLocation.x = 0;
		iconLocation.y = 0;
		GetCursorPos(&iconLocation);
		x = iconLocation.x;
		y = iconLocation.y;
	}
	juce::Rectangle<int> rect(x, y, 1, 1);
	menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(rect), ModalCallbackFunction::forComponent(menuInvocationCallback, this));
	#endif
}

void IconMenu::mouseDown(const MouseEvent& e)
{
	#if JUCE_MAC
		Process::setDockIconVisible(true);
	#endif
    Process::makeForegroundProcess();
    menuIconLeftClicked = e.mods.isLeftButtonDown();
    startTimer(50);
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    // Right click
    if ((!im->menuIconLeftClicked))
    {
		if (id == 1)
		{
			im->savePluginStates();
			return JUCEApplication::getInstance()->quit();
		}
		if (id == 2)
		{
			im->deletePluginStates();
			return im->loadActivePlugins();
		}
		if (id == 3)
		{
			String color = getAppProperties().getUserSettings()->getValue("icon");
			getAppProperties().getUserSettings()->setValue("icon", color.equalsIgnoreCase("black") ? "white" : "black");
			return im->setIcon();
		}
    }
	if (im->menuIconLeftClicked && id == 4)
	{
		if (im->importBackup())
			JUCEApplication::getInstance()->quit();
		return;
	}
	if (im->menuIconLeftClicked && id == 5)
		return im->exportBackup();
#if JUCE_WINDOWS
	if (im->menuIconLeftClicked && id == 6)
	{
		toggleRunOnStartup();
		return;
	}
#endif
	#if JUCE_MAC
    // Click elsewhere
    if (id == 0 && !PluginWindow::containsActiveWindows())
        Process::setDockIconVisible(false);
	#endif
    // Audio settings
    if (id == 1)
        im->showAudioSettings();
    // Reload
    if (id == 2)
        im->reloadPlugins();
    // Plugins
    if (id > 2)
    {
        // Delete plugin
        if (id >= im->INDEX_DELETE && id < im->INDEX_DELETE + 1000000)
        {
            im->deletePluginStates();

			int index = id - im->INDEX_DELETE;
			std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
			String key = getKey("order", timeSorted[index]);
			int unsortedIndex = 0;
			for (int i = 0; im->activePluginList.getNumTypes(); i++)
			{
				PluginDescription current = *im->activePluginList.getType(i);
				if (key.equalsIgnoreCase(getKey("order", current)))
				{
					unsortedIndex = i;
					break;
				}
			}

			// Remove plugin order
			getAppProperties().getUserSettings()->removeValue(key);
			// Remove bypass entry
			getAppProperties().getUserSettings()->removeValue(getKey("bypass", timeSorted[index]));
			getAppProperties().saveIfNeeded();
			
			// Remove plugin from list
            im->activePluginList.removeType(unsortedIndex);

			// Save current states
			im->savePluginStates();
			im->loadActivePlugins();
        }
        // Add plugin
        else if (im->knownPluginList.getIndexChosenByMenu(id) > -1)
        {
			PluginDescription plugin = *im->knownPluginList.getType(im->knownPluginList.getIndexChosenByMenu(id));
			String key = getKey("order", plugin);
			int t = time(0);
			getAppProperties().getUserSettings()->setValue(key, t);
			getAppProperties().saveIfNeeded();
            im->activePluginList.addType(plugin);

			im->savePluginStates();
			im->loadActivePlugins();
        }
		// Bypass plugin
		else if (id >= im->INDEX_BYPASS && id < im->INDEX_BYPASS + 1000000)
		{
			int index = id - im->INDEX_BYPASS;
			std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
			String key = getKey("bypass", timeSorted[index]);

			// Set bypass flag
			bool bypassed = getAppProperties().getUserSettings()->getBoolValue(key);
			getAppProperties().getUserSettings()->setValue(key, !bypassed);
			getAppProperties().saveIfNeeded();

			im->savePluginStates();
			im->loadActivePlugins();
		}
        // Show active plugin GUI
		else if (id >= im->INDEX_EDIT && id < im->INDEX_EDIT + 1000000)
        {
            if (const AudioProcessorGraph::Node::Ptr f = im->graph.getNodeForId(id - im->INDEX_EDIT + 1))
                if (PluginWindow* const w = PluginWindow::getWindowFor(f, PluginWindow::Normal))
                    w->toFront(true);
        }
		// Move plugin up the list
		else if (id >= im->INDEX_MOVE_UP && id < im->INDEX_MOVE_UP + 1000000)
		{
			im->savePluginStates();
			std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
			PluginDescription toMove = timeSorted[id - im->INDEX_MOVE_UP];
			for (int i = 0; i < timeSorted.size(); i++)
			{
				bool move = getKey("move", toMove).equalsIgnoreCase(getKey("move", timeSorted[i]));
				getAppProperties().getUserSettings()->setValue(getKey("order", timeSorted[i]), move ? i : i+1);
				if (move)
					getAppProperties().getUserSettings()->setValue(getKey("order", timeSorted[i-1]), i+1);
			}
			im->loadActivePlugins();
		}
		// Move plugin down the list
		else if (id >= im->INDEX_MOVE_DOWN && id < im->INDEX_MOVE_DOWN + 1000000)
		{
			im->savePluginStates();
			std::vector<PluginDescription> timeSorted = im->getTimeSortedList();
			PluginDescription toMove = timeSorted[id - im->INDEX_MOVE_DOWN];
			for (int i = 0; i < timeSorted.size(); i++)
			{
				bool move = getKey("move", toMove).equalsIgnoreCase(getKey("move", timeSorted[i]));
				getAppProperties().getUserSettings()->setValue(getKey("order", timeSorted[i]), move ? i+2 : i+1);
				if (move)
				{
					getAppProperties().getUserSettings()->setValue(getKey("order", timeSorted[i + 1]), i + 1);
					i++;
				}
			}
			im->loadActivePlugins();
		}
        // Update menu
        im->startTimer(50);
    }
}

std::vector<PluginDescription> IconMenu::getTimeSortedList()
{
	int time = 0;
	std::vector<PluginDescription> list;
	for (int i = 0; i < activePluginList.getNumTypes(); i++)
		list.push_back(getNextPluginOlderThanTime(time));
	return list;
		
}

String IconMenu::getKey(String type, PluginDescription plugin)
{
	String key = "plugin-" + type.toLowerCase() + "-" + plugin.name + plugin.version + plugin.pluginFormatName;
	return key;
}

void IconMenu::deletePluginStates()
{
	std::vector<PluginDescription> list = getTimeSortedList();
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		String pluginUid = getKey("state", list[i]);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::savePluginStates()
{
	if (settingsImported)
		return;

	std::vector<PluginDescription> list = getTimeSortedList();
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		AudioProcessorGraph::Node* node = graph.getNodeForId(i + 1);
		if (node == nullptr)
			break;
        AudioProcessor& processor = *node->getProcessor();
		String pluginUid = getKey("state", list[i]);
        MemoryBlock savedStateBinary;
        processor.getStateInformation(savedStateBinary);
        getAppProperties().getUserSettings()->setValue(pluginUid, savedStateBinary.toBase64Encoding());
        getAppProperties().saveIfNeeded();
    }
}

void IconMenu::exportBackup()
{
	PropertiesFile* settings = getAppProperties().getUserSettings();
	savePluginStates();

	ScopedPointer<XmlElement> audioState(deviceManager.createStateXml());
	settings->setValue("audioDeviceState", audioState);
	if (!settings->saveIfNeeded())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Could not save the current settings.");
		return;
	}

	FileChooser chooser("Export Light Host backup",
		File::getSpecialLocation(File::userHomeDirectory).getChildFile("Light Host Backup.zip"),
		"*.zip");
	if (!chooser.browseForFileToSave(true))
		return;

	const File destination = chooser.getResult();
	if (destination == settings->getFile())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Choose a different file from the active settings file.");
		return;
	}

	ZipFile::Builder archive;
	XmlElement manifest("LIGHTHOST_BACKUP");
	manifest.setAttribute("version", 1);
	StringArray archivedPluginPaths;
	StringArray errors;
	int archivedPlugins = 0;

	for (int i = 0; i < knownPluginList.getNumTypes(); ++i)
	{
		const PluginDescription* plugin = knownPluginList.getType(i);
		const File pluginFile(File::createFileWithoutCheckingPath(plugin->fileOrIdentifier));

		AudioPluginFormat* format = nullptr;
		for (int formatIndex = 0; formatIndex < formatManager.getNumFormats(); ++formatIndex)
		{
			AudioPluginFormat* candidate = formatManager.getFormat(formatIndex);
			if (candidate->canScanForPlugins()
				&& candidate->getName().equalsIgnoreCase(plugin->pluginFormatName))
			{
				format = candidate;
				break;
			}
		}

		if (format == nullptr)
			continue;
		if (!pluginFile.existsAsFile() && !pluginFile.isDirectory())
		{
			if (format->getDefaultLocationsToSearch().getNumPaths() > 0)
				errors.add(plugin->name + ": plugin file is missing at "
					+ pluginFile.getFullPathName() + ".");
			continue;
		}
		if (!format->fileMightContainThisPluginType(plugin->fileOrIdentifier)
			|| archivedPluginPaths.contains(pluginFile.getFullPathName()))
			continue;

		const String archiveRoot = "plugins/" + String(archivedPlugins);
		if (!addPluginFilesToArchive(archive, pluginFile, archiveRoot))
		{
			AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
				"Could not read the plugin file " + pluginFile.getFullPathName() + ".");
			return;
		}

		XmlElement* pluginEntry = manifest.createNewChildElement("PLUGIN");
		pluginEntry->setAttribute("id", archivedPlugins);
		pluginEntry->setAttribute("format", format->getName());
		pluginEntry->setAttribute("source", pluginFile.getFullPathName());
		pluginEntry->setAttribute("archiveRoot", archiveRoot);
		archivedPluginPaths.add(pluginFile.getFullPathName());
		++archivedPlugins;
	}

	if (!errors.isEmpty())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Could not create a complete backup because some plugin files are unavailable.\n\n"
			+ errors.joinIntoString("\n"));
		return;
	}

	ScopedPointer<XmlElement> settingsXml(settings->createXml("PROPERTIES"));
	const String settingsText = settingsXml->createDocument(String(), true);
	MemoryBlock settingsData(settingsText.toRawUTF8(), settingsText.getNumBytesAsUTF8());
	archive.addEntry(new MemoryInputStream(settingsData, true), 6, "settings.xml",
		Time::getCurrentTime());
	const String manifestText = manifest.createDocument(String(), true);
	MemoryBlock manifestData(manifestText.toRawUTF8(), manifestText.getNumBytesAsUTF8());
	archive.addEntry(new MemoryInputStream(manifestData, true), 6, "manifest.xml",
		Time::getCurrentTime());

	const File temporaryFile = destination.getSiblingFile(destination.getFileName() + ".tmp");
	if (temporaryFile.exists() && !temporaryFile.deleteFile())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Could not prepare a temporary backup file.");
		return;
	}

	bool archiveWritten = false;
	{
		FileOutputStream output(temporaryFile);
		if (!output.failedToOpen())
		{
			archiveWritten = archive.writeToStream(output, nullptr);
			output.flush();
			archiveWritten = archiveWritten && !output.getStatus().failed();
		}
	}
	if (!archiveWritten || !temporaryFile.replaceFileIn(destination))
	{
		temporaryFile.deleteFile();
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Could not write the backup archive.");
		return;
	}

	AlertWindow::showMessageBox(AlertWindow::InfoIcon, "Export Backup",
		"Backup exported with settings and " + String(archivedPlugins) + " plugin file(s). "
		"Plugin installers, licenses, and external dependencies are not included.");
}

bool IconMenu::importBackup()
{
	FileChooser chooser("Import Light Host backup",
		File::getSpecialLocation(File::userHomeDirectory), "*.zip");
	if (!chooser.browseForFileToOpen())
		return false;

	ZipFile archive(chooser.getResult());
	const int manifestIndex = archive.getIndexOfFileName("manifest.xml");
	const int settingsIndex = archive.getIndexOfFileName("settings.xml");
	if (manifestIndex < 0 || settingsIndex < 0)
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"The selected ZIP is not a Light Host backup.");
		return false;
	}

	ScopedPointer<InputStream> manifestStream(archive.createStreamForEntry(manifestIndex));
	ScopedPointer<InputStream> settingsStream(archive.createStreamForEntry(settingsIndex));
	if (manifestStream == nullptr || settingsStream == nullptr)
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"Could not read the backup archive.");
		return false;
	}

	ScopedPointer<XmlElement> manifest(XmlDocument::parse(manifestStream->readEntireStreamAsString()));
	ScopedPointer<XmlElement> settingsXml(XmlDocument::parse(settingsStream->readEntireStreamAsString()));
	if (manifest == nullptr || !manifest->hasTagName("LIGHTHOST_BACKUP")
		|| manifest->getIntAttribute("version") != 1
		|| settingsXml == nullptr || !settingsXml->hasTagName("PROPERTIES"))
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"The backup manifest or settings file is invalid or unsupported.");
		return false;
	}

	if (!AlertWindow::showOkCancelBox(AlertWindow::QuestionIcon, "Import Backup",
		"This will install included plugin files and replace your current Light Host settings. "
		"Existing plugin files will not be overwritten. Continue?",
		"Import", "Cancel"))
		return false;

	PropertySet importedSettings;
	importedSettings.restoreFromXml(*settingsXml);
	PropertiesFile* currentSettings = getAppProperties().getUserSettings();
	StringArray oldPluginPaths, newPluginPaths;
	StringArray localScanFormats, localScanPaths;
	StringArray errors;

	forEachXmlChildElementWithTagName(*manifest, pluginEntry, "PLUGIN")
	{
		const String id = pluginEntry->getStringAttribute("id");
		const String archiveRoot = pluginEntry->getStringAttribute("archiveRoot");
		const String sourcePath = pluginEntry->getStringAttribute("source");
		const String formatName = pluginEntry->getStringAttribute("format");
		if (id.isEmpty() || archiveRoot != "plugins/" + id || sourcePath.isEmpty())
		{
			errors.add("The backup contains an invalid plugin entry.");
			continue;
		}

		AudioPluginFormat* format = nullptr;
		for (int formatIndex = 0; formatIndex < formatManager.getNumFormats(); ++formatIndex)
		{
			AudioPluginFormat* candidate = formatManager.getFormat(formatIndex);
			if (candidate->canScanForPlugins()
				&& candidate->getName().equalsIgnoreCase(formatName))
			{
				format = candidate;
				break;
			}
		}
		if (format == nullptr)
		{
			errors.add(formatName + ": this plugin format is not supported on this computer.");
			continue;
		}

		const File sourcePlugin(File::createFileWithoutCheckingPath(sourcePath));
		const String pluginFileName = sourcePlugin.getFileName();
		const FileSearchPath scanPaths =
			PluginListComponent::getLastSearchPath(*currentSettings, *format);
		if (scanPaths.getNumPaths() == 0 || pluginFileName.isEmpty()
			|| pluginFileName == "." || pluginFileName == ".."
			|| pluginFileName.containsChar('/') || pluginFileName.containsChar('\\'))
		{
			errors.add(formatName + ": no valid plugin scan location or filename is available.");
			continue;
		}

		const File scanDirectory = scanPaths[0];
		const File destinationPlugin = scanDirectory.getChildFile(pluginFileName);
		if (!destinationPlugin.isAChildOf(scanDirectory))
		{
			errors.add(formatName + ": the plugin destination is outside its scan location.");
			continue;
		}
		const String archivePrefix = archiveRoot + "/";
		int archivedFileCount = 0;
		bool unsafeArchivePath = false;
		for (int entryIndex = 0; entryIndex < archive.getNumEntries(); ++entryIndex)
		{
			const ZipFile::ZipEntry* entry = archive.getEntry(entryIndex);
			if (entry != nullptr && entry->filename.startsWith(archivePrefix))
			{
				++archivedFileCount;
				if (!isSafePluginArchivePath(entry->filename.substring(archivePrefix.length())))
					unsafeArchivePath = true;
			}
		}
		if (unsafeArchivePath)
		{
			errors.add(sourcePlugin.getFileName() + ": backup contains an unsafe plugin path.");
			continue;
		}
		if (archivedFileCount == 0)
		{
			errors.add(sourcePlugin.getFileName() + ": plugin files are missing from the backup.");
			continue;
		}

		int localFormatIndex = -1;
		for (int i = 0; i < localScanFormats.size(); ++i)
			if (localScanFormats[i].equalsIgnoreCase(format->getName()))
				localFormatIndex = i;
		if (localFormatIndex >= 0)
			localScanPaths.set(localFormatIndex, scanPaths.toString());
		else
		{
			localScanFormats.add(format->getName());
			localScanPaths.add(scanPaths.toString());
		}

		bool hasPluginFiles = false;
		bool pluginImportFailed = false;

		if (destinationPlugin.exists())
		{
			if (format->fileMightContainThisPluginType(destinationPlugin.getFullPathName()))
			{
				oldPluginPaths.add(sourcePath);
				newPluginPaths.add(destinationPlugin.getFullPathName());
				continue;
			}
			errors.add(destinationPlugin.getFullPathName() + ": destination already exists.");
			continue;
		}

		for (int entryIndex = 0; entryIndex < archive.getNumEntries(); ++entryIndex)
		{
			const ZipFile::ZipEntry* entry = archive.getEntry(entryIndex);
			if (entry == nullptr || !entry->filename.startsWith(archivePrefix))
				continue;

			const String relativePath = entry->filename.substring(archivePrefix.length());
			if (!isSafePluginArchivePath(relativePath))
			{
				errors.add("The backup contains an unsafe plugin path.");
				pluginImportFailed = true;
				break;
			}

			const File target = destinationPlugin.getChildFile(relativePath);
			if (!target.isAChildOf(destinationPlugin) || target.exists()
				|| !target.getParentDirectory().createDirectory())
			{
				errors.add(target.getFullPathName() + ": could not create plugin file.");
				pluginImportFailed = true;
				break;
			}

			ScopedPointer<InputStream> input(archive.createStreamForEntry(entryIndex));
			FileOutputStream output(target);
			if (input == nullptr || output.failedToOpen())
			{
				errors.add(target.getFullPathName() + ": could not extract plugin file.");
				pluginImportFailed = true;
				break;
			}
			const int64 bytesWritten = output.writeFromInputStream(*input, -1);
			output.flush();
			if (bytesWritten != entry->uncompressedSize || output.getStatus().failed())
			{
				errors.add(target.getFullPathName() + ": could not extract plugin file.");
				pluginImportFailed = true;
				break;
			}
			hasPluginFiles = true;
		}

		if (!pluginImportFailed && hasPluginFiles
			&& format->fileMightContainThisPluginType(destinationPlugin.getFullPathName()))
		{
			oldPluginPaths.add(sourcePath);
			newPluginPaths.add(destinationPlugin.getFullPathName());
		}
		else if (!pluginImportFailed)
			errors.add(sourcePlugin.getFileName() + ": plugin files are missing or invalid.");
	}

	if (!errors.isEmpty())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"Settings were not changed because some plugin files could not be restored. "
			"Files copied before the error may remain in the plugin scan folders.\n\n"
			+ errors.joinIntoString("\n"));
		return false;
	}

	remapPluginPaths(importedSettings, "pluginList", oldPluginPaths, newPluginPaths);
	remapPluginPaths(importedSettings, "pluginListActive", oldPluginPaths, newPluginPaths);
	for (int i = 0; i < localScanFormats.size(); ++i)
		importedSettings.setValue("lastPluginScanPath_" + localScanFormats[i], localScanPaths[i]);

	PropertySet previousSettings;
	previousSettings.addAllPropertiesFrom(*currentSettings);
	currentSettings->clear();
	currentSettings->addAllPropertiesFrom(importedSettings);
	if (!currentSettings->saveIfNeeded())
	{
		currentSettings->clear();
		currentSettings->addAllPropertiesFrom(previousSettings);
		const bool restored = currentSettings->saveIfNeeded();
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			restored ? "Could not save the imported settings. Your previous settings were restored."
				: "Could not save the imported settings or restore the previous settings.");
		return false;
	}

	settingsImported = true;
	AlertWindow::showMessageBox(AlertWindow::InfoIcon, "Import Backup",
		"Backup imported. Light Host will now close; reopen it to apply the restored settings.");
	return true;
}

void IconMenu::showAudioSettings()
{
    AudioDeviceSelectorComponent audioSettingsComp (deviceManager, 0, 256, 0, 256, false, false, true, true);
    audioSettingsComp.setSize(500, 450);
    
    DialogWindow::LaunchOptions o;
    o.content.setNonOwned(&audioSettingsComp);
    o.dialogTitle                   = "Audio Settings";
    o.componentToCentreAround       = this;
    o.dialogBackgroundColour        = Colour::fromRGB(236, 236, 236);
    o.escapeKeyTriggersCloseButton  = true;
    o.useNativeTitleBar             = true;
    o.resizable                     = false;

    o.runModal();
        
    ScopedPointer<XmlElement> audioState(deviceManager.createStateXml());
        
    getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState);
    getAppProperties().getUserSettings()->saveIfNeeded();
}

void IconMenu::reloadPlugins()
{
	if (pluginListWindow == nullptr)
		pluginListWindow = new PluginListWindow(*this, formatManager);
	pluginListWindow->toFront(true);
}

void IconMenu::removePluginsLackingInputOutput()
{
	std::vector<int> removeIndex;
	for (int i = 0; i < knownPluginList.getNumTypes(); i++)
	{
		PluginDescription* plugin = knownPluginList.getType(i);
		if (plugin->numInputChannels < 2 || plugin->numOutputChannels < 2)
			removeIndex.push_back(i);
	}
	for (int i = 0; i < removeIndex.size(); i++)
		knownPluginList.removeType(removeIndex[i] - i);
}