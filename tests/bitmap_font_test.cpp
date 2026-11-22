#include "../include/desknx/BitmapFont.hpp"
#include "../include/desknx/TextLayout.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>
#include <limits>

int main() {
    const std::string alphabet =
        u8"АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ";
    const std::array<std::uint32_t, 33> expected{{
        0x0410,0x0411,0x0412,0x0413,0x0414,0x0415,0x0401,
        0x0416,0x0417,0x0418,0x0419,0x041A,0x041B,0x041C,
        0x041D,0x041E,0x041F,0x0420,0x0421,0x0422,0x0423,
        0x0424,0x0425,0x0426,0x0427,0x0428,0x0429,0x042A,
        0x042B,0x042C,0x042D,0x042E,0x042F
    }};

    std::size_t offset = 0;
    std::size_t count = 0;
    while (offset < alphabet.size()) {
        const auto decoded = desknx::ui::DecodeUtf8Codepoint(alphabet, offset);
        assert(decoded.valid);
        assert(count < expected.size());
        assert(decoded.codepoint == expected[count]);
        assert(desknx::ui::IsRussianBitmapLetter(decoded.codepoint));

        const auto upper = desknx::ui::RussianBitmapGlyph(decoded.codepoint);
        bool hasInk = false;
        for (const auto row : upper) {
            assert(row <= 31);
            hasInk = hasInk || row != 0;
        }
        assert(hasInk);

        const std::uint32_t lowerCodepoint =
            decoded.codepoint == 0x0401 ? 0x0451 : decoded.codepoint + 0x20;
        assert(desknx::ui::RussianBitmapGlyph(lowerCodepoint) == upper);

        offset += decoded.length;
        ++count;
    }
    assert(count == expected.size());

    const std::string lowerAlphabet =
        u8"абвгдеёжзийклмнопрстуфхцчшщъыьэюя";
    offset = 0;
    count = 0;
    while (offset < lowerAlphabet.size()) {
        const auto decoded =
            desknx::ui::DecodeUtf8Codepoint(lowerAlphabet, offset);
        assert(decoded.valid);
        assert(desknx::ui::IsRussianBitmapLetter(decoded.codepoint));
        assert(desknx::ui::UppercaseUnicodeInitial(decoded.codepoint) ==
            expected[count]);
        offset += decoded.length;
        ++count;
    }
    assert(count == expected.size());

    std::string initials;
    desknx::ui::AppendUtf8Codepoint(initials,
        desknx::ui::UppercaseUnicodeInitial(0x0442));
    desknx::ui::AppendUtf8Codepoint(initials,
        desknx::ui::UppercaseUnicodeInitial(0x044F));
    assert(initials == u8"ТЯ");

    std::string asciiInitial;
    desknx::ui::AppendUtf8Codepoint(asciiInitial,
        desknx::ui::UppercaseUnicodeInitial('s'));
    assert(asciiInitial == "S");

    const std::string incomplete(1, static_cast<char>(0xD0));
    const auto truncated =
        desknx::ui::DecodeUtf8Codepoint(incomplete, 0);
    assert(!truncated.valid);
    assert(truncated.codepoint == 0xFFFD);
    assert(truncated.length == 1);

    const std::string overlong{"\xC0\xAF", 2};
    const auto invalidLead =
        desknx::ui::DecodeUtf8Codepoint(overlong, 0);
    assert(!invalidLead.valid);
    assert(invalidLead.length == 1);

    const std::string badContinuation{"\xE2\x28\xA1", 3};
    const auto invalidContinuation =
        desknx::ui::DecodeUtf8Codepoint(badContinuation, 0);
    assert(!invalidContinuation.valid);
    assert(invalidContinuation.length == 1);

    const std::string multiByte = u8"Привет, мир!";
    offset = 0;
    while (offset < multiByte.size()) {
        const auto decoded =
            desknx::ui::DecodeUtf8Codepoint(multiByte, offset);
        assert(decoded.valid);
        offset += decoded.length;
    }

    // Bitmap fallback width uses Unicode code points, not UTF-8 bytes.
    assert(desknx::ui::MeasureBitmapTextWidth("ABC",1)==18);
    assert(desknx::ui::MeasureBitmapTextWidth(u8"Ёж",1)==12);
    assert(desknx::ui::MeasureBitmapTextWidth(u8"Привет",2)==72);
    assert(desknx::ui::MeasureBitmapTextWidth("A\tB",1)==30);
    assert(desknx::ui::MeasureBitmapTextWidth("A\r\nB",1)==12);
    assert(desknx::ui::MeasureBitmapTextWidth("",1)==0);
    assert(desknx::ui::MeasureBitmapTextWidth("ABC",0)==0);
    const std::string invalidBitmapUtf8{"\xD0",1};
    assert(desknx::ui::MeasureBitmapTextWidth(invalidBitmapUtf8,1)==6);

    const auto bitmapMeasure=[](const std::string& value) {
        return desknx::ui::MeasureBitmapTextWidth(value,1);
    };
    const auto wrappedRussian=desknx::ui::WrapUtf8Text(
        u8"Привет мир",36,2,bitmapMeasure);
    assert(wrappedRussian.size()==2);
    assert(wrappedRussian[0]==u8"Привет");
    assert(wrappedRussian[1]==u8"мир");
    const auto truncatedRussian=desknx::ui::WrapUtf8Text(
        u8"Привет мир снова",36,2,bitmapMeasure);
    assert(truncatedRussian.size()==2);
    assert(truncatedRussian[0]==u8"Привет");
    assert(truncatedRussian[1]==u8"мир…");
    assert(desknx::ui::MeasureBitmapTextWidth(
        "A",std::numeric_limits<int>::max())==std::numeric_limits<int>::max());

    return 0;
}
