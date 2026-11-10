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
#include "PluginChain.h"
#include "AudioSetup.h"
#include "PluginState.h"
#include "PluginBackup.h"
#include <algorithm>
#include <ctime>
#include <functional>
#include <limits.h>
#include <string>
#if JUCE_WINDOWS
#include "Windows.h"
#endif

namespace
{
enum
{
	menuOpenLightHost = 1,
	menuRunOnStartup,
	menuQuit
};

enum ActivePluginHoveredAction
{
	noActivePluginAction,
	bypassActivePluginAction,
	menuActivePluginAction
};

const char* const activePluginDragPrefix = "active-plugin:";

class PluginActionMenuLookAndFeel : public LookAndFeel_V3
{
public:
	PluginActionMenuLookAndFeel()
	{
	}

	void drawCallOutBoxBackground(CallOutBox& box, Graphics& graphics,
		const Path& path, Image& cachedImage) override
	{
		if (cachedImage.isNull())
		{
			cachedImage = Image(Image::ARGB, box.getWidth(), box.getHeight(), true);
			Graphics shadowGraphics(cachedImage);
			DropShadow(Colours::black.withAlpha(0.18f), 10, Point<int>(0, 3))
				.drawForPath(shadowGraphics, path);
		}

		graphics.drawImageAt(cachedImage, 0, 0);
		graphics.setColour(Colours::white);
		graphics.fillPath(path);
		graphics.setColour(Colour(0xffE3E1F0));
		graphics.strokePath(path, PathStrokeType(1.0f));
	}

	int getCallOutBoxBorderSize(const CallOutBox&) override
	{
		return 0;
	}
};

class PluginActionMenuContent : public Component
{
public:
	explicit PluginActionMenuContent(std::function<void(int)> action_)
		: action(action_)
	{
		setSize(128, 68);
	}

	void paint(Graphics& graphics) override
	{
		graphics.setColour(hoveredAction == 1
			? Colour(0xffECE9FA) : Colours::white);
		graphics.fillRoundedRectangle(2.0f, 2.0f, (float) getWidth() - 4.0f,
			30.0f, 4.0f);
		graphics.setColour(Colour(0xff1D1B2E));
		graphics.setFont(Font(13.0f));
		graphics.drawText("Rename", 14, 2, getWidth() - 28, 30,
			Justification::centredLeft);

		graphics.setColour(Colour(0xffEEECF7));
		graphics.fillRect(12, 34, getWidth() - 24, 1);

		graphics.setColour(hoveredAction == 2
			? Colour(0xffFCE8E8) : Colours::white);
		graphics.fillRoundedRectangle(2.0f, 36.0f, (float) getWidth() - 4.0f,
			30.0f, 4.0f);
		graphics.setColour(Colour(0xffC42B2B));
		graphics.drawText("Delete", 14, 36, getWidth() - 28, 30,
			Justification::centredLeft);
	}

	void mouseMove(const MouseEvent& event) override
	{
		const int actionAtPosition = getActionAtPosition(event.y);
		if (hoveredAction != actionAtPosition)
		{
			hoveredAction = actionAtPosition;
			repaint();
		}
	}

	void mouseExit(const MouseEvent&) override
	{
		if (hoveredAction != 0)
		{
			hoveredAction = 0;
			repaint();
		}
	}

	void mouseUp(const MouseEvent& event) override
	{
		const int selectedAction = getActionAtPosition(event.y);
		if (selectedAction == 0)
			return;

		if (auto* callout = dynamic_cast<CallOutBox*>(getParentComponent()))
			callout->dismiss();
		action(selectedAction);
	}

private:
	int getActionAtPosition(int y) const
	{
		if (y >= 0 && y < 34)
			return 1;
		if (y >= 34 && y < getHeight())
			return 2;
		return 0;
	}

	std::function<void(int)> action;
	int hoveredAction = 0;
};

PluginActionMenuLookAndFeel pluginActionMenuLookAndFeel;

class TrayMenuLookAndFeel : public LookAndFeel_V3
{
public:
	TrayMenuLookAndFeel()
		: appIcon(ImageFileFormat::loadFrom(BinaryData::icon_png, BinaryData::icon_pngSize))
	{
		setColour(PopupMenu::backgroundColourId, Colour(0x00000000));
		setColour(PopupMenu::textColourId, Colour(0xff1D1B2E));
		setColour(PopupMenu::highlightedBackgroundColourId, Colour(0xffF4F3FB));
		setColour(PopupMenu::highlightedTextColourId, Colour(0xff1D1B2E));
	}

	void drawPopupMenuBackground(Graphics& graphics, int width, int height) override
	{
		const juce::Rectangle<float> bounds(0.5f, 0.5f,
			(float) width - 1.0f, (float) height - 1.0f);
		graphics.setColour(Colours::white);
		graphics.fillRoundedRectangle(bounds.reduced(0.5f), 14.0f);
		graphics.setColour(Colour(0xffE3E1F0));
		graphics.drawRoundedRectangle(bounds, 14.0f, 1.0f);
	}

	void drawPopupMenuItem(Graphics& graphics, const juce::Rectangle<int>& area,
		bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
		bool, const String& text, const String&, const Drawable*,
		const Colour* textColour) override
	{
		if (isSeparator)
		{
			const juce::Rectangle<int> divider = area.reduced(10, 0);
			graphics.setColour(Colour(0xffEEECF7));
			graphics.fillRect(divider.getX(), divider.getCentreY(),
				divider.getWidth(), 1);
			return;
		}

		if (isHighlighted)
		{
			graphics.setColour(Colour(0xffF4F3FB));
			graphics.fillRoundedRectangle(area.reduced(4, 1).toFloat(), 5.0f);
		}

		Colour itemTextColour = textColour != nullptr
			? *textColour
			: Colour(0xff1D1B2E);
		if (! isActive)
			itemTextColour = itemTextColour.withAlpha(0.45f);

		graphics.setColour(itemTextColour);
		graphics.setFont(Font(13.0f));
		graphics.drawFittedText(text, area.reduced(14, 0),
			Justification::centredLeft, 1);

		if (text == "Run on startup")
		{
			const juce::Rectangle<float> track((float) area.getRight() - 44.0f,
				(float) area.getCentreY() - 9.0f, 34.0f, 18.0f);
			graphics.setColour(isTicked ? Colour(0xff5B45D6) : Colour(0xffCFCBE6));
			graphics.fillRoundedRectangle(track, 9.0f);
			graphics.setColour(Colours::white);
			graphics.fillEllipse(track.getX() + (isTicked ? 18.0f : 2.0f),
				track.getY() + 2.0f, 14.0f, 14.0f);
		}
	}

	void drawPopupMenuSectionHeader(Graphics& graphics,
		const juce::Rectangle<int>& area, const String& sectionName) override
	{
		graphics.drawImageWithin(appIcon, 14, area.getY(), 20, area.getHeight(),
			RectanglePlacement::centred);
		graphics.setColour(Colour(0xff1D1B2E));
		graphics.setFont(Font(14.0f, Font::bold));
		graphics.drawFittedText(sectionName,
			juce::Rectangle<int>(42, area.getY(), area.getWidth() - 54,
				area.getHeight() - 1),
			Justification::centredLeft, 1);
		graphics.setColour(Colour(0xffEEECF7));
		graphics.fillRect(10, area.getBottom() - 1, area.getWidth() - 20, 1);
	}

