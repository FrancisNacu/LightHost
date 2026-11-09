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
#include <algorithm>
#include <ctime>
#include <limits.h>
#include <string>
#if JUCE_WINDOWS
#include "Windows.h"
#endif

namespace
{
enum
{
	menuAudioSettings = 1,
	menuPlugins,
	menuImportBackup,
	menuExportBackup,
	menuRunOnStartup,
	menuDeletePluginStates,
	menuInvertIconColor,
	menuQuit
};

enum ActivePluginHoveredAction
{
	noActivePluginAction,
	bypassActivePluginAction
};

const char* const activePluginDragPrefix = "active-plugin:";

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

class IconMenu::PluginManagementComponent : public Component,
	public DragAndDropContainer, public DragAndDropTarget,
	private ListBoxModel, private Button::Listener
{
public:
	PluginManagementComponent(IconMenu& owner_)
		: owner(owner_), activePlugins("Active Plugins", this),
		  addButton("Add to Active Plugins"),
		  hoveredRow(-1), hoveredAction(noActivePluginAction), dragTargetIndex(-1)
	{
		activeGroup.setText("Active Plugins");
		availableGroup.setText("Available Plugins");
		addAndMakeVisible(activeGroup);
		addAndMakeVisible(availableGroup);
		bypassHeading.setText("Bypass", dontSendNotification);
		bypassHeading.setJustificationType(Justification::centred);
		bypassHeading.setFont(Font(12.0f, Font::bold));
		bypassHeading.setTooltip("Check a plugin to bypass it");
		addAndMakeVisible(bypassHeading);

		activeInstructions.setText(
			"Drag to reorder. Right-click a plugin to remove it; double-click to open.",
			dontSendNotification);
		availableInstructions.setText(
			"Select a plugin below and add it to your active chain.",
			dontSendNotification);
		activeInstructions.setFont(Font(13.0f));
		availableInstructions.setFont(Font(13.0f));
		activeInstructions.setColour(Label::textColourId, Colours::darkgrey);
		availableInstructions.setColour(Label::textColourId, Colours::darkgrey);
		addAndMakeVisible(activeInstructions);
		addAndMakeVisible(availableInstructions);

		activePlugins.setRowHeight(26);
		addAndMakeVisible(activePlugins);
		activePlugins.addMouseListener(this, true);

		const File deadMansPedalFile(getAppProperties().getUserSettings()
			->getFile().getSiblingFile("RecentlyCrashedPluginsList"));
		availablePlugins = new PluginListComponent(owner.formatManager,
			owner.knownPluginList, deadMansPedalFile,
			getAppProperties().getUserSettings());
		addAndMakeVisible(availablePlugins);
		availablePlugins->getTableListBox().addMouseListener(this, true);

		activeEmptyState.setText(
			"No active plugins. Add one from Available Plugins.",
			dontSendNotification);
		activeEmptyState.setJustificationType(Justification::centred);
		activeEmptyState.setColour(Label::textColourId, Colours::darkgrey);
		activeEmptyState.setInterceptsMouseClicks(false, false);
		addAndMakeVisible(activeEmptyState);

		Button* const buttons[] = {
			&addButton
		};
		for (int i = 0; i < (int) numElementsInArray(buttons); ++i)
		{
			addAndMakeVisible(buttons[i]);
			buttons[i]->addListener(this);
		}

		refreshActivePlugins();
		setSize(1000, 600);
	}

	void refreshActivePlugins()
	{
		activePlugins.updateContent();
		activeEmptyState.setVisible(owner.activePluginList.getNumTypes() == 0);
		updateAddButton();
		activePlugins.repaint();
	}

	void mouseUp(const MouseEvent&) override
	{
		updateAddButton();
	}

	void mouseMove(const MouseEvent& event) override
	{
		updateActiveHover(event);
	}

	void mouseExit(const MouseEvent& event) override
	{
		updateActiveHover(event);
	}

	void resized() override
	{
		juce::Rectangle<int> contentBounds(getLocalBounds().reduced(10));
		const int gap = 12;
		const int activeWidth = (contentBounds.getWidth() - gap) * 2 / 5;
		juce::Rectangle<int> activeArea = contentBounds.removeFromLeft(activeWidth);
		contentBounds.removeFromLeft(gap);
		juce::Rectangle<int> availableArea = contentBounds;

		activeGroup.setBounds(activeArea);
		availableGroup.setBounds(availableArea);

		activeArea = activeArea.reduced(10);
		availableArea = availableArea.reduced(10);
		activeArea.removeFromTop(16);
		availableArea.removeFromTop(16);

		activeInstructions.setBounds(activeArea.removeFromBottom(24));
		activeArea.removeFromBottom(4);
		activePlugins.setBounds(activeArea);
		activeEmptyState.setBounds(activeArea);
		bypassHeading.setBounds(activePlugins.getRight() - 43,
			activeGroup.getY(), 44, 16);

		addButton.setBounds(availableArea.removeFromBottom(30));
		availableArea.removeFromBottom(4);
		availableInstructions.setBounds(availableArea.removeFromBottom(24));
		availableArea.removeFromBottom(4);
		availablePlugins->setBounds(availableArea);

	}

private:
	int getNumRows() override
	{
		return owner.activePluginList.getNumTypes();
	}

	void paintListBoxItem(int row, Graphics& graphics, int width, int height,
		bool rowIsSelected) override
	{
		if (row < 0 || row >= owner.activePluginList.getNumTypes())
			return;

		if (rowIsSelected)
			graphics.fillAll(findColour(TextEditor::highlightColourId));
		else if (row == hoveredRow)
			graphics.fillAll(findColour(TextEditor::highlightColourId).withAlpha(0.2f));
		else
			graphics.fillAll(findColour(ListBox::backgroundColourId));

		const std::vector<PluginDescription> plugins = owner.getTimeSortedList();
		String name = String(row + 1) + ". " + plugins[row].name;
		graphics.setColour(findColour(rowIsSelected
			? TextEditor::highlightedTextColourId : ListBox::textColourId));
		const bool bypassed = getAppProperties().getUserSettings()->getBoolValue(
			IconMenu::getKey("bypass", plugins[row]));

		graphics.drawText(name, 6, 0, width - 48, height,
			Justification::centredLeft);

		const juce::Rectangle<int> bypassBounds = getBypassBounds(width, height);
		graphics.setColour(Colours::white);
		graphics.fillRoundedRectangle(bypassBounds.toFloat(), 2.0f);
		graphics.setColour(hoveredRow == row
			&& hoveredAction == bypassActivePluginAction
			? Colours::royalblue : Colours::darkgrey);
		graphics.drawRoundedRectangle(bypassBounds.toFloat(), 2.0f, 1.2f);
		if (bypassed)
		{
			graphics.setColour(Colours::darkgreen);
			graphics.drawLine((float) bypassBounds.getX() + 3.0f,
				(float) bypassBounds.getY() + bypassBounds.getHeight() * 0.55f,
				(float) bypassBounds.getX() + bypassBounds.getWidth() * 0.42f,
				(float) bypassBounds.getBottom() - 3.0f, 2.0f);
			graphics.drawLine((float) bypassBounds.getX() + bypassBounds.getWidth() * 0.42f,
				(float) bypassBounds.getBottom() - 3.0f,
				(float) bypassBounds.getRight() - 3.0f,
				(float) bypassBounds.getY() + 3.0f, 2.0f);
		}

		if (dragTargetIndex == row
			|| (row == owner.activePluginList.getNumTypes() - 1
				&& dragTargetIndex == owner.activePluginList.getNumTypes()))
		{
			graphics.setColour(Colours::deepskyblue);
			graphics.fillRect(0, dragTargetIndex == row ? 0 : height - 2, width, 2);
		}
	}

	void listBoxItemClicked(int row, const MouseEvent& event) override
	{
		if (event.getNumberOfClicks() > 1)
			return;

		const MouseEvent listEvent = event.getEventRelativeTo(&activePlugins);
		if (row < 0 || row >= owner.activePluginList.getNumTypes())
			return;

		if (event.mods.isPopupMenu())
		{
			activePlugins.selectRow(row, true, true);
			PopupMenu contextMenu;
			contextMenu.addItem(1, "Remove Plugin...");
			const juce::Rectangle<int> menuAnchor(event.getScreenPosition().x,
				event.getScreenPosition().y, 1, 1);
			if (contextMenu.showAt(menuAnchor) == 1)
				confirmRemovePlugin(row);
			return;
		}

		const ActivePluginHoveredAction action =
			getActionAtPosition(listEvent.getPosition(), row);
		if (action == bypassActivePluginAction)
		{
			owner.performPluginAction(owner.INDEX_BYPASS + row);
			refreshActivePlugins();
		}
	}

	void deleteKeyPressed(int row) override
	{
		confirmRemovePlugin(row);
	}

	void confirmRemovePlugin(int row)
	{
		const std::vector<PluginDescription> plugins = owner.getTimeSortedList();
		if (row < 0 || row >= (int) plugins.size())
			return;

		if (AlertWindow::showOkCancelBox(AlertWindow::QuestionIcon,
			"Remove Plugin", "Remove " + plugins[row].name
				+ " from your active plugins?",
			"Remove", "Cancel"))
		{
			owner.performPluginAction(owner.INDEX_DELETE + row);
			refreshActivePlugins();
		}
	}

	void listBoxItemDoubleClicked(int row, const MouseEvent& event) override
	{
		if (row < 0 || row >= owner.activePluginList.getNumTypes())
			return;

		const MouseEvent listEvent = event.getEventRelativeTo(&activePlugins);
		if (getActionAtPosition(listEvent.getPosition(), row) == noActivePluginAction)
			owner.performPluginAction(owner.INDEX_EDIT + row);
	}

	String getTooltipForRow(int row) override
	{
		if (row != hoveredRow)
			return String();

		if (hoveredAction == bypassActivePluginAction)
		{
			const std::vector<PluginDescription> plugins = owner.getTimeSortedList();
			if (row >= 0 && row < (int) plugins.size()
				&& getAppProperties().getUserSettings()->getBoolValue(
					IconMenu::getKey("bypass", plugins[row])))
				return "Enable this plugin";
			return "Bypass this plugin";
		}

		return "Drag to reorder; double-click to open; right-click to remove";
	}

	void buttonClicked(Button* button) override
	{
		if (button == &addButton)
		{
			const int row = availablePlugins->getTableListBox().getSelectedRow();
			if (row >= 0 && row < owner.knownPluginList.getNumTypes())
			{
				owner.addPluginToActiveList(*owner.knownPluginList.getType(row));
				refreshActivePlugins();
			}
			return;
		}

	}

	var getDragSourceDescription(const SparseSet<int>& rowsToDescribe) override
	{
		if (rowsToDescribe.size() != 1)
			return var();

		const int row = rowsToDescribe[0];
		const std::vector<PluginDescription> plugins = owner.getTimeSortedList();
		if (row < 0 || row >= (int) plugins.size())
			return var();

		return String(activePluginDragPrefix) + IconMenu::getKey("order", plugins[row]);
	}

	bool isInterestedInDragSource(const SourceDetails& details) override
	{
		return details.sourceComponent.get() == &activePlugins
			&& details.description.toString().startsWith(activePluginDragPrefix);
	}

	void itemDragEnter(const SourceDetails& details) override
	{
		updateDragTarget(details);
	}

	void itemDragMove(const SourceDetails& details) override
	{
		updateDragTarget(details);
	}

	void itemDragExit(const SourceDetails&) override
	{
		setDragTargetIndex(-1);
	}

	void itemDropped(const SourceDetails& details) override
	{
		const String description = details.description.toString();
		if (!description.startsWith(activePluginDragPrefix))
		{
			setDragTargetIndex(-1);
			return;
		}

		const String pluginOrderKey = description.substring(
			String(activePluginDragPrefix).length());
		const std::vector<PluginDescription> plugins = owner.getTimeSortedList();
		int fromIndex = -1;
		for (int i = 0; i < (int) plugins.size(); ++i)
			if (pluginOrderKey == IconMenu::getKey("order", plugins[i]))
			{
				fromIndex = i;
				break;
			}

		if (fromIndex >= 0 && !plugins.empty())
		{
			const Point<int> listPosition =
				activePlugins.getLocalPoint(this, details.localPosition);
			int toIndex = getInsertionIndex(listPosition);
			if (toIndex > fromIndex)
				--toIndex;
			toIndex = jlimit(0, (int) plugins.size() - 1, toIndex);
			owner.moveActivePlugin(fromIndex, toIndex);
			activePlugins.selectRow(toIndex, true, true);
			refreshActivePlugins();
		}

		setDragTargetIndex(-1);
	}

	void updateAddButton()
	{
		const int row = availablePlugins->getTableListBox().getSelectedRow();
		addButton.setEnabled(row >= 0 && row < owner.knownPluginList.getNumTypes());
	}

	void updateActiveHover(const MouseEvent& event)
	{
		if (!activePlugins.isParentOf(event.eventComponent)
			&& event.eventComponent != &activePlugins)
			return;

		const MouseEvent listEvent = event.getEventRelativeTo(&activePlugins);
		const bool insideList = activePlugins.getLocalBounds().contains(listEvent.getPosition());
		const int row = insideList
			? activePlugins.getRowContainingPosition(listEvent.x, listEvent.y) : -1;
		const ActivePluginHoveredAction action = row >= 0
			? getActionAtPosition(listEvent.getPosition(), row) : noActivePluginAction;
		if (row == hoveredRow && action == hoveredAction)
			return;

		const int previousRow = hoveredRow;
		hoveredRow = row;
		hoveredAction = action;
		if (previousRow >= 0)
			activePlugins.repaint(activePlugins.getRowPosition(previousRow, true));
		if (hoveredRow >= 0)
			activePlugins.repaint(activePlugins.getRowPosition(hoveredRow, true));
	}

	juce::Rectangle<int> getBypassBounds(int width, int height) const
	{
		return juce::Rectangle<int>(width - 30, (height - 18) / 2, 18, 18);
	}

	ActivePluginHoveredAction getActionAtPosition(Point<int> position, int row) const
	{
		const juce::Rectangle<int> rowBounds = activePlugins.getRowPosition(row, true);
		const Point<int> rowPosition(position.x - rowBounds.getX(),
			position.y - rowBounds.getY());
		if (getBypassBounds(rowBounds.getWidth(), rowBounds.getHeight())
			.contains(rowPosition))
			return bypassActivePluginAction;
		return noActivePluginAction;
	}

	int getInsertionIndex(const Point<int>& position) const
	{
		const int numRows = owner.activePluginList.getNumTypes();
		if (numRows == 0 || position.y < 0)
			return 0;
		if (position.y >= activePlugins.getHeight())
			return numRows;

		const int row = activePlugins.getRowContainingPosition(position.x, position.y);
		if (row < 0)
			return numRows;

		const juce::Rectangle<int> rowBounds = activePlugins.getRowPosition(row, true);
		return row + (position.y >= rowBounds.getCentreY() ? 1 : 0);
	}

	void setDragTargetIndex(int index)
	{
		if (dragTargetIndex != index)
		{
			dragTargetIndex = index;
			activePlugins.repaint();
		}
	}

	void updateDragTarget(const SourceDetails& details)
	{
		const Point<int> listPosition = activePlugins.getLocalPoint(this, details.localPosition);
		setDragTargetIndex(getInsertionIndex(listPosition));
	}

	IconMenu& owner;
	GroupComponent activeGroup, availableGroup;
	Label activeInstructions, availableInstructions, activeEmptyState, bypassHeading;
	ListBox activePlugins;
	TextButton addButton;
	ScopedPointer<PluginListComponent> availablePlugins;
	int hoveredRow;
	ActivePluginHoveredAction hoveredAction;
	int dragTargetIndex;
};

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
	explicit PluginListWindow(IconMenu& owner_)
		: DocumentWindow("Plugins", Colours::white,
			DocumentWindow::minimiseButton | DocumentWindow::closeButton),
		owner(owner_), managementComponent(new PluginManagementComponent(owner_))
	{
		setContentOwned(managementComponent, true);

		setUsingNativeTitleBar(true);
		setResizable(true, false);
		setResizeLimits(820, 500, 1400, 1000);
		setTopLeftPosition(60, 60);

		restoreWindowStateFromString(getAppProperties().getUserSettings()->getValue("listWindowPos"));
		setVisible(true);
	}

