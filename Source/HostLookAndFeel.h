#ifndef LIGHTHOST_HOST_LOOK_AND_FEEL_H
#define LIGHTHOST_HOST_LOOK_AND_FEEL_H

#include "HostDesign.h"

// One theme for host windows, menus, dialogs, and scrolling containers.
class LightHostLookAndFeel : public LookAndFeel_V3
{
public:
    LightHostLookAndFeel()
    {
#if JUCE_WINDOWS
        setDefaultSansSerifTypefaceName ("Segoe UI");
#endif
        setColour (Label::textColourId, HostDesign::text);
        setColour (ComboBox::textColourId, HostDesign::text);
        setColour (ListBox::outlineColourId, Colours::transparentBlack);
        setColour (ResizableWindow::backgroundColourId, Colour (0xffFAFBFC));
        setColour (DocumentWindow::backgroundColourId, Colour (0xffFAFBFC));
        setColour (TextEditor::backgroundColourId, Colours::white);
        setColour (TextEditor::textColourId, Colour (0xff202329));
        setColour (TextEditor::highlightColourId, Colour (0xffEAF3FC));
        setColour (TextEditor::highlightedTextColourId, Colour (0xff0067B8));
        setColour (TextEditor::outlineColourId, Colour (0xffD6DBE2));
        setColour (TextButton::buttonColourId, Colour (0xffFFFFFF));
        setColour (TextButton::buttonOnColourId, Colour (0xff0078D4));
        setColour (TextButton::textColourOnId, Colours::white);
        setColour (TextButton::textColourOffId, Colour (0xff202329));
        setColour (ComboBox::backgroundColourId, Colour (0xffFFFFFF));
        setColour (ComboBox::outlineColourId, Colour (0xffD6DBE2));
        setColour (ComboBox::arrowColourId, Colour (0xff202329));
        setColour (PopupMenu::backgroundColourId, Colours::transparentBlack);
        setColour (PopupMenu::textColourId, HostDesign::text);
        setColour (PopupMenu::headerTextColourId, HostDesign::secondary);
        setColour (PopupMenu::highlightedBackgroundColourId, HostDesign::selection);
        setColour (PopupMenu::highlightedTextColourId, HostDesign::text);
        setColour (ToggleButton::tickColourId, Colour (0xffFFFFFF));
        setColour (ToggleButton::textColourId, Colour (0xff202329));
        setColour (ListBox::backgroundColourId, Colour (0xffFAFBFC));
        setColour (ListBox::textColourId, Colour (0xff202329));
        setColour (ScrollBar::thumbColourId, Colour (0xffA4ACB7));
        setColour (ScrollBar::backgroundColourId, Colours::transparentBlack);
        setColour (AlertWindow::backgroundColourId, HostDesign::canvas);
        setColour (AlertWindow::textColourId, HostDesign::text);
        setColour (AlertWindow::outlineColourId, HostDesign::border);
        setColour (ProgressBar::backgroundColourId, HostDesign::sidebar);
        setColour (ProgressBar::foregroundColourId, HostDesign::accent);
        setColour (HyperlinkButton::textColourId, Colour (0xff0067B8));
    }

    Typeface::Ptr getTypefaceForFont (const Font& font) override
    {
        return LookAndFeel_V3::getTypefaceForFont (HostDesign::resolveUiFont (font));
    }

    Font getTextButtonFont (TextButton&, int) override { return HostDesign::bodyFont (16.0f); }
    Font getComboBoxFont (ComboBox&) override { return HostDesign::bodyFont (16.0f); }
    Font getPopupMenuFont() override { return HostDesign::bodyFont(15.0f); }
    Font getAlertWindowTitleFont() override { return HostDesign::bodyFont(21.0f); }
    Font getAlertWindowMessageFont() override { return HostDesign::resolveUiFont(Font(16.0f)); }
    Font getAlertWindowFont() override { return HostDesign::bodyFont(15.0f); }
    int getAlertWindowButtonHeight() override { return HostDesign::menuRowHeight; }