	void getIdealPopupMenuItemSize(const String&, bool isSeparator,
		int standardItemHeight, int& idealWidth, int& idealHeight) override
	{
		if (isSeparator)
		{
			idealWidth = 240;
			idealHeight = 4;
			return;
		}

		idealHeight = standardItemHeight > 0 ? standardItemHeight : 36;
		idealWidth = 240;
	}

private:
	Image appIcon;
};

TrayMenuLookAndFeel trayMenuLookAndFeel;

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
                PluginDescription before;
                if (before.loadFromXml(*plugin))
                {
                    PluginDescription after(before);
                    after.fileOrIdentifier = newPaths[i];
                    PluginState::remapSettings(settings, before, after);
                }
				plugin->setAttribute("file", newPaths[i]);
				break;
			}
	}

	settings.setValue(key, pluginList->createDocument(String(), true));
}

#if JUCE_WINDOWS
const wchar_t* const startupRegistryPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const startupRegistryValue = L"Light Host";

String getStartupCommandLine()
{
	return File::getSpecialLocation(File::currentExecutableFile)
		.getFullPathName().quoted();
}

void showStartupRegistryError(LSTATUS error)
{
	AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Run on startup",
		"Could not access or update the Windows startup setting. Error code: "
		+ String((int) error));
}

bool getRunOnStartupEnabled(bool& enabled, bool* registered = nullptr)
{
	enabled = false;
	if (registered != nullptr)
		*registered = false;
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
	// Run commands are limited to 260 characters, including the quoted path.
	wchar_t commandLine[261] = {};
	DWORD size = sizeof(commandLine);
	result = RegQueryValueExW(key, startupRegistryValue, nullptr, &type,
		reinterpret_cast<BYTE*>(commandLine), &size);
	RegCloseKey(key);
	if (result == ERROR_FILE_NOT_FOUND)
		return true;
	if (registered != nullptr && (result == ERROR_SUCCESS || result == ERROR_MORE_DATA))
		*registered = true;
	if (result == ERROR_MORE_DATA)
		return true;
	if (result != ERROR_SUCCESS)
	{
		showStartupRegistryError(result);
		return false;
	}

	// Check the command as well as its presence so a moved executable can
	// refresh an existing registration when it launches.
	enabled = type == REG_SZ && size >= sizeof(wchar_t)
		&& size % sizeof(wchar_t) == 0
		&& commandLine[size / sizeof(wchar_t) - 1] == 0
		&& String(commandLine).equalsIgnoreCase(getStartupCommandLine());
	return true;
}

void setRunOnStartupEnabled(bool enabled)
{
	HKEY key = nullptr;
	DWORD disposition = 0;
	LSTATUS result = RegCreateKeyExW(HKEY_CURRENT_USER, startupRegistryPath, 0, nullptr,
		REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &key, &disposition);
	if (result != ERROR_SUCCESS)
	{
		showStartupRegistryError(result);
		return;
	}

	if (!enabled)
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
		if (commandLine.length() > 260)
		{
			RegCloseKey(key);
			AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Run on startup",
				"The executable path is too long for Windows startup. Move Light Host "
				"to a folder with a shorter path and enable this setting again.");
			return;
		}
		result = RegSetValueExW(key, startupRegistryValue, 0, REG_SZ,
			reinterpret_cast<const BYTE*>(commandLine.c_str()),
			static_cast<DWORD>((commandLine.length() + 1) * sizeof(wchar_t)));
	}

	RegCloseKey(key);
	if (result != ERROR_SUCCESS && !(!enabled && result == ERROR_FILE_NOT_FOUND))
		showStartupRegistryError(result);
}

void toggleRunOnStartup()
{
	bool enabled = false;
	if (getRunOnStartupEnabled(enabled))
		setRunOnStartupEnabled(!enabled);
}

void refreshStartupRegistration()
{
	bool enabled = false;
	bool registered = false;
	// Absence means the user turned startup off. Only repair existing entries.
	if (getRunOnStartupEnabled(enabled, &registered) && registered && !enabled)
		setRunOnStartupEnabled(true);
}
#endif
}

class LightHostSwitch : public ToggleButton
{
public:
    explicit LightHostSwitch (const String& text)
        : ToggleButton (text)
    {
        setClickingTogglesState (true);
    }

    void paintButton (Graphics& g, bool isMouseOverButton, bool isButtonDown) override
    {
        const juce::Rectangle<float> buttonBounds = getLocalBounds().toFloat();
        juce::Rectangle<float> track (buttonBounds.getRight() - 42.0f,
                                      buttonBounds.getCentreY() - 10.0f, 38.0f, 20.0f);
        const bool enabled = getToggleState();

        Colour trackColour = enabled ? Colour (0xff5B45D6) : Colour (0xffCFCBE6);
        if (isMouseOverButton || isButtonDown)
            trackColour = trackColour.brighter (isButtonDown ? 0.12f : 0.06f);
        g.setColour (trackColour);
        g.fillRoundedRectangle (track, 10.0f);
        g.setColour (Colours::white);
        const float thumbX = enabled ? track.getRight() - 18.0f : track.getX() + 2.0f;
        g.fillEllipse (thumbX, track.getY() + 2.0f, 16.0f, 16.0f);

        g.setColour (Colour (0xff1D1B2E).withAlpha (isEnabled() ? 1.0f : 0.5f));
        g.setFont (Font (14.0f));
        g.drawText (getButtonText(), 0, 0, getWidth() - 52, getHeight(),
                    Justification::centredLeft, true);

        if (hasKeyboardFocus (false))
        {
            g.setColour (Colour (0xff5B45D6));
            g.drawRoundedRectangle (buttonBounds.reduced (0.5f), 5.0f, 1.0f);
        }

    }
};

class LightHostAudioDeviceSelector : public AudioDeviceSelectorComponent
{
public:
    explicit LightHostAudioDeviceSelector (AudioDeviceManager& deviceManager)
        : AudioDeviceSelectorComponent (deviceManager, 0, 2, 0, 2,
                                        false, false, true, false)
    {
    }

    void resized() override
    {
        AudioDeviceSelectorComponent::resized();

        ComboBox* deviceType = nullptr;
        Component* deviceSettings = nullptr;
        for (int i = 0; i < getNumChildComponents(); ++i)
        {
            Component* child = getChildComponent (i);
            if (auto* combo = dynamic_cast<ComboBox*> (child))
                deviceType = combo;
            else if (child->getWidth() >= getWidth() - 1
                     && child->getHeight() > 100)
                deviceSettings = child;
        }

        if (deviceType != nullptr && deviceSettings != nullptr)
        {
            const int typeTop = 8;
            deviceType->setTopLeftPosition (deviceType->getX(), typeTop);
            deviceSettings->setTopLeftPosition (
                deviceSettings->getX(), deviceType->getBottom() + 12);

            compactAdvancedRowSpacing (*deviceSettings);
        }

        int contentBottom = 0;
        for (int i = 0; i < getNumChildComponents(); ++i)
            contentBottom = jmax (contentBottom, getChildComponent (i)->getBottom());

        const int contentHeight = contentBottom + 8;
        if (contentHeight != getHeight())
            setSize (getWidth(), contentHeight);
    }

private:
    static void shiftChildrenFromY (Component& parent, int y, int amount)
    {
        for (int i = 0; i < parent.getNumChildComponents(); ++i)
        {
            Component* const child = parent.getChildComponent (i);
            if (child->getY() >= y)
                child->setTopLeftPosition (child->getX(), child->getY() - amount);
        }
    }

