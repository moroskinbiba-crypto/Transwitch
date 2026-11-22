#include "../include/desknx/Typography.hpp"

#include <cassert>

int main() {
    using desknx::ui::TextRole;
    using desknx::ui::TypographyFor;
    using desknx::ui::TypographyFontPixelSize;

    const auto display=TypographyFor(TextRole::Display);
    const auto heading=TypographyFor(TextRole::Heading);
    const auto subheading=TypographyFor(TextRole::Subheading);
    const auto body=TypographyFor(TextRole::Body);
    const auto caption=TypographyFor(TextRole::Caption);
    const auto metadata=TypographyFor(TextRole::Metadata);

    assert(display.fontScale>heading.fontScale);
    assert(heading.fontScale>subheading.fontScale);
    assert(subheading.fontScale>body.fontScale);
    assert(body.fontScale==caption.fontScale);
    assert(caption.fontScale==metadata.fontScale);

    // Semantic roles must preserve the established visual scale used before
    // the migration. This prevents a role rename from silently resizing screens.
    assert(display.fontScale==5);
    assert(heading.fontScale==3);
    assert(subheading.fontScale==2);
    assert(body.fontScale==1);
    assert(caption.fontScale==1);
    assert(metadata.fontScale==1);

    assert(display.lineHeight>=TypographyFontPixelSize(TextRole::Display));
    assert(heading.lineHeight>=TypographyFontPixelSize(TextRole::Heading));
    assert(subheading.lineHeight>=TypographyFontPixelSize(TextRole::Subheading));
    assert(body.lineHeight>=TypographyFontPixelSize(TextRole::Body));
    assert(caption.lineHeight>=TypographyFontPixelSize(TextRole::Caption));
    assert(metadata.lineHeight>=TypographyFontPixelSize(TextRole::Metadata));

    assert(display.maxLines==2);
    assert(heading.maxLines==1);
    assert(subheading.maxLines==2);
    assert(body.maxLines==3);
    assert(caption.maxLines==2);
    assert(metadata.maxLines==1);
    return 0;
}
