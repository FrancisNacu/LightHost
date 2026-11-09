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
    void toggleIconColor();
    static void menuInvocationCallback(int id, IconMenu*);
    void changeListenerCallback(ChangeBroadcaster* changed);
	static String getKey(const String& type, const PluginDescription& plugin);
	AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }
	void reloadPlugins();
	void loadActivePlugins();
    void savePluginStates();
    void deletePluginStates();
    void exportBackup();
    bool importBackup();

	const int INDEX_EDIT, INDEX_BYPASS, INDEX_DELETE;

    class PluginManagementComponent;
	class PluginListWindow;
private:
	#if JUCE_MAC
    std::string exec(const char* cmd);
	#endif
    void timerCallback();
	void performPluginAction(int id);
	void addPluginToActiveList(const PluginDescription& plugin);
	void moveActivePlugin(int fromIndex, int toIndex);
	std::vector<PluginDescription> getOrderedActivePlugins();
	void setIcon();
    void updatePluginConnections();

    AudioDeviceManager deviceManager;
    AudioPluginFormatManager formatManager;
    KnownPluginList knownPluginList;
    KnownPluginList activePluginList;
    PopupMenu menu;
	uint32 lastLeftClickTime;
    bool settingsImported;
    AudioProcessorGraph graph;
    AudioProcessorPlayer player;
	#if JUCE_WINDOWS
	int x, y;
	#endif

	ScopedPointer<PluginListWindow> pluginListWindow;
};

#endif /* IconMenu_hpp */