    void compactAdvancedRowSpacing (Component& deviceSettings)
    {
        int lastChannelListBottom = -1;
        for (int i = 0; i < deviceSettings.getNumChildComponents(); ++i)
            if (auto* list = dynamic_cast<ListBox*> (deviceSettings.getChildComponent (i)))
                lastChannelListBottom = jmax (lastChannelListBottom, list->getBottom());

        if (lastChannelListBottom < 0)
            return;

        int sampleRateTop = deviceSettings.getHeight();
        for (int i = 0; i < deviceSettings.getNumChildComponents(); ++i)
            if (auto* combo = dynamic_cast<ComboBox*> (deviceSettings.getChildComponent (i)))
                if (combo->getY() >= lastChannelListBottom)
                    sampleRateTop = jmin (sampleRateTop, combo->getY());

        if (sampleRateTop == deviceSettings.getHeight())
            return;

        const int rowSpacing = getItemHeight() / 4;
        shiftChildrenFromY (deviceSettings, sampleRateTop - rowSpacing,
                           rowSpacing * 2);

        int bufferSizeBottom = -1;
        for (int i = 0; i < deviceSettings.getNumChildComponents(); ++i)
            if (auto* combo = dynamic_cast<ComboBox*> (deviceSettings.getChildComponent (i)))
                if (combo->getY() >= sampleRateTop)
                    bufferSizeBottom = jmax (bufferSizeBottom, combo->getBottom());

        int buttonsTop = deviceSettings.getHeight();
        for (int i = 0; i < deviceSettings.getNumChildComponents(); ++i)
            if (auto* button = dynamic_cast<TextButton*> (deviceSettings.getChildComponent (i)))
                if (button->getY() >= bufferSizeBottom)
                    buttonsTop = jmin (buttonsTop, button->getY());

        if (buttonsTop < deviceSettings.getHeight())
            shiftChildrenFromY (deviceSettings, buttonsTop, rowSpacing);
    }
};

class AudioSettingsViewport : public Viewport
{
public:
    std::function<void(bool, bool)> onScrollStateChanged;

    void visibleAreaChanged (const juce::Rectangle<int>& visibleArea) override
    {
        Component* const content = getViewedComponent();
        if (content != nullptr && onScrollStateChanged)
            onScrollStateChanged (visibleArea.getY() > 0,
                                  visibleArea.getBottom() < content->getHeight());
    }
};

class AudioScrollIndicator : public Component
{
public:
    AudioScrollIndicator()
    {
        setInterceptsMouseClicks (false, false);
    }

    void setScrollState (bool canScrollUp_, bool canScrollDown_)
    {
        if (canScrollUp != canScrollUp_ || canScrollDown != canScrollDown_)
        {
            canScrollUp = canScrollUp_;
            canScrollDown = canScrollDown_;
            repaint();
        }
    }

    void paint (Graphics& g) override
    {
        if (canScrollUp)
        {
            ColourGradient fade (Colour (0xf2FFFFFF), 0.0f, 0.0f,
                                 Colour (0x00FFFFFF), 0.0f, 22.0f, false);
            g.setGradientFill (fade);
            g.fillRect (getLocalBounds().removeFromTop (22));
        }

        if (canScrollDown)
        {
            const int hintHeight = 28;
            const int hintTop = getHeight() - hintHeight;
            ColourGradient fade (Colour (0x00FFFFFF), 0.0f, (float) hintTop,
                                 Colour (0xf2FFFFFF), 0.0f, (float) getHeight(), false);
            g.setGradientFill (fade);
            g.fillRect (0, hintTop, getWidth(), hintHeight);

            Path chevron;
            const float x = (float) getWidth() - 12.0f;
            const float y = (float) getHeight() - 13.0f;
            chevron.startNewSubPath (x - 3.0f, y - 2.0f);
            chevron.lineTo (x, y + 1.0f);
            chevron.lineTo (x + 3.0f, y - 2.0f);
            g.strokePath (chevron, PathStrokeType (1.5f, PathStrokeType::curved,
                                                    PathStrokeType::rounded));
        }
    }

private:
    bool canScrollUp = false;
    bool canScrollDown = false;
};

class LightHostSettingsView : public Component,
    private Button::Listener
{
public:
    explicit LightHostSettingsView (IconMenu& owner_)
        : owner (owner_),
          audioDeviceSelector (owner_.getDeviceManager())
    {
        setOpaque (false);
        audioDeviceSelector.setItemHeight (44);
        audioViewport.setViewedComponent (&audioDeviceSelector, false);
        audioViewport.setScrollBarsShown (true, false);
        audioViewport.setScrollBarThickness (9);
        audioViewport.setColour (ScrollBar::thumbColourId, Colour (0xffA8A4B8));
        audioViewport.setColour (ScrollBar::backgroundColourId, Colour (0xffF4F3FB));
        audioViewport.onScrollStateChanged = [this] (bool canScrollUp, bool canScrollDown)
        {
            audioScrollIndicator.setScrollState (canScrollUp, canScrollDown);
        };
        addAndMakeVisible (audioViewport);
        addAndMakeVisible (audioScrollIndicator);
        restartAudioButton.setButtonText("Restart audio");
        restartAudioButton.setTooltip("Restart the audio device and reload plugins, keeping their settings");
        restartAudioButton.addListener(this);
        addAndMakeVisible(restartAudioButton);

        appTitle.setText ("App", dontSendNotification);
        appTitle.setFont (Font (20.0f, Font::bold));
        appTitle.setColour (Label::textColourId, Colour (0xff1D1B2E));
        addAndMakeVisible (appTitle);

        runOnStartupButton.addListener (this);
        addAndMakeVisible (runOnStartupButton);
        invertIconButton.addListener (this);
        addAndMakeVisible (invertIconButton);

        importButton.setButtonText ("Import backup...");
        importButton.addListener (this);
        addAndMakeVisible (importButton);
        exportButton.setButtonText ("Export backup...");
        exportButton.addListener (this);
        addAndMakeVisible (exportButton);

        deleteButton.setButtonText ("Delete plugin states");
        deleteButton.setColour (TextButton::textColourOnId, Colour (0xffC42B2B));
        deleteButton.setColour (TextButton::textColourOffId, Colour (0xffC42B2B));
        deleteButton.addListener (this);
        addAndMakeVisible (deleteButton);

#if JUCE_WINDOWS
        bool enabled = false;
        if (getRunOnStartupEnabled (enabled))
            runOnStartupButton.setToggleState (enabled, dontSendNotification);
#endif

        const String iconColour = getAppProperties().getUserSettings()->getValue ("icon");
        invertIconButton.setToggleState (iconColour.equalsIgnoreCase ("black"),
                                         dontSendNotification);
    }

    void paint (Graphics& g) override
    {
        const Colour surface (0xffFFFFFF);
        const Colour border (0xffE3E1F0);
        g.setColour (surface);
        g.fillRoundedRectangle (audioCard.toFloat(), 14.0f);
        g.fillRoundedRectangle (appCard.toFloat(), 14.0f);
        g.setColour (border);
        g.drawRoundedRectangle (audioCard.toFloat().reduced (0.5f), 14.0f, 1.0f);
        g.drawRoundedRectangle (appCard.toFloat().reduced (0.5f), 14.0f, 1.0f);
        g.setColour (Colour (0xff1D1B2E));
        g.setFont (Font (20.0f, Font::bold));
        g.drawText ("Audio", audioCard.withTrimmedLeft (18).withTrimmedTop (14)
                    .withHeight (28), Justification::centredLeft);
        g.setColour (Colour (0xffEEECF7));
        g.drawHorizontalLine (appCard.getY() + 56, (float) appCard.getX() + 18.0f,
                              (float) appCard.getRight() - 18.0f);
        g.drawHorizontalLine (appCard.getY() + 168, (float) appCard.getX() + 18.0f,
                              (float) appCard.getRight() - 18.0f);
    }

    void resized() override
    {
        juce::Rectangle<int> area (getLocalBounds().reduced (12));
        const int gap = 12;
        const int cardWidth = (area.getWidth() - gap) * 63 / 100;
        audioCard = area.removeFromLeft (cardWidth);
        area.removeFromLeft (gap);
        appCard = area;
        restartAudioButton.setBounds(audioCard.getRight() - 136, audioCard.getY() + 14, 120, 28);

        juce::Rectangle<int> audioContents = audioCard.reduced (16);
        audioContents.removeFromTop (34);
        audioViewport.setBounds (audioContents);
        audioScrollIndicator.setBounds (audioContents);
        audioDeviceSelector.setSize (
            jmax (0, audioViewport.getWidth() - audioViewport.getScrollBarThickness()),
            jmax (audioViewport.getHeight(), audioDeviceSelector.getHeight()));

        juce::Rectangle<int> appContents = appCard.reduced (18);
        appTitle.setBounds (appContents.removeFromTop (34));
        appContents.removeFromTop (12);
        runOnStartupButton.setBounds (appContents.removeFromTop (44));
        appContents.removeFromTop (8);
        invertIconButton.setBounds (appContents.removeFromTop (44));
        appContents.removeFromTop (18);
        juce::Rectangle<int> backupButtons = appContents.removeFromTop (44);
        const int importWidth = (backupButtons.getWidth() - 8) / 2;
        importButton.setBounds (backupButtons.removeFromLeft (importWidth));
        backupButtons.removeFromLeft (8);
        exportButton.setBounds (backupButtons);
        appContents.removeFromTop (12);
        deleteButton.setBounds (appContents.removeFromTop (44));
    }

    void buttonClicked (Button* button) override
    {
        if (button == &restartAudioButton)
        {
            owner.restartAudio();
            return;
        }
        if (button == &runOnStartupButton)
        {
#if JUCE_WINDOWS
            toggleRunOnStartup();
            bool enabled = false;
            if (getRunOnStartupEnabled (enabled))
                runOnStartupButton.setToggleState (enabled, dontSendNotification);
#endif
            return;
        }

        if (button == &invertIconButton)
        {
            owner.toggleIconColor();
            return;
        }

        if (button == &importButton)
        {
            if (owner.importBackup())
                restartAfterSettingsImport();
            return;
        }

        if (button == &exportButton)
        {
            owner.exportBackup();
            return;
        }

        if (button == &deleteButton)
        {
            owner.deletePluginStates();
            owner.loadActivePlugins();
        }
    }

private:
    IconMenu& owner;
    LightHostAudioDeviceSelector audioDeviceSelector;
    AudioSettingsViewport audioViewport;
    AudioScrollIndicator audioScrollIndicator;
    Label appTitle;
    LightHostSwitch runOnStartupButton {"Run on startup"};
    LightHostSwitch invertIconButton {"Invert icon color"};
    TextButton importButton, exportButton, deleteButton, restartAudioButton;
    juce::Rectangle<int> audioCard, appCard;
};

