#include "SkinAssets.h"
#include <SkratchSkinData.h>

namespace SkratchSkin
{
namespace
{
juce::Image load(const void *data, int size)
{
    auto result = juce::ImageFileFormat::loadFrom(data, static_cast<size_t>(size));
    jassert(result.isValid());
    return result;
}

juce::Typeface::Ptr typeface(const void *data, int size)
{
    auto result = juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
    jassert(result != nullptr);
    return result;
}

void healRail(juce::Image &deck, const juce::Image &source, juce::Rectangle<int> region, bool vertical)
{
    for (int y = 0; y < region.getHeight(); ++y)
        for (int x = 0; x < region.getWidth(); ++x)
        {
            const int distance = std::min({x, y, region.getWidth() - 1 - x, region.getHeight() - 1 - y});
            const float blend = std::min(1.0f, distance * .25f);
            const auto to = region.getPosition() + juce::Point<int>(x, y);
            const auto first =
                vertical ? juce::Point<int>(to.x, region.getY()) : juce::Point<int>(region.getX(), to.y);
            const auto last = vertical ? juce::Point<int>(to.x, region.getBottom() - 1)
                                       : juce::Point<int>(region.getRight() - 1, to.y);
            const float position = vertical ? y / static_cast<float>(region.getHeight() - 1)
                                            : x / static_cast<float>(region.getWidth() - 1);
            const auto healed = source.getPixelAt(first.x, first.y)
                                    .interpolatedWith(source.getPixelAt(last.x, last.y), position);
            deck.setPixelAt(to.x, to.y, source.getPixelAt(to.x, to.y).interpolatedWith(healed, blend));
        }
}

juce::Image cutCap(const juce::Image &source, juce::Rectangle<int> region)
{
    juce::Image result(juce::Image::ARGB, region.getWidth(), region.getHeight(), true);
    juce::Graphics g(result);
    juce::Path contour;
    contour.addRoundedRectangle(result.getBounds().toFloat().reduced(.5f), 4.0f);
    g.reduceClipRegion(contour);
    g.drawImageAt(source.getClippedImage(region), 0, 0);
    return result;
}

void cleanAperture(juce::Image &chassis)
{
    for (int y = 310; y < 1158; ++y)
        for (int x = 190; x < 1038; ++x)
        {
            const float r2 = (x - deckAxis.x) * (x - deckAxis.x) + (y - deckAxis.y) * (y - deckAxis.y);
            if (r2 < 414 * 414 || r2 > 424 * 424)
                continue;
            const auto c = chassis.getPixelAt(x, y);
            if (c.getAlpha() != 0 && c.getAlpha() < 247)
                chassis.setPixelAt(x, y, c.withSaturation(0));
        }
}
} // namespace

const Art &art()
{
    using namespace SkratchSkinData;
    static const Art assets = []
    {
        const auto atlas = load(mixer_png, mixer_pngSize);
        const auto originalDeck = load(deck_png, deck_pngSize);
        auto chassis = load(deck_headshell_underlay_png, deck_headshell_underlay_pngSize);
        {
            juce::Graphics layers(chassis);
            layers.drawImageAt(load(deck_xp_png, deck_xp_pngSize), 0, 0);
        }
        // Continue the rail's cross-section through the cap while preserving its longitudinal lighting.
        healRail(chassis, originalDeck, {839, 92, 92, 73}, false);
        healRail(chassis, originalDeck, {1091, 916, 70, 96}, true);
        cleanAperture(chassis);
        return Art{chassis,
                   load(mixer_xp_png, mixer_xp_pngSize),
                   load(platter_matte_png, platter_matte_pngSize),
                   load(vinyl_matte_png, vinyl_matte_pngSize),
                   load(logo_png, logo_pngSize).getClippedImage({233, 485, 1464, 378}),
                   load(knob_cap_png, knob_cap_pngSize),
                   atlas.getClippedImage({879, 539, 131, 79}),
                   atlas.getClippedImage({1018, 658, 128, 89}),
                   cutCap(originalDeck, {1101, 934, 53, 60}),
                   cutCap(originalDeck, {852, 98, 63, 56})};
    }();
    return assets;
}

juce::Font font(FontRole role, float size)
{
    using namespace SkratchSkinData;
    static const std::array<juce::Typeface::Ptr, 6> faces{
        typeface(blackopsone_ttf, blackopsone_ttfSize), typeface(teko_ttf, teko_ttfSize),
        typeface(rajdhani_ttf, rajdhani_ttfSize), typeface(ibmplexmono_ttf, ibmplexmono_ttfSize),
        typeface(dseg_ttf, dseg_ttfSize),
        typeface(teko_hardware_medium_ttf, teko_hardware_medium_ttfSize)};
    return juce::Font(juce::FontOptions(faces[static_cast<size_t>(role)]).withHeight(size));
}

void text(juce::Graphics &g, FontRole role, const juce::String &value, juce::Rectangle<int> bounds,
          float size, juce::Colour colour, int justification)
{
    g.setColour(colour);
    g.setFont(font(role, size));
    g.drawFittedText(value, bounds, justification, 1);
}

juce::Point<float> deckPoint(float x, float y)
{
    return deckOrigin + juce::Point<float>(x, y) * deckScale;
}
juce::Point<float> mixerPoint(float x, float y)
{
    return mixerOrigin + (juce::Point<float>(x, y) - mixerCrop.getPosition().toFloat()) * mixerScale;
}
juce::Rectangle<int> around(juce::Point<float> centre, int w, int h)
{
    return juce::Rectangle<float>(static_cast<float>(w), static_cast<float>(h))
        .withCentre(centre)
        .toNearestInt();
}

juce::Rectangle<int> sliderRail(juce::Point<float> start, juce::Point<float> end, bool vertical,
                                int crossSize, int margin)
{
    if (vertical)
        return {juce::roundToInt(start.x) - crossSize / 2, juce::roundToInt(start.y) - margin, crossSize,
                juce::roundToInt(end.y - start.y) + 2 * margin};
    return {juce::roundToInt(start.x) - margin, juce::roundToInt(start.y) - crossSize / 2,
            juce::roundToInt(end.x - start.x) + 2 * margin, crossSize};
}

void image(juce::Graphics &g, const juce::Image &source, juce::Rectangle<float> bounds)
{
    g.drawImage(source, bounds, juce::RectanglePlacement::centred);
}

void rotate(juce::Graphics &g, const juce::Image &source, juce::Point<float> axis, float diameter,
            float angle)
{
    const float scale = diameter / static_cast<float>(source.getWidth());
    auto transform = juce::AffineTransform::translation(-source.getWidth() * 0.5f, -source.getHeight() * 0.5f)
                         .scaled(scale)
                         .rotated(angle)
                         .translated(axis.x, axis.y);
    g.drawImageTransformed(source, transform);
}

void drawPlatter(juce::Graphics &g, juce::Point<float> axis, float angle)
{
    const auto &assets = art();
    g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
    g.setColour(juce::Colour(0xff181719));
    g.fillEllipse(juce::Rectangle<float>(platterDiameter - 4, platterDiameter - 4).withCentre(axis));
    rotate(g, assets.metal, axis, platterDiameter, angle);
    rotate(g, assets.vinyl, axis, platterDiameter * .874f, angle);
}
} // namespace SkratchSkin