    void drawPopupMenuBackground (Graphics& g, int width, int height) override
    {
        HostDesign::drawPopupSurface(g, width, height);
    }

    int getMenuWindowFlags() override { return 0; }

    DropShadower* createDropShadowerForComponent (Component* component) override
    {
        // JUCE 4's separate rectangular shadow windows show through rounded corners.
        // Floating surfaces paint their shadow in the same transparent window instead.
        if (component != nullptr && !component->isOpaque())
            return nullptr;
        return LookAndFeel_V3::createDropShadowerForComponent(component);
    }

    void getIdealPopupMenuItemSize (const String& text, bool separator, int standardHeight,
                                   int& width, int& height) override
    {
        width = separator ? 80 : getPopupMenuFont().getStringWidth(text) + 64;
        height = separator ? 9 : jmax(HostDesign::menuRowHeight, standardHeight);
    }

    void drawPopupMenuItem (Graphics& g, const Rectangle<int>& row, bool separator,
                           bool enabled, bool highlighted, bool ticked, bool submenu,
                           const String& text, const String& shortcut, const Drawable* icon,
                           const Colour* textColour) override
    {
        // JUCE's item components span the full window width, including our shadow gutter.
        const auto area = row.reduced(HostDesign::popupShadowMargin, 0);
        if (separator)
        {
            g.setColour(HostDesign::border);
            g.fillRect(area.getX() + 12, area.getCentreY(), jmax(0, area.getWidth() - 24), 1);
            return;
        }
        Colour ink = textColour != nullptr ? *textColour : HostDesign::text;
        if (enabled && highlighted)
        {
            g.setColour(ink == HostDesign::danger ? HostDesign::danger.withAlpha(0.08f) : HostDesign::selection);
            g.fillRoundedRectangle(area.reduced(4, 2).toFloat(), HostDesign::controlRadius - 2);
        }
        if (!enabled) ink = HostDesign::secondary.withAlpha(0.55f);
        g.setColour(ink);
        const float centreY = (float) area.getCentreY();
        if (icon != nullptr)
            icon->drawWithin(g, Rectangle<float>((float) area.getX() + 11, centreY - 8, 16, 16),
                             RectanglePlacement::centred, enabled ? 1.0f : 0.45f);
        else if (ticked)
        {
            Path check;
            check.startNewSubPath((float) area.getX() + 12, centreY);
            check.lineTo((float) area.getX() + 16, centreY + 4);
            check.lineTo((float) area.getX() + 23, centreY - 4);
            g.strokePath(check, PathStrokeType(1.7f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        if (submenu)
        {
            Path arrow;
            const float x = (float) area.getRight() - 14;
            arrow.startNewSubPath(x - 3, centreY - 4);
            arrow.lineTo(x + 1, centreY);
            arrow.lineTo(x - 3, centreY + 4);
            g.strokePath(arrow, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        auto label = area.withTrimmedLeft(34).withTrimmedRight(submenu ? 30 : 14);
        if (shortcut.isNotEmpty())
        {
            const Font shortcutFont = HostDesign::resolveUiFont(Font(14.0f));
            const int shortcutWidth = shortcutFont.getStringWidth(shortcut) + 16;
            g.setFont(shortcutFont);
            g.drawText(shortcut, label.removeFromRight(shortcutWidth), Justification::centredRight);
        }
        g.setFont(getPopupMenuFont());
        g.drawText(text, label, Justification::centredLeft);
    }

    void drawPopupMenuSectionHeader (Graphics& g, const Rectangle<int>& row, const String& title) override
    {
        const auto area = row.reduced(HostDesign::popupShadowMargin, 0);
        auto label = area.reduced(14, 0);
        if (title == "Light Host")
        {
            HostDesign::drawSymbol(g, HostDesign::waveform,
                Rectangle<float>((float) label.getX(), (float) area.getCentreY() - 10, 20, 20), HostDesign::text);
            label.removeFromLeft(28);
        }
        g.setColour(HostDesign::text);
        g.setFont(HostDesign::bodyFont(15.0f));
        g.drawText(title, label, Justification::centredLeft);
        g.setColour(HostDesign::border);
        g.fillRect(area.getX() + 12, area.getBottom() - 1, jmax(0, area.getWidth() - 24), 1);
    }

    bool areScrollbarButtonsVisible() override { return false; }
    int getDefaultScrollbarWidth() override { return HostDesign::scrollbarWidth; }
    int getMinimumScrollbarThumbSize (ScrollBar&) override { return 28; }

    void drawScrollbar (Graphics& g, ScrollBar& scrollbar, int x, int y, int width, int height,
                        bool vertical, int thumbStart, int thumbSize, bool hovered, bool pressed) override
    {
        const auto track = Rectangle<float>((float) x, (float) y, (float) width, (float) height);
        if (hovered || pressed)
        {
            g.setColour(HostDesign::sidebar);
            g.fillRoundedRectangle(track, (vertical ? width : height) * 0.5f);
        }
        if (thumbSize <= 0) return;
        const auto thumb = vertical
            ? Rectangle<float>((float) x + 2, (float) thumbStart + 2, (float) width - 4, (float) jmax(1, thumbSize - 4))
            : Rectangle<float>((float) thumbStart + 2, (float) y + 2, (float) jmax(1, thumbSize - 4), (float) height - 4);
        Colour ink = scrollbar.findColour(ScrollBar::thumbColourId);
        if (hovered || pressed) ink = HostDesign::secondary.withAlpha(pressed ? 1.0f : 0.8f);
        g.setColour(ink.withMultipliedAlpha(scrollbar.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle(thumb, (vertical ? thumb.getWidth() : thumb.getHeight()) * 0.5f);
    }

    void fillTextEditorBackground (Graphics& g, int width, int height, TextEditor& editor) override
    {
        // TextEditor marks itself opaque when its fill is opaque; paint the corners too.
        g.fillAll(HostDesign::canvas);
        g.setColour(editor.findColour(TextEditor::backgroundColourId));
        g.fillRoundedRectangle(Rectangle<float>(0, 0, (float) width, (float) height), HostDesign::controlRadius);
    }

    void drawAlertBox (Graphics& g, AlertWindow& alert, const Rectangle<int>& textArea, TextLayout& layout) override
    {
        g.fillAll(HostDesign::canvas);
        g.setColour(HostDesign::border);
        g.drawRect(alert.getLocalBounds(), 1);
        int iconSpace = 0;
        if (alert.getAlertType() != AlertWindow::NoIcon)
        {
            iconSpace = 80; // Matches the space reserved by JUCE's alert layout.
            const Colour ink = alert.getAlertType() == AlertWindow::WarningIcon ? HostDesign::warning : HostDesign::accent;
            const auto icon = Rectangle<float>((float) textArea.getX() + 20, (float) textArea.getY() + 8, 36, 36);
            g.setColour(ink.withAlpha(0.1f));
            g.fillEllipse(icon);
            g.setColour(ink);
            g.setFont(HostDesign::bodyFont(24.0f));
            g.drawText(alert.getAlertType() == AlertWindow::WarningIcon ? "!"
                : alert.getAlertType() == AlertWindow::QuestionIcon ? "?" : "i", icon, Justification::centred);
        }
        layout.draw(g, textArea.withTrimmedLeft(iconSpace).toFloat());
    }

    void drawProgressBar (Graphics& g, ProgressBar& bar, int width, int height,
                          double progress, const String& label) override
    {
        const float trackY = label.isEmpty() ? height * 0.5f - 3 : (float) height - 6;
        const Rectangle<float> track(0, trackY, (float) width, 6);
        g.setColour(bar.findColour(ProgressBar::backgroundColourId));
        g.fillRoundedRectangle(track, 3);
        g.setColour(bar.findColour(ProgressBar::foregroundColourId));
        if (progress >= 0 && progress <= 1)
            g.fillRoundedRectangle(track.withWidth((float) (width * progress)), 3);
        else
        {
            const float phase = (float) (Time::getMillisecondCounter() % 1600) / 1600.0f;
            g.fillRoundedRectangle(track.withX((float) width * 0.7f * (0.5f - 0.5f * std::cos(phase * float_Pi * 2)))
                .withWidth((float) width * 0.3f), 3);
        }
        if (label.isNotEmpty())
        {
            g.setColour(HostDesign::secondary);
            g.setFont(HostDesign::resolveUiFont(Font(14.0f)));
            g.drawText(label, 0, 0, width, jmax(1, height - 8), Justification::centred);
        }
    }

    void drawButtonText (Graphics& g, TextButton& button, bool, bool) override
    {
        g.setColour (button.findColour (button.getToggleState()
            ? TextButton::textColourOnId : TextButton::textColourOffId)
            .withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.78f));
        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (7, 0),
                         Justification::centred, 1);
    }

    void drawTextEditorOutline (Graphics& g, int width, int height, TextEditor& editor) override
    {
        g.setColour (editor.hasKeyboardFocus (true) ? HostDesign::accent : HostDesign::border);
        g.drawRoundedRectangle (Rectangle<float> (0.5f, 0.5f, (float) width - 1, (float) height - 1), HostDesign::controlRadius, 1);
    }

    void drawTableHeaderBackground (Graphics& g, TableHeaderComponent& header) override
    {
        g.fillAll (HostDesign::sidebar);
        g.setColour (HostDesign::border);
        g.drawHorizontalLine (header.getHeight() - 1, 0.0f, (float) header.getWidth());
    }

    void drawButtonBackground (Graphics& g, Button& button, const Colour& background,
                               bool isMouseOverButton, bool isButtonDown) override
    {
        Colour fill = button.getToggleState() ? HostDesign::accent : background;
        if (isMouseOverButton)
            fill = fill.overlaidWith (Colour (0xff0078D4).withAlpha (0.06f));
        if (isButtonDown)
            fill = fill.overlaidWith (Colour (0xff0078D4).withAlpha (0.12f));

        const Rectangle<float> bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (button.getToggleState() ? Colour (0xffB6D6F2) : Colour (0xffD6DBE2));
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        if (button.hasKeyboardFocus (false))
        {
            g.setColour (Colour (0xff0078D4));
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
            ? Colour (0xff0078D4) : box.findColour (ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 8.0f, 1.0f);

        const float centreX = (float) buttonX + (float) buttonW * 0.5f;
        const float centreY = (float) buttonY + (float) buttonH * 0.5f;
        Path arrow;
        arrow.startNewSubPath (centreX - 4, centreY - 2);
        arrow.lineTo (centreX, centreY + 2);
        arrow.lineTo (centreX + 4, centreY - 2);
        g.setColour (box.findColour (ComboBox::arrowColourId));
        g.strokePath (arrow, PathStrokeType(1.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    void drawTickBox (Graphics& g, Component&,
                      float x, float y, float width, float height,
                      bool ticked, bool isEnabled, bool isMouseOverButton,
                      bool isButtonDown) override
    {
        const float side = jmin (14.0f, jmin (width, height));
        const Rectangle<float> box (x, y + (height - side) * 0.5f, side, side);
        const Colour accent (0xff0078D4);

        g.setColour (ticked ? accent : Colours::white);
        g.fillRoundedRectangle (box.reduced (0.5f), 3.0f);
        g.setColour (ticked || isMouseOverButton || isButtonDown
            ? accent : Colour (0xff969DA8));
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

#endif