class LightHostAboutView : public Component
{
public:
    LightHostAboutView()
    {
        title.setText ("Light Host", dontSendNotification);
        title.setFont (Font (22.0f, Font::bold));
        addAndMakeVisible (title);

        version.setText ("Version " + String (ProjectInfo::versionString), dontSendNotification);
        version.setFont (Font (15.0f));
        version.setColour (Label::textColourId, Colour (0xff6B6880));
        addAndMakeVisible (version);

        credit.setText ("Built with JUCE and designed for a modern single-window workflow.", dontSendNotification);
        credit.setFont (Font (14.0f));
        credit.setColour (Label::textColourId, Colour (0xff6B6880));
        addAndMakeVisible (credit);
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xffF4F3FB));
    }

    void resized() override
    {
        juce::Rectangle<int> area (getLocalBounds().reduced (28));
        title.setBounds (area.removeFromTop (28));
        version.setBounds (area.removeFromTop (24));
        credit.setBounds (area.removeFromTop (22));
    }

private:
    Label title, version, credit;
};

class StatusDotComponent : public Component
{
public:
    void paint (Graphics& g) override
    {
        g.setColour (Colour (0xff1F9D55));
        g.fillEllipse (getLocalBounds().toFloat().reduced (1.0f));
    }
};

class IconMenu::PluginManagementComponent : public Component,
	public DragAndDropContainer, public DragAndDropTarget,
	private ListBoxModel, private Button::Listener
{
public:
	PluginManagementComponent(IconMenu& owner_)
		: owner(owner_), activePlugins("Active Plugins", this),
		  addButton("+ Add to Active Plugins"),
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

		activePlugins.setRowHeight(34);
		addAndMakeVisible(activePlugins);
		activePlugins.addMouseListener(this, true);

		const File deadMansPedalFile(getAppProperties().getUserSettings()
			->getFile().getSiblingFile("RecentlyCrashedPluginsList"));
		availablePlugins = new PluginListComponent(owner.formatManager,
			owner.knownPluginList, deadMansPedalFile,
			getAppProperties().getUserSettings());
		availablePlugins->setOptionsButtonText("Options...");
		addAndMakeVisible(availablePlugins);
		availablePlugins->getTableListBox().addMouseListener(this, true);

		activeEmptyState.setText(
			"No active plugins. Add one from Available Plugins.",
			dontSendNotification);
		activeEmptyState.setJustificationType(Justification::centred);
		activeEmptyState.setColour(Label::textColourId, Colours::darkgrey);
		activeEmptyState.setInterceptsMouseClicks(false, false);
		addAndMakeVisible(activeEmptyState);

		addAndMakeVisible(addButton);
		addButton.addListener(this);

		refreshActivePlugins();
		setSize(1000, 600);
	}

	void refreshActivePlugins()
	{
		activePluginRows = owner.getOrderedActivePlugins();
		activePlugins.updateContent();
		activeEmptyState.setVisible(owner.activePluginList.getNumTypes() == 0);
		updateAddButton();
		activePlugins.repaint();
	}

	void mouseUp(const MouseEvent&) override
	{
		dragHandleRow = -1;
		updateAddButton();
	}

	void mouseDown(const MouseEvent& event) override
	{
		if (! activePlugins.isParentOf(event.eventComponent)
			&& event.eventComponent != &activePlugins)
			return;

		const MouseEvent listEvent = event.getEventRelativeTo(&activePlugins);
		const int row = activePlugins.getRowContainingPosition(listEvent.x, listEvent.y);
		dragHandleRow = event.mods.isLeftButtonDown()
			&& isDragHandlePosition(listEvent.getPosition()) ? row : -1;
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

		activePlugins.setBounds(activeArea);
		activeEmptyState.setBounds(activeArea);
		bypassHeading.setBounds(activePlugins.getRight() - 61,
			activeGroup.getY(), 44, 16);

		addButton.setBounds(availableArea.removeFromBottom(44));
		availableArea.removeFromBottom(4);
		availablePlugins->setBounds(availableArea);

	}

