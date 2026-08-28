#include <JuceHeader.h>
#include "CustomLookAndFeel.h"

//==============================================================================
// Palette — mirrors EditorPalette in PluginEditor.h
//
// All colours used for text and borders are pre-mixed solid values.
// No withAlpha() on text colours — see PluginEditor.h for the rationale.
//==============================================================================
namespace Palette
{
    static const juce::Colour dark{ 0xff110b11 };
    static const juce::Colour cream{ 0xfff9dec9 };
    static const juce::Colour blue{ 0xff99b2dd };
    static const juce::Colour pink{ 0xffde369d };

    // Pre-mixed solid text colours
    static const juce::Colour textPrimary{ 0xfff9dec9 };
    static const juce::Colour textSecondary{ 0xffb8c0d4 };
    static const juce::Colour textTertiary{ 0xff6e7a96 };

    // Solid surface/border colours
    static const juce::Colour surfaceRaise{ 0xff170e17 };
    static const juce::Colour borderFaint{ 0xff221a22 };
    static const juce::Colour borderMid{ 0xff2e2535 };
}

//==============================================================================
CustomLookAndFeel::CustomLookAndFeel()
{
    // ── PopupMenu ─────────────────────────────────────────────────────────
    setColour(juce::PopupMenu::backgroundColourId, Palette::surfaceRaise);
    setColour(juce::PopupMenu::textColourId, Palette::textPrimary);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, juce::Colour(0xff2a1228));
    setColour(juce::PopupMenu::highlightedTextColourId, Palette::textPrimary);

    // ── ComboBox ──────────────────────────────────────────────────────────
    setColour(juce::ComboBox::backgroundColourId, Palette::dark.withAlpha(0.0f));
    setColour(juce::ComboBox::textColourId, Palette::textPrimary);
    setColour(juce::ComboBox::outlineColourId, Palette::cream.withAlpha(0.0f));
    setColour(juce::ComboBox::arrowColourId, Palette::textSecondary);
    setColour(juce::ComboBox::focusedOutlineColourId, Palette::blue.withAlpha(0.40f));

    // ── TextButton ────────────────────────────────────────────────────────
    setColour(juce::TextButton::buttonColourId, Palette::borderMid);
    setColour(juce::TextButton::textColourOffId, Palette::textSecondary);
    setColour(juce::TextButton::textColourOnId, Palette::textPrimary);

    // ── ScrollBar ─────────────────────────────────────────────────────────
    setColour(juce::ScrollBar::thumbColourId, Palette::borderMid);
}

CustomLookAndFeel::~CustomLookAndFeel() {}

//==============================================================================
// drawComboBox
// Apple-style: solid surfaceRaise fill + 1px borderMid stroke.
// No alpha fills, no gradients — just clean geometric form.
//==============================================================================
void CustomLookAndFeel::drawComboBox(juce::Graphics& g,
    int width, int height,
    bool /*isButtonDown*/,
    int /*buttonX*/, int /*buttonY*/,
    int /*buttonW*/, int /*buttonH*/,
    juce::ComboBox& box)
{
    const auto  bounds = juce::Rectangle<float>(0.f, 0.f, (float)width, (float)height);
    const float r = 6.0f;

    // Fill — solid lifted surface
    g.setColour(Palette::surfaceRaise);
    g.fillRoundedRectangle(bounds, r);

    // Border — 1px solid, slightly brighter when focused
    const bool focused = box.hasKeyboardFocus(false);
    g.setColour(focused ? Palette::blue.withAlpha(0.50f) : Palette::borderMid);
    g.drawRoundedRectangle(bounds.reduced(0.5f), r, 1.0f);

    // Chevron — textSecondary solid, clean
    const float ax = (float)width - 20.0f;
    const float ay = (float)height * 0.5f - 3.0f;
    juce::Path arrow;
    arrow.startNewSubPath(ax, ay);
    arrow.lineTo(ax + 5.5f, ay + 5.0f);
    arrow.lineTo(ax + 11.0f, ay);
    g.setColour(Palette::textSecondary);
    g.strokePath(arrow, juce::PathStrokeType(1.2f,
        juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));
}

//==============================================================================
// drawPopupMenuBackground
// Solid fill + 1px border. No multi-pass shadow simulation.
//==============================================================================
void CustomLookAndFeel::drawPopupMenuBackground(juce::Graphics& g,
    int width, int height)
{
    const auto bounds = juce::Rectangle<float>(0.f, 0.f, (float)width, (float)height);

    g.setColour(Palette::surfaceRaise);
    g.fillRoundedRectangle(bounds, 8.0f);

    g.setColour(Palette::borderMid);
    g.drawRoundedRectangle(bounds.reduced(0.5f), 8.0f, 1.0f);
}

