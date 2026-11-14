#pragma once

#include <JuceHeader.h>
#include "Theme.h"
#include <functional>

/** A collapsible, user-movable smoked dark-glass HUD control console floating
    in the bottom middle of the monitoring / spectrum window.
    - Features 3 direct-access field buttons on the header pill bar ([ EQ ] [ DYN ] [ SPLIT ])
      so the user can immediately switch fields whether the pod is collapsed or expanded.
    - Closed state: Sleek, low-profile 28px hardware pill displaying drag grip dots,
      glowing LED accent, band badge, direct field buttons, live OLED parameter readout, and expand chevron.
    - Open state: Compact floating control card (620px x 178px) hosting the selected field.
    - Dragging the header moves the console anywhere across the monitoring area, clamped to bounds.
    - Clicking the header outside buttons or clicking the chevron toggles open/close. */
class FloatingInspector : public juce::Component
{
public:
    static constexpr int kHeaderHeight = 28;

    enum class Field
    {
        Eq = 0,
        Dyn,
        Split,
        Spec
    };

    FloatingInspector()
    {
        addAndMakeVisible (body);
        body.setVisible (open);

        for (auto* btn : { &eqButton, &dynButton, &splitButton, &specButton })
        {
            btn->setClickingTogglesState (false);
            btn->setColour (juce::TextButton::buttonColourId, juce::Colour (0x20ffffff));
            btn->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0x4035c6f0));
            btn->setColour (juce::TextButton::textColourOffId, juce::Colour (0xffb0b8c1));
            btn->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            addAndMakeVisible (btn);
        }

        eqButton.onClick = [this]
        {
            setActiveField (Field::Eq);
            if (! open)
                setOpen (true);
            if (onFieldSelected)
                onFieldSelected (Field::Eq);
        };

        dynButton.onClick = [this]
        {
            setActiveField (Field::Dyn);
            if (! open)
                setOpen (true);
            if (onFieldSelected)
                onFieldSelected (Field::Dyn);
        };

        splitButton.onClick = [this]
        {
            setActiveField (Field::Split);
            if (! open)
                setOpen (true);
            if (onFieldSelected)
                onFieldSelected (Field::Split);
        };

        specButton.onClick = [this]
        {
            setActiveField (Field::Spec);
            if (! open)
                setOpen (true);
            if (onFieldSelected)
                onFieldSelected (Field::Spec);
        };

        updateButtonColours();
        setSize (closedSize.x, closedSize.y);
    }

    juce::Component& getBody() noexcept { return body; }
    bool isOpen() const noexcept { return open; }

    void setAccent (juce::Colour c) { accent = c; repaint(); }
    void setTitle (const juce::String& t) { title = t; repaint (0, 0, getWidth(), kHeaderHeight); }
    void setSummary (const juce::String& s)
    {
        if (summary != s)
        {
            summary = s;
            repaint (0, 0, getWidth(), kHeaderHeight);
        }
    }

    void setActiveField (Field f)
    {
        if (activeField != f)
        {
            activeField = f;
            updateButtonColours();
            repaint (0, 0, getWidth(), kHeaderHeight);
        }
    }

    void setFieldIndicators (bool dynActive, bool splitActive)
    {
        if (dynIndicator != dynActive || splitIndicator != splitActive)
        {
            dynIndicator = dynActive;
            splitIndicator = splitActive;
            dynButton.setButtonText (dynIndicator ? juce::String::fromUTF8 ("DYN ●") : juce::String ("DYN"));
            splitButton.setButtonText (splitIndicator ? juce::String::fromUTF8 ("SPLIT ●") : juce::String ("SPLIT"));
            updateButtonColours();
        }
    }

    void setOpen (bool shouldOpen, bool notify = true)
    {
        if (open == shouldOpen)
            return;

        const auto oldBounds = getBounds();
        open = shouldOpen;
        body.setVisible (open);

        const auto targetSize = open ? openSize : closedSize;
        const int centerX = oldBounds.getCentreX();
        const int bottomY = oldBounds.getBottom();

        const int newX = centerX - targetSize.x / 2;
        const int newY = bottomY - targetSize.y;

        setBounds (newX, newY, targetSize.x, targetSize.y);
        if (open)
            toFront (false);

        clampToParent();
        resized();
        repaint();

        if (notify && onStateChanged)
            onStateChanged();
    }

    void resetToBottomCenter()
    {
        if (auto* p = getParentComponent())
        {
            const auto targetSize = open ? openSize : closedSize;
            const int cx = p->getWidth() / 2;
            const int by = p->getHeight() - 8;
            setBounds (cx - targetSize.x / 2, by - targetSize.y, targetSize.x, targetSize.y);
            clampToParent();
        }
    }

    void clampToParent()
    {
        auto* p = getParentComponent();
        if (p == nullptr || p->getWidth() <= 0 || p->getHeight() <= 0)
            return;

        constexpr int margin = 4;
        const int maxX = juce::jmax (margin, p->getWidth() - getWidth() - margin);
        const int maxY = juce::jmax (margin, p->getHeight() - getHeight() - margin);
        const int nx = juce::jlimit (margin, maxX, getX());
        const int ny = juce::jlimit (margin, maxY, getY());
        if (nx != getX() || ny != getY())
            setTopLeftPosition (nx, ny);
    }

    void setDimensions (juce::Point<int> closed, juce::Point<int> opened)
    {
        closedSize = closed;
        openSize = opened;
        const auto target = open ? openSize : closedSize;
        setSize (target.x, target.y);
    }

    void resized() override
    {
        // Header button bar layout
        constexpr int btnH = 20;
        constexpr int btnY = (kHeaderHeight - btnH) / 2;
        const int btnStartX = 84;

        eqButton.setBounds (btnStartX, btnY, 34, btnH);
        dynButton.setBounds (btnStartX + 38, btnY, dynIndicator ? 48 : 40, btnH);
        const int splitX = dynButton.getRight() + 4;
        splitButton.setBounds (splitX, btnY, splitIndicator ? 54 : 46, btnH);
        specButton.setBounds (splitButton.getRight() + 4, btnY, 48, btnH);

        body.setBounds (0, kHeaderHeight, getWidth(), juce::jmax (0, getHeight() - kHeaderHeight));
    }

    void paint (juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced (1.0f);

        // 1. Drop shadow so HUD module floats clearly above the spectrum analyzer & curves
        g.setColour (juce::Colours::black.withAlpha (open ? 0.70f : 0.45f));
        g.fillRoundedRectangle (b.translated (0.0f, 2.5f), 6.0f);

        // 2. Smoked dark-glass surface
        juce::Colour bgTop (open ? 0xf2161c20 : 0xea1a2126);
        juce::Colour bgBot (open ? 0xf60f1316 : 0xee12171a);
        if (hoverHeader && ! open)
        {
            bgTop = bgTop.brighter (0.08f);
            bgBot = bgBot.brighter (0.08f);
        }
        g.setGradientFill (juce::ColourGradient (bgTop, 0.0f, b.getY(), bgBot, 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, 5.0f);

        // 3. Header bar surface when open
        if (open)
        {
            g.setColour (juce::Colour (0x35ffffff));
            g.drawHorizontalLine (kHeaderHeight, b.getX() + 2.0f, b.getRight() - 2.0f);
            g.setColour (accent.withAlpha (0.35f));
            g.drawHorizontalLine (kHeaderHeight + 1, b.getX() + 2.0f, b.getRight() - 2.0f);
        }

        // 4. Outer metallic / neon border
        const bool lit = open || hoverHeader;
        g.setColour (lit ? accent.withAlpha (open ? 0.85f : 0.60f) : juce::Colour (0x35ffffff));
        g.drawRoundedRectangle (b, 5.0f, lit ? 1.25f : 1.0f);

        // 5. Left 2x3 machined grip dots (indicates movable / draggable)
        g.setColour (juce::Colour (0x80ffffff));
        for (int row = 0; row < 3; ++row)
        {
            for (int col = 0; col < 2; ++col)
            {
                const float dx = b.getX() + 7.0f + static_cast<float> (col) * 3.5f;
                const float dy = b.getY() + 8.5f + static_cast<float> (row) * 4.2f;
                g.fillEllipse (dx, dy, 1.8f, 1.8f);
            }
        }

        // 6. Glowing vertical LED status indicator
        g.setColour (accent.withAlpha (lit ? 0.35f : 0.20f));
        g.fillRoundedRectangle (b.getX() + 15.5f, b.getY() + 5.0f, 5.0f, static_cast<float> (kHeaderHeight) - 10.0f, 2.0f);
        g.setColour (lit ? accent.brighter (0.2f) : accent);
        g.fillRoundedRectangle (b.getX() + 16.8f, b.getY() + 6.2f, 2.5f, static_cast<float> (kHeaderHeight) - 12.4f, 1.2f);

        // 7. Band Title
        g.setColour (juce::Colour (0xfff0f6fc));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        const int titleX = static_cast<int> (b.getX()) + 23;
        g.drawFittedText (title, titleX, 0, 56, kHeaderHeight, juce::Justification::centredLeft, 1, 0.85f);

        // 8. Recessed mini-OLED summary badge (e.g. "1000 Hz  +0.0 dB  Q 1.00  Bell")
        const int sumX = specButton.getRight() + 8;
        const int sumW = juce::jmax (40, static_cast<int> (b.getRight()) - sumX - 28);
        if (summary.isNotEmpty() && sumW > 30)
        {
            const juce::Rectangle<float> sumBox (float (sumX), 4.5f, float (sumW), float (kHeaderHeight) - 9.0f);
            g.setColour (juce::Colour (0xff0a0e11));
            g.fillRoundedRectangle (sumBox, 3.0f);
            g.setColour (lit ? accent.withAlpha (0.40f) : juce::Colour (0x25ffffff));
            g.drawRoundedRectangle (sumBox, 3.0f, 0.8f);

            g.setColour (lit ? accent.brighter (0.3f) : juce::Colour (0xff9aa0a6));
            g.setFont (juce::FontOptions (8.8f, juce::Font::bold));
            g.drawFittedText (summary, sumBox.toNearestInt().reduced (4, 0), juce::Justification::centred, 1, 0.80f);
        }

        // 9. Expand / collapse chevron button on right
        const float chevX = b.getRight() - 13.0f;
        const float chevY = static_cast<float> (kHeaderHeight) * 0.5f;
        juce::Path chev;
        if (open)
        {
            // Point down to collapse
            chev.startNewSubPath (chevX - 4.0f, chevY - 2.0f);
            chev.lineTo (chevX, chevY + 2.5f);
            chev.lineTo (chevX + 4.0f, chevY - 2.0f);
        }
        else
        {
            // Point up to expand
            chev.startNewSubPath (chevX - 4.0f, chevY + 2.0f);
            chev.lineTo (chevX, chevY - 2.5f);
            chev.lineTo (chevX + 4.0f, chevY + 2.0f);
        }
        g.setColour (lit ? accent.brighter (0.3f) : juce::Colour (0xffcccccc));
        g.strokePath (chev, juce::PathStrokeType (1.75f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const bool inHdr = e.y <= kHeaderHeight || ! open;
        if (inHdr != hoverHeader)
        {
            hoverHeader = inHdr;
            setMouseCursor (inHdr ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint (0, 0, getWidth(), kHeaderHeight);
        }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverHeader)
        {
            hoverHeader = false;
            setMouseCursor (juce::MouseCursor::NormalCursor);
            repaint (0, 0, getWidth(), kHeaderHeight);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        headerGestureActive = (! open) || (e.y <= kHeaderHeight);
        wasDragged = false;
        if (! headerGestureActive)
            return;

        toFront (false);
        if (auto* p = getParentComponent())
            dragStartParent = e.getEventRelativeTo (p).getPosition();
        else
            dragStartParent = e.getScreenPosition();
        startTopLeft = getPosition();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! headerGestureActive)
            return;

        const auto curParent = (getParentComponent() != nullptr)
                                   ? e.getEventRelativeTo (getParentComponent()).getPosition()
                                   : e.getScreenPosition();
        const auto delta = curParent - dragStartParent;
        if (! wasDragged && (std::abs (delta.x) >= 4 || std::abs (delta.y) >= 4))
        {
            wasDragged = true;
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        }

        if (wasDragged)
        {
            setTopLeftPosition (startTopLeft + delta);
            clampToParent();
            if (onMoved)
                onMoved();
            if (onStateChanged)
                onStateChanged();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (! headerGestureActive)
            return;

        setMouseCursor (hoverHeader ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        const bool shouldToggle = ! wasDragged;
        headerGestureActive = false;
        wasDragged = false;

        if (shouldToggle)
            setOpen (! open, true);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y <= kHeaderHeight || ! open)
            setOpen (! open, true);
    }

    std::function<void (Field)> onFieldSelected;
    std::function<void()> onStateChanged;
    std::function<void()> onMoved;

private:
    void updateButtonColours()
    {
        const bool isEq = (activeField == Field::Eq);
        const bool isDyn = (activeField == Field::Dyn);
        const bool isSplit = (activeField == Field::Split);
        const bool isSpec = (activeField == Field::Spec);

        eqButton.setColour (juce::TextButton::buttonColourId, isEq ? juce::Colour (0x5035c6f0) : juce::Colour (0x18ffffff));
        eqButton.setColour (juce::TextButton::textColourOffId, isEq ? juce::Colours::white : juce::Colour (0xff9aa0a6));

        dynButton.setColour (juce::TextButton::buttonColourId, isDyn ? juce::Colour (0x5035c6f0) : juce::Colour (0x18ffffff));
        dynButton.setColour (juce::TextButton::textColourOffId, isDyn ? juce::Colours::white : (dynIndicator ? juce::Colour (0xff35c6f0) : juce::Colour (0xff9aa0a6)));

        splitButton.setColour (juce::TextButton::buttonColourId, isSplit ? juce::Colour (0x504ade80) : juce::Colour (0x18ffffff));
        splitButton.setColour (juce::TextButton::textColourOffId, isSplit ? juce::Colours::white : (splitIndicator ? juce::Colour (0xff4ade80) : juce::Colour (0xff9aa0a6)));

        specButton.setColour (juce::TextButton::buttonColourId, isSpec ? juce::Colour (0x50b58cff) : juce::Colour (0x18ffffff));
        specButton.setColour (juce::TextButton::textColourOffId, isSpec ? juce::Colours::white : juce::Colour (0xff9aa0a6));
    }

    juce::TextButton eqButton { "EQ" };
    juce::TextButton dynButton { "DYN" };
    juce::TextButton splitButton { "SPLIT" };
    juce::TextButton specButton { "RESO" };

    juce::String title { "BAND 1" };
    juce::String summary { "1000 Hz  +0.0 dB  Q 1.00  Bell" };
    juce::Colour accent { 0xff35c6f0 };
    juce::Point<int> closedSize { 480, 28 };
    juce::Point<int> openSize   { 840, 196 };
    juce::Component body;
    Field activeField = Field::Eq;
    bool dynIndicator = false;
    bool splitIndicator = false;
    bool open = true;
    bool hoverHeader = false;
    bool headerGestureActive = false;
    bool wasDragged = false;
    juce::Point<int> dragStartParent;
    juce::Point<int> startTopLeft;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FloatingInspector)
};