private:
	int getNumRows() override
	{
		return (int) activePluginRows.size();
	}

	void paintListBoxItem(int row, Graphics& graphics, int width, int height,
		bool rowIsSelected) override
	{
		if (row < 0 || row >= (int) activePluginRows.size())
			return;

		if (rowIsSelected)
			graphics.fillAll(findColour(TextEditor::highlightColourId));
		else if (row == hoveredRow)
			graphics.fillAll(findColour(TextEditor::highlightColourId).withAlpha(0.2f));
		else
			graphics.fillAll(findColour(ListBox::backgroundColourId));

		const std::vector<PluginDescription>& plugins = activePluginRows;
		const String displayName = getDisplayNameForPlugin (plugins[row]);
		String name = String(row + 1) + ". " + displayName;
		const bool bypassed = getAppProperties().getUserSettings()->getBoolValue(
			IconMenu::getKey("bypass", plugins[row]), false);
		graphics.setColour(findColour(rowIsSelected
			? TextEditor::highlightedTextColourId : ListBox::textColourId));

		const int gripWidth = 28;
		graphics.setColour (Colour (0xffA8A4B8));
		for (int dotRow = 0; dotRow < 3; ++dotRow)
			for (int dotColumn = 0; dotColumn < 2; ++dotColumn)
				graphics.fillEllipse (8.0f + dotColumn * 4.0f,
					height * 0.5f - 5.0f + dotRow * 5.0f, 2.0f, 2.0f);

		graphics.setColour(findColour(rowIsSelected
			? TextEditor::highlightedTextColourId : ListBox::textColourId));
		graphics.drawText(name, gripWidth, 0, width - gripWidth - 80, height,
			Justification::centredLeft);

		const juce::Rectangle<int> bypassBounds = getBypassBounds(width, height);
		graphics.setColour(bypassed ? Colour (0xff5B45D6) : Colours::white);
		graphics.fillRoundedRectangle(bypassBounds.toFloat(), 4.0f);
		graphics.setColour(bypassed ? Colour (0xff5B45D6)
			: Colour (0xff8F8AA8));
		graphics.drawRoundedRectangle(bypassBounds.toFloat(), 4.0f, 1.5f);
		if (bypassed)
		{
			graphics.setColour(Colours::white);
			graphics.drawLine((float) bypassBounds.getX() + 3.0f,
				(float) bypassBounds.getY() + bypassBounds.getHeight() * 0.55f,
				(float) bypassBounds.getX() + bypassBounds.getWidth() * 0.42f,
				(float) bypassBounds.getBottom() - 4.0f, 2.0f);
			graphics.drawLine((float) bypassBounds.getX() + bypassBounds.getWidth() * 0.42f,
				(float) bypassBounds.getBottom() - 4.0f,
				(float) bypassBounds.getRight() - 3.0f,
				(float) bypassBounds.getY() + 4.0f, 2.0f);
		}

		if (hoveredRow == row)
		{
			const juce::Rectangle<int> menuBounds = getMenuBounds (width, height);
			if (hoveredAction == menuActivePluginAction)
			{
				graphics.setColour (Colour (0xffDCD6F4));
				graphics.fillEllipse (menuBounds.toFloat().expanded (3.0f));
			}
			graphics.setColour (Colour (0xff6B6880));
			for (int dot = 0; dot < 3; ++dot)
				graphics.fillEllipse (menuBounds.getCentreX() - 1.5f,
					menuBounds.getY() + 2.0f + dot * 5.0f, 3.0f, 3.0f);
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
			showPluginMenu (row, event.getScreenPosition());
			return;
		}

		const ActivePluginHoveredAction action =
			getActionAtPosition(listEvent.getPosition(), row);
		if (action == menuActivePluginAction)
		{
			activePlugins.selectRow (row, true, true);
			const juce::Rectangle<int> menuAnchor (event.getScreenPosition().x,
				event.getScreenPosition().y, 1, 1);
			showPluginMenu (row, menuAnchor.getPosition());
			return;
		}

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
		const std::vector<PluginDescription> plugins = owner.getOrderedActivePlugins();
		if (row < 0 || row >= (int) plugins.size())
			return;

		if (AlertWindow::showOkCancelBox(AlertWindow::QuestionIcon,
			"Remove Plugin", "Remove " + getDisplayNameForPlugin (plugins[row])
				+ " from your active plugins?",
			"Remove", "Cancel"))
		{
			owner.performPluginAction(owner.INDEX_DELETE + row);
			refreshActivePlugins();
		}
	}

	void showPluginMenu (int row, Point<int> screenPosition)
	{
		if (row < 0 || row >= owner.activePluginList.getNumTypes())
			return;

		const Point<int> localPosition = getLocalPoint (nullptr, screenPosition);
		const juce::Rectangle<int> menuAnchor (
			localPosition.x, localPosition.y, 1, 1);
		PluginActionMenuContent* content = new PluginActionMenuContent(
			[this, row](int action)
			{
				if (action == 1)
					renamePlugin(row);
				else if (action == 2)
					confirmRemovePlugin(row);
			});
		CallOutBox& callout = CallOutBox::launchAsynchronously(
			content, menuAnchor, this);
		callout.setLookAndFeel(&pluginActionMenuLookAndFeel);
		callout.setArrowSize(0.0f);
	}

	void renamePlugin (int row)
	{
		const std::vector<PluginDescription> plugins = owner.getOrderedActivePlugins();
		if (row < 0 || row >= (int) plugins.size())
			return;

		const PluginDescription& plugin = plugins[row];
		AlertWindow renameDialog ("Rename plugin", "Choose a display name for this plugin.",
			AlertWindow::NoIcon, this);
		renameDialog.addTextEditor ("name", getDisplayNameForPlugin (plugin), "Name:");
		renameDialog.addButton ("Save", 1, KeyPress (KeyPress::returnKey));
		renameDialog.addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
		if (renameDialog.runModalLoop() != 1)
			return;

		const String newName = renameDialog.getTextEditorContents ("name").trim();
		if (newName.isEmpty())
			return;

		PropertiesFile* settings = getAppProperties().getUserSettings();
		const String key = getDisplayNameKey (plugin);
		if (newName == plugin.name)
			settings->removeValue (key);
		else
			settings->setValue (key, newName);
		settings->saveIfNeeded();
		activePlugins.repaint();
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
			const std::vector<PluginDescription> plugins = owner.getOrderedActivePlugins();
			if (row >= 0 && row < (int) plugins.size()
				&& getAppProperties().getUserSettings()->getBoolValue(
					IconMenu::getKey("bypass", plugins[row])))
				return "Enable this plugin";
			return "Bypass this plugin";
		}

		return String();
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
		if (rowsToDescribe.size() != 1 || dragHandleRow != rowsToDescribe[0])
			return var();

		const int row = rowsToDescribe[0];
		const std::vector<PluginDescription> plugins = owner.getOrderedActivePlugins();
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
		const std::vector<PluginDescription> plugins = owner.getOrderedActivePlugins();
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
		return juce::Rectangle<int>(width - 48, (height - 18) / 2, 18, 18);
	}

	juce::Rectangle<int> getMenuBounds (int width, int height) const
	{
		return juce::Rectangle<int> (width - 20, (height - 18) / 2, 12, 18);
	}

	bool isDragHandlePosition (Point<int> position) const
	{
		return position.x >= 0 && position.x < 28
			&& activePlugins.getRowContainingPosition (position.x, position.y) >= 0;
	}

	ActivePluginHoveredAction getActionAtPosition(Point<int> position, int row) const
	{
		const juce::Rectangle<int> rowBounds = activePlugins.getRowPosition(row, true);
		const Point<int> rowPosition(position.x - rowBounds.getX(),
			position.y - rowBounds.getY());
		if (getBypassBounds(rowBounds.getWidth(), rowBounds.getHeight())
			.contains(rowPosition))
			return bypassActivePluginAction;
		if (getMenuBounds (rowBounds.getWidth(), rowBounds.getHeight())
			.contains (rowPosition))
			return menuActivePluginAction;
		return noActivePluginAction;
	}

	String getDisplayNameKey (const PluginDescription& plugin) const
	{
		return PluginState::key("display-name", plugin);
	}

	String getDisplayNameForPlugin (const PluginDescription& plugin) const
	{
		return getAppProperties().getUserSettings()->getValue (
			getDisplayNameKey (plugin), plugin.name);
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
	Label activeEmptyState, bypassHeading;
	std::vector<PluginDescription> activePluginRows;
	ListBox activePlugins;
	TextButton addButton;
	ScopedPointer<PluginListComponent> availablePlugins;
	int hoveredRow;
	ActivePluginHoveredAction hoveredAction;
	int dragTargetIndex;
	int dragHandleRow = -1;
};

