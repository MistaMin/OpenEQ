#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"
#include <vector>
#include <GoodLookinUI.h>

// Console faceplate with fixed knob slots and aligned choice keys.
class SectionPanel : public juce::Component {
public:
    explicit SectionPanel(const juce::String& titleText) : title(titleText) {}

    void addItem(juce::Component& c, int preferredWidth)
    {
        items.push_back({&c, preferredWidth});
        addAndMakeVisible(c);
    }

    // Narrow vertical channel-strip layout: items are stacked top-to-bottom
    // (each item's "width" value is reused as its height in this mode),
    // matching a hardware EQ module rather than a wide horizontal row.
    void setVerticalLayout(bool v, int width = 132)
    {
        vLayout = v;
        vWidth = width;
    }

    // Reserve a tiny square slot (top-right) for a per-band bypass button.
    void setBypassButton(juce::Component& bypass)
    {
        bypassButton = &bypass;
        addAndMakeVisible(bypass);
    }

    int getPreferredHeight() const
    {
        if (!vLayout)
            return 116;
        int h = 40;
        for (auto& item : items)
            h += item.width + 5;
        return h;
    }

    void setAccent (juce::Colour colour, const juce::String& range) { accent=colour; subtitle=range; }
    void setAccentColour (juce::Colour colour) { accent=colour; repaint(); }
    void setTitleText (const juce::String& t) { title = t; repaint(); }
    juce::Colour getAccentColour() const { return accent; }
    void setCompactHeader (bool c) { compactHeader = c; repaint(); }
    bool isCompactHeader() const noexcept { return compactHeader; }

    // Per-section plate: top, bottom, text, secondary text. Overrides the global theme.
    void setPlate(juce::Colour top, juce::Colour bottom, juce::Colour text, juce::Colour mid, int finish=0)
    { plate[0]=top; plate[1]=bottom; plate[2]=text; plate[3]=mid; plateFinish=finish; hasPlate=true; repaint(); }
    void clearPlate() { if(hasPlate) { hasPlate=false; repaint(); } }
    // Colour behind the controls (mean of the plate gradient), used by knobs to pick light or dark printed marks.
    juce::Colour getBackdrop() const
    {
        return hasPlate ? plate[0].interpolatedWith(plate[1], 0.5f) : Theme::panelTop.interpolatedWith(Theme::panelBottom, 0.5f);
    }
    bool getPlate(juce::Colour out[4], int* finish=nullptr) const
    {
        if(!hasPlate) return false;
        if(finish) *finish=plateFinish;
        for(int i=0;i<4;++i) out[i]=plate[i];
        return true;
    }
    void paint(juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat().reduced (1.0f);

        // Drop shadow.
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillRoundedRectangle (b.translated (0.0f, 1.0f), 5.0f);

        // Panel body: follows the active theme unless the owner supplies a plate.
        juce::Colour cTop = hasPlate ? plate[0] : Theme::panelTop;
        juce::Colour cBot = hasPlate ? plate[1] : Theme::panelBottom;
        g.setGradientFill (juce::ColourGradient (cTop, 0.0f, b.getY(),
                                                 cBot, 0.0f, b.getBottom(), false));
        g.fillRoundedRectangle (b, 5.0f);

        // Soft inner top highlight.
        g.setColour (juce::Colours::white.withAlpha (hasPlate ? 0.08f : 0.06f));
        g.drawHorizontalLine (juce::roundToInt (b.getY()) + 1, b.getX() + 4, b.getRight() - 4);

        // Outer edge.
        g.setColour (Theme::panelEdge);
        g.drawRoundedRectangle (b, 5.0f, 1.0f);

        // LED accent bar with soft glow.
        auto led = accent;
        const float barX = 10.0f;
        const float barY = compactHeader ? 5.0f : 10.0f;
        const float barW = 3.0f;
        const float barH = compactHeader ? 14.0f : 24.0f;
        for (int i = 3; i >= 0; --i)
        {
            g.setColour (led.withAlpha (0.16f - (float) i * 0.035f));
            g.fillRoundedRectangle (barX - (float) i, barY - (float) i * 0.5f,
                                    barW + (float) i * 2.0f, barH + (float) i, 1.5f);
        }
        g.setColour (led.darker (0.15f));
        g.fillRoundedRectangle (barX, barY, barW, barH, 1.5f);
        g.setColour (led.brighter (0.25f));
        g.fillRoundedRectangle (barX, barY, barW, barH * 0.5f, 1.5f);

        // Titles.
        const bool hasSub = subtitle.isNotEmpty();
        const float headerH = (compactHeader || ! hasSub) ? 24.0f : 44.0f;
        g.setColour (hasPlate ? plate[2] : Theme::textBright);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        if (compactHeader && hasSub)
        {
            const float titleW = juce::jmin ((float) getWidth() * 0.52f, g.getCurrentFont().getStringWidthFloat (title) + 2.0f);
            g.drawText (title, 20, 4, juce::roundToInt (titleW), 16, juce::Justification::centredLeft);
            // Drawn LED indicator dot for subtitle
            const float dotX = 20.0f + titleW + 5.0f;
            g.setColour (accent.withAlpha (0.4f));
            g.fillEllipse (dotX, 9.5f, 5.0f, 5.0f);
            g.setColour (accent.brighter (0.3f));
            g.fillEllipse (dotX + 1.0f, 10.5f, 3.0f, 3.0f);

            g.setColour (hasPlate ? plate[3] : accent.brighter (0.25f));
            g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
            const float subX = dotX + 9.0f;
            g.drawText (subtitle, (int) subX, 4, getWidth() - (int) subX - 8, 16, juce::Justification::centredLeft);
        }
        else
        {
            g.drawText (title, 20, hasSub ? 5 : 4, getWidth() - 70, 16, juce::Justification::centredLeft);
            if (hasSub)
            {
                g.setColour (hasPlate ? plate[3] : Theme::textStd);
                g.setFont (juce::FontOptions (8.5f));
                g.drawText (subtitle, 20, 20, getWidth() - 44, 14, juce::Justification::centredLeft);
            }
        }

        // Hairline under the header.
        g.setColour (juce::Colours::black.withAlpha (0.40f));
        g.drawHorizontalLine (juce::roundToInt (headerH - 1.0f), 10.0f, float(getWidth() - 10));
        g.setColour (juce::Colour (0x10ffffff));
        g.drawHorizontalLine (juce::roundToInt (headerH), 10.0f, float(getWidth() - 10));
    }

