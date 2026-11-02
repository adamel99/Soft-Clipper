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

juce::Font CustomLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::Font(juce::FontOptions()
        .withName("Arial Rounded MT Bold")
        .withHeight(juce::jlimit(10.0f, 13.0f, buttonHeight * 0.42f)));
}

juce::Font CustomLookAndFeel::getPopupMenuFont()
{
    return juce::Font(juce::FontOptions()
        .withName("Arial Rounded MT Bold")
        .withHeight(13.0f));
}

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

    // Rounded labels keep popup typography consistent with the pedal face.
    g.setFont(juce::Font(juce::FontOptions()
        .withName("Arial Rounded MT Bold")
        .withHeight(12.0f)));

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
// Silver stompbox knobs with a ribbed dark sidewall and pedal-specific pointer.
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
    const float angle = rotaryStartAngle
        + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    const juce::String name = slider.getName();
    const bool isInput = name == "inputGain";
    const bool isOutput = name == "outputGain";
    const juce::Colour pointerColour = isInput ? juce::Colour(0xff503487)
                                         : isOutput ? juce::Colour(0xff344c2d)
                                                    : juce::Colour(0xff572b64);
    const bool hovered = slider.isMouseOverOrDragging();
    const bool pressed = slider.isMouseButtonDown();
    const float faceR = size * 0.39f;
    const float sideR = faceR + 3.6f;

    // Drop shadow, black knurled skirt, then a thin chrome lip.
    g.setColour(juce::Colour(0x660d0710));
    g.fillEllipse(cx - sideR - 1.0f, cy - sideR + 3.5f,
                  (sideR + 1.0f) * 2.0f, (sideR + 1.0f) * 2.0f);
    juce::ColourGradient skirt(juce::Colour(0xff65616a), cx, cy - sideR,
                               juce::Colour(0xff17151b), cx, cy + sideR, false);
    g.setGradientFill(skirt);
    g.fillEllipse(cx - sideR, cy - sideR, sideR * 2.0f, sideR * 2.0f);

    for (int i = 0; i < 48; ++i)
    {
        const float a = juce::MathConstants<float>::twoPi * (float)i / 48.0f;
        const float outer = sideR - 0.5f;
        const float inner = faceR + 0.8f;
        g.setColour(i % 2 == 0 ? juce::Colour(0xffaaa6af) : juce::Colour(0xff29262e));
        g.drawLine(cx + std::sin(a) * inner, cy - std::cos(a) * inner,
                   cx + std::sin(a) * outer, cy - std::cos(a) * outer, 1.0f);
    }

    g.setColour(juce::Colour(0xffd9d6dc));
    g.drawEllipse(cx - faceR - 0.6f, cy - faceR - 0.6f,
                  (faceR + 0.6f) * 2.0f, (faceR + 0.6f) * 2.0f, 1.0f);

    juce::ColourGradient face(juce::Colour(0xfffaf9fb), cx - faceR * 0.7f, cy - faceR * 0.8f,
                              juce::Colour(0xff89868f), cx + faceR * 0.6f, cy + faceR * 0.9f, false);
    face.addColour(0.42, juce::Colour(0xffe0dee3));
    g.setGradientFill(face);
    g.fillEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f);
    g.setColour(juce::Colour(0xff77727b));
    g.drawEllipse(cx - faceR, cy - faceR, faceR * 2.0f, faceR * 2.0f, 0.8f);

    // A narrow colored pointer like the painted index line on the reference knobs.
    const float tickOuter = faceR * 0.78f;
    const float tickInner = faceR * 0.10f;
    const float sinA = std::sin(angle);
    const float cosA = std::cos(angle);
    const float pointerX1 = cx + sinA * tickInner;
    const float pointerY1 = cy - cosA * tickInner;
    const float pointerX2 = cx + sinA * tickOuter;
    const float pointerY2 = cy - cosA * tickOuter;
    g.setColour(juce::Colour(0x550c0810));
    g.drawLine(pointerX1 + 0.7f, pointerY1 + 1.0f,
               pointerX2 + 0.7f, pointerY2 + 1.0f, 4.0f);
    g.setColour(pointerColour.brighter(hovered && ! pressed ? 0.18f : 0.0f));
    g.drawLine(pointerX1, pointerY1, pointerX2, pointerY2, 2.7f);
    g.setColour(juce::Colour(0xff625c66));
    g.fillEllipse(cx - 2.4f, cy - 2.4f, 4.8f, 4.8f);
    g.setColour(juce::Colour(0xfff5f3f5));
    g.fillEllipse(cx - 1.1f, cy - 1.1f, 2.2f, 2.2f);
}

//==============================================================================
// drawLabel — compact, rounded plates keep labels legible over decorative shells.
//==============================================================================
void CustomLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    const auto bounds = label.getLocalBounds().toFloat();
    const auto background = label.findColour(juce::Label::backgroundColourId);
    const auto outline = label.findColour(juce::Label::outlineColourId);
    if (background.getAlpha() != 0)
    {
        g.setColour(background);
        g.fillRoundedRectangle(bounds, bounds.getHeight() * 0.32f);
        if (outline.getAlpha() != 0)
        {
            g.setColour(outline);
            g.drawRoundedRectangle(bounds.reduced(0.5f), bounds.getHeight() * 0.32f, 0.8f);
        }
    }
    g.setColour(label.findColour(juce::Label::textColourId));
    g.setFont(label.getFont());
    g.drawText(label.getText(),
        label.getLocalBounds(),
        label.getJustificationType(),
        false);
}