class LightHostMainWindowContent : public Component,
    private Button::Listener
{
public:
    enum ViewType
    {
        pluginsView,
        settingsView,
        aboutView
    };

    explicit LightHostMainWindowContent (IconMenu& owner_)
        : owner (owner_)
    {
        setOpaque (true);

        titleBar.setText ("Light Host", dontSendNotification);
        titleBar.setFont (Font (20.0f, Font::bold));
        titleBar.setColour (Label::textColourId, Colour (0xff1D1B2E));
        addAndMakeVisible (titleBar);

        icon.setImage (ImageFileFormat::loadFrom (BinaryData::menu_icon_png, BinaryData::menu_icon_pngSize));
        icon.setImagePlacement (RectanglePlacement::centred);
        icon.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (icon);

        pluginsButton.setButtonText ("Plugins");
        pluginsButton.setColour (TextButton::textColourOnId, Colour (0xff3B2AA8));
        pluginsButton.setColour (TextButton::textColourOffId, Colour (0xff1D1B2E));
        pluginsButton.setClickingTogglesState (true);
        pluginsButton.setToggleState (true, dontSendNotification);
        addAndMakeVisible (pluginsButton);
        pluginsButton.addListener (this);

        settingsButton.setButtonText ("Settings");
        settingsButton.setColour (TextButton::textColourOnId, Colour (0xff3B2AA8));
        settingsButton.setColour (TextButton::textColourOffId, Colour (0xff1D1B2E));
        settingsButton.setClickingTogglesState (true);
        addAndMakeVisible (settingsButton);
        settingsButton.addListener (this);

        aboutButton.setButtonText ("About");
        aboutButton.setColour (TextButton::textColourOnId, Colour (0xff3B2AA8));
        aboutButton.setColour (TextButton::textColourOffId, Colour (0xff1D1B2E));
        aboutButton.setClickingTogglesState (true);
        addAndMakeVisible (aboutButton);
        aboutButton.addListener (this);

        readyStatus.setText ("Ready", dontSendNotification);
        readyStatus.setFont (Font (13.0f));
        readyStatus.setColour (Label::textColourId, Colour (0xff6B6880));
        addAndMakeVisible (readyStatus);

        readyDot.setSize (8, 8);
        addAndMakeVisible (readyDot);

        pluginsPanel = new IconMenu::PluginManagementComponent (owner);
        aboutPanel = new LightHostAboutView();

        addAndMakeVisible (pluginsPanel);
        addAndMakeVisible (aboutPanel);

        setView (pluginsView);
    }

    void setView (ViewType newView)
    {
        pluginsButton.setToggleState (newView == pluginsView, dontSendNotification);
        settingsButton.setToggleState (newView == settingsView, dontSendNotification);
        aboutButton.setToggleState (newView == aboutView, dontSendNotification);

        pluginsPanel->setVisible (newView == pluginsView);
        if (newView == settingsView && settingsPanel == nullptr)
        {
            settingsPanel = new LightHostSettingsView(owner);
            addChildComponent(settingsPanel);
            resized();
        }
        if (settingsPanel != nullptr)
            settingsPanel->setVisible(newView == settingsView);
        aboutPanel->setVisible (newView == aboutView);
        repaint();
    }

    void refreshActivePlugins()
    {
        pluginsPanel->refreshActivePlugins();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xffF4F3FB));
    }

    void resized() override
    {
        juce::Rectangle<int> area (getLocalBounds());
        const int headerHeight = 48;
        const int sidebarWidth = 156;

        juce::Rectangle<int> headerArea = area.removeFromTop (headerHeight);
        icon.setBounds (headerArea.removeFromLeft (40).withTrimmedLeft (10).withTrimmedRight (10).withTrimmedTop (8).withTrimmedBottom (8));
        titleBar.setBounds (headerArea.withTrimmedLeft (8));

        juce::Rectangle<int> bodyArea (area);
        juce::Rectangle<int> sidebarArea = bodyArea.removeFromLeft (sidebarWidth);

        sidebarArea.reduce (8, 8);
        pluginsButton.setBounds (sidebarArea.removeFromTop (46));
        settingsButton.setBounds (sidebarArea.removeFromTop (46));
        aboutButton.setBounds (sidebarArea.removeFromTop (46));

        sidebarArea.removeFromTop (12);
        juce::Rectangle<int> statusArea = sidebarArea.removeFromBottom (22);
        readyDot.setBounds (statusArea.removeFromLeft (10).withTrimmedLeft (1).withTrimmedRight (1).withTrimmedTop (6).withTrimmedBottom (6));
        readyStatus.setBounds (statusArea);

        const juce::Rectangle<int> contentArea (bodyArea.reduced (12, 12));
        pluginsPanel->setBounds (contentArea);
        if (settingsPanel != nullptr)
            settingsPanel->setBounds (contentArea);
        aboutPanel->setBounds (contentArea);
    }

    void buttonClicked (Button* button) override
    {
        if (button == &pluginsButton)
            setView (pluginsView);
        else if (button == &settingsButton)
            setView (settingsView);
        else if (button == &aboutButton)
            setView (aboutView);
    }

private:
    IconMenu& owner;
    ImageComponent icon;
    Label titleBar;
    TextButton pluginsButton, settingsButton, aboutButton;
    Label readyStatus;
    StatusDotComponent readyDot;
    ScopedPointer<IconMenu::PluginManagementComponent> pluginsPanel;
    ScopedPointer<Component> settingsPanel;
    ScopedPointer<Component> aboutPanel;
};

class IconMenu::PluginListWindow : public DocumentWindow
{
public:
	explicit PluginListWindow(IconMenu& owner_)
		: DocumentWindow("Light Host", Colours::white,
			DocumentWindow::minimiseButton | DocumentWindow::closeButton),
		content(new LightHostMainWindowContent(owner_))
	{
		setContentOwned(content, true);
		setUsingNativeTitleBar(true);
		setResizable(true, true);
		setResizeLimits(1000, 520, 1400, 1000);
		setTopLeftPosition(60, 60);
		setSize(1000, 520);
		setVisible(true);
	}

	void refreshActivePlugins()
	{
		if (content != nullptr)
			content->refreshActivePlugins();
	}

	void showPluginsView()
	{
		if (content != nullptr)
			content->setView(LightHostMainWindowContent::pluginsView);
		setVisible(true);
		toFront(true);
	}

	void closeButtonPressed()
	{
		setVisible(false);
	}

private:
	LightHostMainWindowContent* content; // Owned by DocumentWindow.
};