    void resized() override
    {
        if(bypassButton) bypassButton->setBounds(getWidth()-36,10,24,20);
        if(vLayout) {
            int knob=0,button=0;
            for(auto& item:items) {
                if(item.width>=70) item.component->setBounds(12,52+knob++*100,getWidth()-24,98);
                else item.component->setBounds(22,354+button++*36,getWidth()-44,32);
            }
        } else {
            if (items.empty()) return;
            auto area = getLocalBounds().reduced (10, 0);
            const int topH = (compactHeader || subtitle.isEmpty()) ? 26 : 50;
            area.removeFromTop (topH);
            area.removeFromBottom (6);
            const int gap = 6;
            int preferred = 0;
            for (const auto& item : items) preferred += item.width;
            const int available = juce::jmax (0, area.getWidth() - gap * ((int) items.size() - 1));
            int used = 0;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const int next = juce::roundToInt ((float) available * (float) (used + items[i].width) / (float) preferred);
                const int previous = juce::roundToInt ((float) available * (float) used / (float) preferred);
                items[i].component->setBounds (area.removeFromLeft (next - previous));
                area.removeFromLeft (gap);
                used += items[i].width;
            }
        }
    }

    int getPreferredWidth() const
    {
        if (vLayout)
            return vWidth + 16;
        int w = 20;
        for (auto& item : items)
            w += item.width + 6;
        return w - (items.empty() ? 0 : 6);
    }

private:
    // Stack every item top-to-bottom, full width, using each item's stored
    // "width" value as its height (narrow hardware channel-strip look).
    void layoutVertical(juce::Rectangle<int> b)
    {
        int y = b.getY();
        for (auto& item : items) {
            item.component->setBounds(b.getX(), y, b.getWidth(), item.width);
            y += item.width + 5;
        }
    }

    struct Item { juce::Component* component; int width; };

    juce::String title, subtitle;
    juce::Colour accent=Theme::faceAccent;
    juce::Colour plate[4];
    bool hasPlate=false;
    int plateFinish=0;
    juce::Component* bypassButton = nullptr;
    bool vLayout = false;
    int vWidth = 372;
    bool compactHeader = true;
    std::vector<Item> items;
};
