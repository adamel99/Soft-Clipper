#pragma once

#include <JuceHeader.h>
#include "EmbeddedFontData.h"

namespace ViveFonts
{
    inline juce::Typeface::Ptr regularTypeface()
    {
        static const auto face = juce::Typeface::createSystemTypefaceFor(
            EmbeddedFontData::NunitoRegular_ttf,
            static_cast<size_t> (EmbeddedFontData::NunitoRegular_ttfSize));
        return face;
    }

    inline juce::Typeface::Ptr semiBoldTypeface()
    {
        static const auto face = juce::Typeface::createSystemTypefaceFor(
            EmbeddedFontData::NunitoSemiBold_ttf,
            static_cast<size_t> (EmbeddedFontData::NunitoSemiBold_ttfSize));
        return face;
    }

    inline juce::Typeface::Ptr boldTypeface()
    {
        static const auto face = juce::Typeface::createSystemTypefaceFor(
            EmbeddedFontData::NunitoBold_ttf,
            static_cast<size_t> (EmbeddedFontData::NunitoBold_ttfSize));
        return face;
    }

    inline juce::Font makeFont (juce::Typeface::Ptr typeface, float height)
    {
        if (typeface == nullptr)
            return juce::Font (juce::FontOptions (height));

        return juce::Font (juce::FontOptions (typeface).withHeight (height));
    }

    inline juce::Font regular (float height)
    {
        return makeFont (regularTypeface(), height);
    }

    inline juce::Font semiBold (float height)
    {
        return makeFont (semiBoldTypeface(), height);
    }

    inline juce::Font bold (float height)
    {
        return makeFont (boldTypeface(), height);
    }
}