IconMenu::IconMenu() : INDEX_EDIT(1000000), INDEX_BYPASS(2000000), INDEX_DELETE(3000000),
    player(getAppProperties().getUserSettings()->getFile().withFileExtension("audio.log"))
{
    // Initialization
	settingsImported = false;
	PluginState::relocatePortableSettings(*getAppProperties().getUserSettings(),
		File::getSpecialLocation(File::currentExecutableFile).getParentDirectory());
	lastLeftClickTime = 0;
    formatManager.addDefaultFormats();
	#if JUCE_WINDOWS
	refreshStartupRegistration();
	x = y = 0;
	#endif
    // Audio device
    ScopedPointer<XmlElement> savedAudioState (getAppProperties().getUserSettings()->getXmlValue("audioDeviceState"));
    if (savedAudioState != nullptr)
        AudioSetup::limitSavedChannelsToStereo(*savedAudioState);
    const String audioError = deviceManager.initialise(2, 2, savedAudioState, true);
    if (audioError.isNotEmpty())
        player.audioDeviceError(audioError);
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
    PluginState::migrateLegacySettings(*getAppProperties().getUserSettings(),
        PluginState::collectPlugins(knownPluginList, activePluginList));
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
    if (!settingsImported)
    {
        ScopedPointer<XmlElement> audioState(deviceManager.createStateXml());
        getAppProperties().getUserSettings()->setValue("audioDeviceState", audioState);
    }
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

void IconMenu::toggleIconColor()
{
    String color = getAppProperties().getUserSettings()->getValue("icon");
    const String nextColor = color.equalsIgnoreCase("black") ? "white" : "black";
    getAppProperties().getUserSettings()->setValue("icon", nextColor);
    getAppProperties().saveIfNeeded();
    setIcon();
}

void IconMenu::loadActivePlugins()
{
	PluginWindow::closeAllCurrentlyOpenWindows();
	player.setProcessor(nullptr);
    graph.clear();
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioInputNode), PluginChain::inputNodeId);
    graph.addNode(new AudioProcessorGraph::AudioGraphIOProcessor(AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode), PluginChain::outputNodeId);
	const std::vector<PluginDescription> plugins = getOrderedActivePlugins();
	StringArray loadingErrors;
	// NOTE: Node ids cannot begin at 0.
    for (int i = 1; i <= (int) plugins.size(); i++)
    {
        const PluginDescription& plugin = plugins[i - 1];
        String errorMessage;
        const double sampleRate = graph.getSampleRate() > 0.0 ? graph.getSampleRate() : 48000.0;
        const int blockSize = graph.getBlockSize() > 0 ? graph.getBlockSize() : 512;
        AudioPluginInstance* instance = formatManager.createPluginInstance(plugin, sampleRate, blockSize, errorMessage);
        if (instance == nullptr)
        {
            Logger::writeToLog("Could not load plugin " + plugin.name + ": " + errorMessage);
            loadingErrors.add(plugin.name + ": " + errorMessage);
            continue;
        }
		String pluginUid = getKey("state", plugin);
        String savedPluginState = getAppProperties().getUserSettings()->getValue(pluginUid);
        MemoryBlock savedPluginBinary;
        savedPluginBinary.fromBase64Encoding(savedPluginState);
        const size_t savedPluginStateSize = savedPluginBinary.getSize();
        if (savedPluginStateSize > static_cast<size_t>(INT_MAX))
            Logger::writeToLog("Plugin state is too large to restore: " + plugin.name);
        else if (savedPluginStateSize > 0)
            instance->setStateInformation(savedPluginBinary.getData(),
                static_cast<int>(savedPluginStateSize));
        if (!PluginChain::supportsStereo(*instance))
        {
            loadingErrors.add(plugin.name + ": only plugins with stereo input and output are supported.");
            delete instance;
            continue;
        }
        graph.addNode(instance, i);
    }
    updatePluginConnections();
	player.setProcessor(&graph);
    if (!loadingErrors.isEmpty())
    {
        player.audioDeviceError(loadingErrors.joinIntoString("\n"));
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Plugins skipped",
            loadingErrors.joinIntoString("\n") + "\nAudio continues through the remaining plugins.");
    }
}

void IconMenu::restartAudio()
{
    if (restartingAudio)
        return;
    const ScopedValueSetter<bool> restarting(restartingAudio, true);
    deviceManager.closeAudioDevice();
    // Stop processing before asking plugins for state, then recreate their DSP state.
    savePluginStates();
    loadActivePlugins();
    deviceManager.restartLastAudioDevice();
    AudioIODevice* device = deviceManager.getCurrentAudioDevice();
    if (device == nullptr || !device->isOpen() || !device->isPlaying())
    {
        const String error = device != nullptr ? device->getLastError() : "No audio device is available.";
        player.audioDeviceError("Audio restart failed: " + error);
        AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Restart audio",
            "Could not restart audio. Check the selected device in Settings.\n" + error);
    }
}

void IconMenu::updatePluginConnections()
{
    const std::vector<PluginDescription> plugins = getOrderedActivePlugins();
    PropertiesFile* settings = getAppProperties().getUserSettings();
    const StringArray issues = PluginChain::reconnect(graph, (int) plugins.size(),
        [&](int index) { return settings->getBoolValue(getKey("bypass", plugins[index])); });
    if (!issues.isEmpty())
    {
        player.audioDeviceError(issues.joinIntoString("\n"));
        AlertWindow::showMessageBoxAsync(AlertWindow::WarningIcon, "Audio routing", issues.joinIntoString("\n"));
    }
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
	menu.setLookAndFeel(&trayMenuLookAndFeel);
    menu.addSectionHeader(JUCEApplication::getInstance()->getApplicationName());
    menu.addItem(menuOpenLightHost, "Open Light Host");
#if JUCE_WINDOWS
    bool runOnStartup = false;
    getRunOnStartupEnabled(runOnStartup);
    menu.addItem(menuRunOnStartup, "Run on startup", true, runOnStartup);
#endif
    menu.addSeparator();
    menu.addColouredItem(menuQuit, "Quit", Colour(0xffC42B2B));
	#if JUCE_MAC || JUCE_LINUX
    menu.showMenuAsync(PopupMenu::Options().withTargetComponent(this)
		.withMinimumWidth(240).withStandardItemHeight(36),
		ModalCallbackFunction::forComponent(menuInvocationCallback, this));
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
	menu.showMenuAsync(PopupMenu::Options().withTargetScreenArea(rect)
		.withMinimumWidth(240).withStandardItemHeight(36),
		ModalCallbackFunction::forComponent(menuInvocationCallback, this));
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
#if JUCE_MAC
        if (!PluginWindow::containsActiveWindows())
            Process::setDockIconVisible(false);
#endif
        return;
    }

    switch (id)
    {
    case menuOpenLightHost:
        im->reloadPlugins();
        break;
#if JUCE_WINDOWS
    case menuRunOnStartup:
        toggleRunOnStartup();
        break;
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
    // Also migrate settings for plugins rediscovered since startup.
    PluginState::migrateLegacySettings(*getAppProperties().getUserSettings(),
        PluginState::collectPlugins(knownPluginList, activePluginList));

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
	std::vector<PluginDescription> plugins = getOrderedActivePlugins();
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
		std::vector<PluginDescription> plugins = getOrderedActivePlugins();
		if (index < 0 || index >= (int) plugins.size())
			return;

		savePluginStates();
		const PluginDescription plugin = plugins[index];
		const String orderKey = getKey("order", plugin);
		int unsortedIndex = -1;
		for (int i = 0; i < activePluginList.getNumTypes(); ++i)
			if (plugin.isDuplicateOf(*activePluginList.getType(i)))
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
		std::vector<PluginDescription> plugins = getOrderedActivePlugins();
		if (index < 0 || index >= (int) plugins.size())
			return;

		const String key = getKey("bypass", plugins[index]);
		PropertiesFile* settings = getAppProperties().getUserSettings();
		settings->setValue(key, !settings->getBoolValue(key));
		settings->saveIfNeeded();
		updatePluginConnections();
	}
	else if (id >= INDEX_EDIT && id < INDEX_EDIT + 1000000)
	{
		const int nodeId = id - INDEX_EDIT + 1;
		if (const AudioProcessorGraph::Node::Ptr node = graph.getNodeForId(nodeId))
			if (PluginWindow* const window = PluginWindow::getWindowFor(node, PluginWindow::Normal))
			{
                window->setVisible(true);
				window->toFront(true);
			}
	}

	if (pluginListWindow != nullptr)
		pluginListWindow->refreshActivePlugins();
}

