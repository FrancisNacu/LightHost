//
//  IconMenu.hpp
//  Light Host
//
//  Created by Rolando Islas on 12/26/15.
//
//

#ifndef IconMenu_hpp
#define IconMenu_hpp

ApplicationProperties& getAppProperties();

class IconMenu : public SystemTrayIconComponent, private Timer, public ChangeListener
{
public:
    IconMenu();
    ~IconMenu();
    void mouseDown(const MouseEvent&);
	void mouseDoubleClick(const MouseEvent&);
    static void menuInvocationCallback(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed);
	static String getKey(String type, PluginDescription plugin);

	const int INDEX_EDIT, INDEX_BYPASS, INDEX_DELETE, INDEX_MOVE_UP, INDEX_MOVE_DOWN;
private:
	#if JUCE_MAC
    std::string exec(const char* cmd);
	#endif
    void timerCallback();
    void reloadPlugins();
    void showAudioSettings();
    void loadActivePlugins();
    void savePluginStates();
    void deletePluginStates();
    void exportBackup();
    bool importBackup();
	void performPluginAction(int id);
	void addPluginToActiveList(const PluginDescription& plugin);
	void moveActivePlugin(int fromIndex, int toIndex);
	PluginDescription getNextPluginOlderThanTime(int &time);
	void removePluginsLackingInputOutput();
	std::vector<PluginDescription> getTimeSortedList();
	void setIcon();

    AudioDeviceManager deviceManager;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    KnownPluginList activePluginList;
    PopupMenu menu;
    ScopedPointer<PluginDirectoryScanner> scanner;
	uint32 lastLeftClickTime;
    bool settingsImported;
    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
    AudioProcessorGraph::Node *inputNode;
    AudioProcessorGraph::Node *outputNode;
	#if JUCE_WINDOWS
	int x, y;
	#endif

	class PluginManagementComponent;
	class PluginListWindow;
	ScopedPointer<PluginListWindow> pluginListWindow;
};

#endif /* IconMenu_hpp */
