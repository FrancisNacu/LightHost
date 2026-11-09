#include "../JuceLibraryCode/JuceHeader.h"
#include "IconMenu.hpp"

#if ! (JUCE_PLUGINHOST_VST || JUCE_PLUGINHOST_VST3 || JUCE_PLUGINHOST_AU)
 #error "If you're building the audio plugin host, you probably want to enable VST and/or AU support"
#endif

class LightHostLookAndFeel : public LookAndFeel_V3
{
public:
    LightHostLookAndFeel()
    {
        setColour (ResizableWindow::backgroundColourId, Colour (0xffF4F3FB));
        setColour (DocumentWindow::backgroundColourId, Colour (0xffF4F3FB));
        setColour (TextEditor::backgroundColourId, Colours::white);
        setColour (TextEditor::textColourId, Colour (0xff1D1B2E));
        setColour (TextEditor::highlightColourId, Colour (0xffE4DEFA));
        setColour (TextEditor::highlightedTextColourId, Colour (0xff3B2AA8));
        setColour (TextEditor::outlineColourId, Colour (0xffCFCBE6));
        setColour (TextButton::buttonColourId, Colour (0xffFFFFFF));
        setColour (TextButton::buttonOnColourId, Colour (0xff5B45D6));
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (TextButton::textColourOffId, Colour (0xff1D1B2E));
        setColour (ComboBox::backgroundColourId, Colour (0xffFFFFFF));
        setColour (ComboBox::outlineColourId, Colour (0xffCFCBE6));
        setColour (ComboBox::arrowColourId, Colour (0xff1D1B2E));
        setColour (PopupMenu::backgroundColourId, Colours::white);
        setColour (PopupMenu::textColourId, Colour (0xff1D1B2E));
        setColour (PopupMenu::headerTextColourId, Colour (0xff6B6880));
        setColour (ToggleButton::tickColourId, Colour (0xffFFFFFF));
        setColour (ToggleButton::textColourId, Colour (0xff1D1B2E));
        setColour (ListBox::backgroundColourId, Colour (0xffF4F3FB));
        setColour (ListBox::textColourId, Colour (0xff1D1B2E));
        setColour (ScrollBar::thumbColourId, Colour (0xffCFCBE6));
        setColour (ScrollBar::backgroundColourId, Colour (0xffF4F3FB));
        setColour (AlertWindow::backgroundColourId, Colour (0xffF4F3FB));
        setColour (AlertWindow::textColourId, Colour (0xff1D1B2E));
        setColour (HyperlinkButton::textColourId, Colour (0xff3B2AA8));
    }

    void drawButtonBackground (Graphics& g, Button& button, const Colour& background,
                               bool isMouseOverButton, bool isButtonDown) override
    {
        Colour fill = button.getToggleState() ? Colour (0xffE4DEFA) : background;
        if (isMouseOverButton)
            fill = fill.overlaidWith (Colour (0xff5B45D6).withAlpha (0.06f));
        if (isButtonDown)
            fill = fill.overlaidWith (Colour (0xff5B45D6).withAlpha (0.12f));

        const Rectangle<float> bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (button.getToggleState() ? Colour (0xffD1C9F3) : Colour (0xffCFCBE6));
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        if (button.hasKeyboardFocus (false))
        {
            g.setColour (Colour (0xff5B45D6));
            g.drawRoundedRectangle (bounds.reduced (2.0f), 6.0f, 1.0f);
        }
    }

    void drawComboBox (Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       ComboBox& box) override
    {
        const Rectangle<float> bounds (0.5f, 0.5f, (float) width - 1.0f,
                                       (float) height - 1.0f);
        g.setColour (box.findColour (ComboBox::backgroundColourId));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (box.hasKeyboardFocus (false) || isButtonDown
            ? Colour (0xff5B45D6) : box.findColour (ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        const float centreX = (float) buttonX + (float) buttonW * 0.5f;
        const float centreY = (float) buttonY + (float) buttonH * 0.5f;
        Path arrow;
        arrow.addTriangle (centreX - 4.0f, centreY - 1.0f,
                           centreX + 4.0f, centreY - 1.0f,
                           centreX, centreY + 3.0f);
        g.setColour (box.findColour (ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    void drawTickBox (Graphics& g, Component&,
                      float x, float y, float width, float height,
                      bool ticked, bool isEnabled, bool isMouseOverButton,
                      bool isButtonDown) override
    {
        const float side = jmin (14.0f, jmin (width, height));
        const Rectangle<float> box (x, y + (height - side) * 0.5f, side, side);
        const Colour accent (0xff5B45D6);

        g.setColour (ticked ? accent : Colours::white);
        g.fillRoundedRectangle (box.reduced (0.5f), 3.0f);
        g.setColour (ticked || isMouseOverButton || isButtonDown
            ? accent : Colour (0xffA8A4B8));
        g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.2f);

        if (ticked)
        {
            Path check;
            check.startNewSubPath (box.getX() + side * 0.22f, box.getY() + side * 0.52f);
            check.lineTo (box.getX() + side * 0.43f, box.getY() + side * 0.72f);
            check.lineTo (box.getX() + side * 0.79f, box.getY() + side * 0.30f);
            g.setColour (Colours::white);
            g.strokePath (check, PathStrokeType (2.0f, PathStrokeType::curved,
                                                 PathStrokeType::rounded));
        }

        if (! isEnabled)
        {
            g.setColour (Colours::white.withAlpha (0.45f));
            g.fillRoundedRectangle (box, 3.0f);
        }
    }
};

class PluginHostApp  : public JUCEApplication
{

public:
    PluginHostApp() {}

    void initialise (const String&) override
    {
        PropertiesFile::Options options;
        options.applicationName     = getApplicationName();
        options.filenameSuffix      = "settings";
        options.osxLibrarySubFolder = "Preferences";

        checkArguments(&options);

        appProperties = new ApplicationProperties();
        appProperties->setStorageParameters (options);

        LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        mainWindow = new IconMenu();
		#if JUCE_MAC
			Process::setDockIconVisible(false);
		#endif
    }

    void shutdown() override
    {
        mainWindow = nullptr;
        appProperties = nullptr;
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override
    {
        JUCEApplicationBase::quit();
    }

    const String getApplicationName() override       { return "Light Host"; }
    const String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override       {
        StringArray multiInstance = getParameter("-multi-instance");
        return multiInstance.size() == 2;
    }

    ScopedPointer<ApplicationProperties> appProperties;
    LightHostLookAndFeel lookAndFeel;

private:
    ScopedPointer<IconMenu> mainWindow;

    StringArray getParameter(String lookFor) {
        StringArray parameters = getCommandLineParameterArray();
        StringArray found;
        for (int i = 0; i < parameters.size(); ++i)
        {
            String param = parameters[i];
            if (param.contains(lookFor))
            {
                found.add(lookFor);
                int delimiter = param.indexOf(0, "=") + 1;
                String val = param.substring(delimiter);
                found.add(val);
                return found;
            }
        }
        return found;
    }

    void checkArguments(PropertiesFile::Options *options) {
        StringArray multiInstance = getParameter("-multi-instance");
        if (multiInstance.size() == 2)
            options->filenameSuffix = multiInstance[1] + "." + options->filenameSuffix;
    }
};

static PluginHostApp& getApp()                      { return *dynamic_cast<PluginHostApp*>(JUCEApplication::getInstance()); }
ApplicationProperties& getAppProperties()           { return *getApp().appProperties; }

START_JUCE_APPLICATION (PluginHostApp)