std::vector<PluginDescription> IconMenu::getOrderedActivePlugins()
{
    struct OrderedPlugin
    {
        PluginDescription plugin;
        int order;
    };
    std::vector<OrderedPlugin> ordered;
    ordered.reserve(activePluginList.getNumTypes());
    PropertiesFile* settings = getAppProperties().getUserSettings();
    for (int i = 0; i < activePluginList.getNumTypes(); ++i)
    {
        const PluginDescription& plugin = *activePluginList.getType(i);
        ordered.push_back({ plugin, settings->getIntValue(getKey("order", plugin)) });
    }
    // Keep plugins with duplicate or missing order values in their original order.
    std::stable_sort(ordered.begin(), ordered.end(),
        [](const OrderedPlugin& a, const OrderedPlugin& b) { return a.order < b.order; });
    std::vector<PluginDescription> list;
    list.reserve(ordered.size());
    for (const auto& entry : ordered)
        list.push_back(entry.plugin);
    return list;
}

String IconMenu::getKey(const String& type, const PluginDescription& plugin)
{
    return PluginState::key(type, plugin);
}

void IconMenu::deletePluginStates()
{
	std::vector<PluginDescription> list = getOrderedActivePlugins();
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		String pluginUid = getKey("state", list[i]);
        getAppProperties().getUserSettings()->removeValue(pluginUid);
    }
    getAppProperties().saveIfNeeded();
}

void IconMenu::savePluginStates()
{
	if (settingsImported)
		return;

	std::vector<PluginDescription> list = getOrderedActivePlugins();
    for (int i = 0; i < activePluginList.getNumTypes(); i++)
    {
		AudioProcessorGraph::Node* node = graph.getNodeForId(i + 1);
		if (node == nullptr)
			continue;
        AudioProcessor& processor = *node->getProcessor();
		String pluginUid = getKey("state", list[i]);
        MemoryBlock savedStateBinary;
        PluginState::capture(graph, processor, savedStateBinary);
        getAppProperties().getUserSettings()->setValue(pluginUid, savedStateBinary.toBase64Encoding());
    }
    getAppProperties().saveIfNeeded();
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
		File::getSpecialLocation(File::currentExecutableFile).getParentDirectory()
			.getChildFile("Light Host Backup.lightbak"),
		"*.lightbak");
	if (!chooser.browseForFileToSave(true))
		return;

	const File destination = chooser.getResult().withFileExtension(".lightbak");
	if (destination != chooser.getResult() && destination.exists()
		&& !AlertWindow::showOkCancelBox(AlertWindow::WarningIcon, "Export Backup",
			"The backup file already exists. Replace it?\n" + destination.getFullPathName(),
			"Replace", "Cancel"))
		return;
	if (destination == settings->getFile())
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Export Backup",
			"Choose a different file from the active settings file.");
		return;
	}

	ZipFile::Builder archive;
	XmlElement manifest("LIGHTHOST_BACKUP");
	manifest.setAttribute("version", 2);
	StringArray archivedPluginPaths;
	StringArray errors;
	int archivedPlugins = 0;

    const std::vector<PluginDescription> backupPlugins =
        PluginState::collectPlugins(knownPluginList, activePluginList);
	for (int i = 0; i < (int) backupPlugins.size(); ++i)
	{
		const PluginDescription* plugin = &backupPlugins[i];
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
		if (!PluginBackup::addFiles(archive, pluginFile, archiveRoot))
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
        pluginEntry->setAttribute("isDirectory", pluginFile.isDirectory());
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

	const TemporaryFile temporaryBackup(destination);
	const File temporaryFile = temporaryBackup.getFile();

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
	if (!archiveWritten || !temporaryBackup.overwriteTargetFileWithTemporary())
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
		File::getSpecialLocation(File::currentExecutableFile).getParentDirectory(), "*.lightbak");
	if (!chooser.browseForFileToOpen())
		return false;
	if (!chooser.getResult().hasFileExtension(".lightbak"))
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"Please select a .lightbak backup file.");
		return false;
	}

	ZipFile archive(chooser.getResult());
	const int manifestIndex = archive.getIndexOfFileName("manifest.xml");
	const int settingsIndex = archive.getIndexOfFileName("settings.xml");
	if (manifestIndex < 0 || settingsIndex < 0)
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"The selected file is not a Light Host backup.");
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
		|| !PluginBackup::supportsVersion(manifest->getIntAttribute("version"))
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
	if (!PluginBackup::hasValidPluginLists(importedSettings))
	{
		AlertWindow::showMessageBox(AlertWindow::WarningIcon, "Import Backup",
			"The backup contains invalid plugin settings. Your settings were not changed.");
		return false;
	}
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
        if (manifest->getIntAttribute("version") >= 2 && !pluginEntry->hasAttribute("isDirectory"))
        {
            errors.add("The backup is missing the plugin file layout.");
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
		FileSearchPath scanPaths =
			PluginListComponent::getLastSearchPath(*currentSettings, *format);
		for (int i = 0; i < localScanFormats.size(); ++i)
			if (localScanFormats[i].equalsIgnoreCase(formatName))
				scanPaths = FileSearchPath(localScanPaths[i]);
		if (pluginFileName.isEmpty()
			|| pluginFileName == "." || pluginFileName == ".."
			|| pluginFileName.containsChar('/') || pluginFileName.containsChar('\\'))
		{
			errors.add(formatName + ": no valid plugin scan location or filename is available.");
			continue;
		}

		const File restoreRoot = File::getSpecialLocation(File::currentExecutableFile)
			.getParentDirectory().getChildFile("VST Plugins");
		const File destinationPlugin = PluginBackup::restoreDestination(sourcePlugin, scanPaths, restoreRoot);
		if (!destinationPlugin.exists() && !destinationPlugin.isAChildOf(restoreRoot))
		{
			errors.add(formatName + ": the plugin destination is outside its scan location.");
			continue;
		}
		scanPaths.add(destinationPlugin.getParentDirectory());
		const String archivePrefix = archiveRoot + "/";
		int archivedFileCount = 0;
		bool unsafeArchivePath = false;
		for (int entryIndex = 0; entryIndex < archive.getNumEntries(); ++entryIndex)
		{
			const ZipFile::ZipEntry* entry = archive.getEntry(entryIndex);
			if (entry != nullptr && entry->filename.startsWith(archivePrefix))
			{
				++archivedFileCount;
				if (!PluginBackup::isSafePath(entry->filename.substring(archivePrefix.length())))
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
        const bool directory = PluginBackup::isDirectoryArchive(*pluginEntry, archive,
            archivePrefix, pluginFileName);
        if (!directory && (archivedFileCount != 1
            || archive.getIndexOfFileName(archivePrefix + pluginFileName) < 0))
        {
            errors.add(pluginFileName + ": backup has an invalid single-file plugin layout.");
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
			if (!PluginBackup::isSafePath(relativePath))
			{
				errors.add("The backup contains an unsafe plugin path.");
				pluginImportFailed = true;
				break;
			}

			const File target = PluginBackup::extractionTarget(destinationPlugin, relativePath, directory);
			if (target == File() || target.exists()
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
		"Backup imported. Light Host will now restart to apply the restored settings.");
	return true;
}

void IconMenu::reloadPlugins()
{
	if (pluginListWindow == nullptr)
		pluginListWindow = new PluginListWindow(*this);
	pluginListWindow->showPluginsView();
	pluginListWindow->toFront(true);
}
