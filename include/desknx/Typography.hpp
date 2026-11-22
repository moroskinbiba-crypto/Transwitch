#pragma once

#include <cstdint>

namespace desknx::ui {

enum class TextRole : std::uint8_t {
    Display,
    Heading,
    Subheading,
    Body,
    Caption,
    Metadata
};

struct TypographyStyle {
    int fontScale;
    int lineHeight;
    int maxLines;
};

// Scale values intentionally map to the existing cached FontRenderer sizes.
// This lets screens adopt semantic roles without invalidating their current
// text-cache keys or changing the established visual scale in one migration.
constexpr TypographyStyle TypographyFor(TextRole role) noexcept {
    switch(role) {
        case TextRole::Display:    return {5,50,2};
        case TextRole::Heading:   return {3,32,1};
        case TextRole::Subheading:return {2,24,2};
        case TextRole::Body:      return {1,16,3};
        case TextRole::Caption:   return {1,14,2};
        case TextRole::Metadata:  return {1,12,1};
    }
    return {1,16,3};
}

constexpr int TypographyFontPixelSize(TextRole role) noexcept {
    const int scale=TypographyFor(role).fontScale;
    return 10+(scale-1)*8;
}

} // namespace desknx::ui