	void refreshActivePlugins()
	{
		if (managementComponent != nullptr)
			managementComponent->refreshActivePlugins();
	}

	~PluginListWindow()
	{
		if (!owner.settingsImported)
			getAppProperties().getUserSettings()->setValue("listWindowPos", getWindowStateAsString());

		clearContentComponent();
		managementComponent = nullptr;
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
	PluginManagementComponent* managementComponent;
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000), INDEX_MOVE_UP(4000000), INDEX_MOVE_DOWN(5000000)
{
    // Initiialization
	settingsImported = false;
	lastLeftClickTime = 0;
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
	deviceManager.removeAudioCallback(&player);
	PluginWindow::closeAllCurrentlyOpenWindows();
	pluginListWindow = nullptr;
	savePluginStates();
	player.setProcessor(nullptr);
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
	player.setProcessor(nullptr);
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
	player.setProcessor(&graph);
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
        if (pluginListWindow != nullptr)
            pluginListWindow->refreshActivePlugins();
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
	menu.addItem(menuAudioSettings, "Audio Settings...");
	menu.addItem(menuPlugins, "Plugins...");
#if JUCE_WINDOWS
    bool runOnStartup = false;
    getRunOnStartupEnabled(runOnStartup);
    menu.addItem(menuRunOnStartup, "Run on startup", true, runOnStartup);
#endif
    menu.addSeparator();
    menu.addItem(menuImportBackup, "Import Backup...");
    menu.addItem(menuExportBackup, "Export Backup...");
	menu.addSeparator();
	menu.addItem(menuDeletePluginStates, "Delete Plugin States");
#if !JUCE_MAC
	menu.addItem(menuInvertIconColor, "Invert Icon Color");
#endif
	menu.addSeparator();
	menu.addItem(menuQuit, "Quit");
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
    if (e.mods.isLeftButtonDown())
    {
        const uint32 now = Time::getMillisecondCounter();
        if (lastLeftClickTime != 0
            && now - lastLeftClickTime <= (uint32) MouseEvent::getDoubleClickTimeout())
        {
            lastLeftClickTime = 0;
            #if JUCE_MAC
            Process::setDockIconVisible(true);
            #endif
            Process::makeForegroundProcess();
            reloadPlugins();
        }
        else
            lastLeftClickTime = now;
        return;
    }

    if (!e.mods.isRightButtonDown())
        return;

    lastLeftClickTime = 0;
    #if JUCE_MAC
    Process::setDockIconVisible(true);
    #endif
    Process::makeForegroundProcess();
    startTimer(50);
}

void IconMenu::mouseDoubleClick(const MouseEvent& e)
{
    if (e.mods.isLeftButtonDown())
    {
        lastLeftClickTime = 0;
        #if JUCE_MAC
        Process::setDockIconVisible(true);
        #endif
        Process::makeForegroundProcess();
        reloadPlugins();
    }
}

void IconMenu::menuInvocationCallback(int id, IconMenu* im)
{
    if (id == 0)
    {
#if JUCE_WINDOWS
        return;
#else
        if (!PluginWindow::containsActiveWindows())
            Process::setDockIconVisible(false);
        return;
#endif
    }

    switch (id)
    {
    case menuAudioSettings:
        im->showAudioSettings();
        break;
    case menuPlugins:
        im->reloadPlugins();
        break;
    case menuImportBackup:
        if (im->importBackup())
            JUCEApplication::getInstance()->quit();
        break;
    case menuExportBackup:
        im->exportBackup();
        break;
#if JUCE_WINDOWS
    case menuRunOnStartup:
        toggleRunOnStartup();
        break;
#endif
    case menuDeletePluginStates:
        im->deletePluginStates();
        im->loadActivePlugins();
        break;
#if !JUCE_MAC
    case menuInvertIconColor:
    {
        String color = getAppProperties().getUserSettings()->getValue("icon");
        getAppProperties().getUserSettings()->setValue("icon",
            color.equalsIgnoreCase("black") ? "white" : "black");
        im->setIcon();
        break;
    }
#endif
    case menuQuit:
        JUCEApplication::getInstance()->quit();
        break;
    default:
        break;
    }
}

void IconMenu::addPluginToActiveList(const PluginDescription& plugin)
{
	savePluginStates();

	int order = (int) time(0);
	for (int i = 0; i < activePluginList.getNumTypes(); ++i)
	{
		const int currentOrder = getAppProperties().getUserSettings()
			->getValue(getKey("order", *activePluginList.getType(i))).getIntValue();
		if (currentOrder >= order)
			order = currentOrder + 1;
	}

	getAppProperties().getUserSettings()->setValue(getKey("order", plugin), order);
	getAppProperties().getUserSettings()->saveIfNeeded();
	activePluginList.addType(plugin);
	loadActivePlugins();
	if (pluginListWindow != nullptr)
		pluginListWindow->refreshActivePlugins();
}

void IconMenu::moveActivePlugin(int fromIndex, int toIndex)
{
	std::vector<PluginDescription> plugins = getTimeSortedList();
	if (fromIndex < 0 || fromIndex >= (int) plugins.size()
		|| toIndex < 0 || toIndex >= (int) plugins.size()
		|| fromIndex == toIndex)
		return;

	savePluginStates();
	const PluginDescription plugin = plugins[fromIndex];
	plugins.erase(plugins.begin() + fromIndex);
	plugins.insert(plugins.begin() + toIndex, plugin);

	PropertiesFile* settings = getAppProperties().getUserSettings();
	for (int i = 0; i < (int) plugins.size(); ++i)
		settings->setValue(getKey("order", plugins[i]), i + 1);
	settings->saveIfNeeded();

	loadActivePlugins();
	if (pluginListWindow != nullptr)
		pluginListWindow->refreshActivePlugins();
}

void IconMenu::performPluginAction(int id)
{
	if (id >= INDEX_DELETE && id < INDEX_DELETE + 1000000)
	{
		const int index = id - INDEX_DELETE;
		std::vector<PluginDescription> plugins = getTimeSortedList();
		if (index < 0 || index >= (int) plugins.size())
			return;

		savePluginStates();
		const PluginDescription plugin = plugins[index];
		const String orderKey = getKey("order", plugin);
		int unsortedIndex = -1;
		for (int i = 0; i < activePluginList.getNumTypes(); ++i)
			if (orderKey.equalsIgnoreCase(getKey("order", *activePluginList.getType(i))))
			{
				unsortedIndex = i;
				break;
			}

		if (unsortedIndex < 0)
			return;

		PropertiesFile* settings = getAppProperties().getUserSettings();
		settings->removeValue(orderKey);
		settings->removeValue(getKey("bypass", plugin));
		settings->removeValue(getKey("state", plugin));
		activePluginList.removeType(unsortedIndex);
		settings->saveIfNeeded();
		loadActivePlugins();
	}
	else if (id >= INDEX_BYPASS && id < INDEX_BYPASS + 1000000)
	{
		const int index = id - INDEX_BYPASS;
		std::vector<PluginDescription> plugins = getTimeSortedList();
		if (index < 0 || index >= (int) plugins.size())
			return;

		const String key = getKey("bypass", plugins[index]);
		PropertiesFile* settings = getAppProperties().getUserSettings();
		settings->setValue(key, !settings->getBoolValue(key));
		settings->saveIfNeeded();
		savePluginStates();
		loadActivePlugins();
	}
	else if (id >= INDEX_EDIT && id < INDEX_EDIT + 1000000)
	{
		const int nodeId = id - INDEX_EDIT + 1;
		if (const AudioProcessorGraph::Node::Ptr node = graph.getNodeForId(nodeId))
			if (PluginWindow* const window = PluginWindow::getWindowFor(node, PluginWindow::Normal))
				window->toFront(true);
	}
	else if (id >= INDEX_MOVE_UP && id < INDEX_MOVE_UP + 1000000)
	{
		const int index = id - INDEX_MOVE_UP;
		moveActivePlugin(index, index - 1);
	}
	else if (id >= INDEX_MOVE_DOWN && id < INDEX_MOVE_DOWN + 1000000)
	{
		const int index = id - INDEX_MOVE_DOWN;
		moveActivePlugin(index, index + 1);
	}

	if (pluginListWindow != nullptr)
		pluginListWindow->refreshActivePlugins();
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
		pluginListWindow = new PluginListWindow(*this);
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