//==============================================================================
// drawPopupMenuItem
// Clean rows. Text in solid pre-mixed colours. Highlight is a solid fill.
//==============================================================================
void CustomLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
    const juce::Rectangle<int>& area,
    bool isSeparator,
    bool /*isActive*/,
    bool isHighlighted,
    bool isTicked,
    bool /*hasSubMenu*/,
    const juce::String& text,
    const juce::String& /*shortcutKeyText*/,
    const juce::Drawable* /*icon*/,
    const juce::Colour* /*textColour*/)
{
    if (isSeparator)
    {
        g.setColour(Palette::borderFaint);
        g.fillRect(area.getX() + 10, area.getCentreY(), area.getWidth() - 20, 1);
        return;
    }

    if (isHighlighted)
    {
        const auto hl = area.toFloat().reduced(3.f, 2.f);
        // Solid pink-tinted fill — pre-mixed, not alpha on dark
        g.setColour(juce::Colour(0xff2a1228));
        g.fillRoundedRectangle(hl, 5.f);
        g.setColour(Palette::pink.withAlpha(0.45f));
        g.drawRoundedRectangle(hl.reduced(0.5f), 5.f, 1.0f);
    }

    // Tick mark
    if (isTicked)
    {
        g.setColour(Palette::pink);
        const float cx = (float)area.getX() + 12.f;
        const float cy = (float)area.getCentreY();
        g.fillEllipse(cx - 2.5f, cy - 2.5f, 5.f, 5.f);
    }

    // Label — sans for readability, solid textPrimary
    g.setFont(juce::Font(juce::FontOptions()
        .withName(juce::Font::getDefaultSansSerifFontName())
        .withHeight(12.0f)
        .withStyle("Regular")));

    g.setColour(isHighlighted ? Palette::textPrimary : Palette::textSecondary);
    g.drawText(text,
        area.withLeft(isTicked ? area.getX() + 24 : area.getX() + 12)
        .withRight(area.getRight() - 8),
        juce::Justification::centredLeft,
        false);
}

//==============================================================================
// drawRotarySlider
//
// Final minimal version:
//   • Track ring  — solid borderMid-weight, no alpha blending
//   • Value arc   — 2.5px, full accent colour (pink or blue)
//   • Center disc — solid discFill + solid borderMid rim
//   • Tick        — full textPrimary, 1.5px
//   • No halo ring, no gradients, no outer decorative elements
//   • Bipolar: arc from midpoint (inputGain, outputGain, ceiling)
//   • Pink identity: drive, ceiling
//   • Blue identity: inputGain, mix, outputGain
//==============================================================================
void CustomLookAndFeel::drawRotarySlider(juce::Graphics& g,
    int x, int y, int width, int height,
    float sliderPosProportional,
    float rotaryStartAngle,
    float rotaryEndAngle,
    juce::Slider& slider)
{
    const float cx = (float)x + (float)width * 0.5f;
    const float cy = (float)y + (float)height * 0.5f;
    const float size = (float)juce::jmin(width, height);
    const float r = size * 0.5f - 5.0f;
    const float angle = rotaryStartAngle
        + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const juce::String name = slider.getName();
    const bool  isPink = (name == "drive" || name == "ceiling");
    const juce::Colour arcCol = isPink ? Palette::pink : Palette::blue;

    const bool  bipolar = (name == "inputGain" || name == "outputGain" || name == "ceiling");
    const float midAngle = (rotaryStartAngle + rotaryEndAngle) * 0.5f;
    const float arcFrom = bipolar ? midAngle : rotaryStartAngle;
    const bool hovered = slider.isMouseOverOrDragging();
    const bool pressed = slider.isMouseButtonDown();
    const float lift = pressed ? 0.0f : (hovered ? 1.0f : 0.35f);

    // ── 1. Track ring — solid colour, clearly visible on Windows ─────────
    {
        juce::Path track;
        track.addCentredArc(cx, cy, r, r, 0.f, rotaryStartAngle, rotaryEndAngle, true);
        // Use a solid pre-mixed colour rather than alpha-dimmed cream
        g.setColour(Palette::borderMid.withAlpha(0.92f));
        g.strokePath(track, juce::PathStrokeType(1.5f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    // ── 2. Value arc — 2.5px, full accent colour ──────────────────────────
    if (std::abs(angle - arcFrom) > 0.01f)
    {
        juce::Path arc;
        arc.addCentredArc(cx, cy, r, r, 0.f, arcFrom, angle, true);
        g.setColour(arcCol.brighter(pressed ? 0.0f : 0.12f));
        g.strokePath(arc, juce::PathStrokeType(2.5f,
            juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }

    // ── 3. Center disc ────────────────────────────────────────────────────
    const float kr = r * 0.52f;

    g.setColour(juce::Colour(0xff1c1220));
    g.fillEllipse(cx - kr, cy - kr, kr * 2.f, kr * 2.f);

    // Disc rim — solid borderMid, no alpha
    g.setColour(Palette::borderMid);
    g.drawEllipse(cx - kr, cy - kr, kr * 2.f, kr * 2.f, 1.0f);

    // ── 4. Tick — full textPrimary, clean and crisp ───────────────────────
    const float tickOuter = kr - 2.0f;
    const float tickInner = kr * 0.36f;
    const float sinA = std::sin(angle);
    const float cosA = std::cos(angle);

    g.setColour(Palette::textPrimary);
    g.drawLine(cx + sinA * tickInner,
        cy - cosA * tickInner,
        cx + sinA * tickOuter,
        cy - cosA * tickOuter,
        1.5f);
}

//==============================================================================
// drawLabel — transparent bg, respects the label's own text colour
//==============================================================================
void CustomLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    g.fillAll(juce::Colours::transparentBlack);
    g.setColour(label.findColour(juce::Label::textColourId));
    g.setFont(label.getFont());
    g.drawText(label.getText(),
        label.getLocalBounds(),
        label.getJustificationType(),
        false);
}
