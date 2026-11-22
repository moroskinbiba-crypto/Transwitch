#include "../include/desknx/TextLayout.hpp"
#include <cassert>
#include <string>

namespace {
int codepointWidth(const std::string& value) {
    int count=0;
    for(std::size_t pos=0;pos<value.size();) {
        const std::size_t length=desknx::ui::Utf8CodepointLength(value,pos);
        assert(length>0);
        pos+=length;
        ++count;
    }
    return count;
}
}

int main() {
    using desknx::ui::WrapUtf8Text;
    const auto words=WrapUtf8Text("hello world",7,3,codepointWidth);
    assert(words.size()==2&&words[0]=="hello"&&words[1]=="world");

    const auto cyrillic=WrapUtf8Text("дом",2,3,codepointWidth);
    assert(cyrillic.size()==2&&cyrillic[0]=="до"&&cyrillic[1]=="м");

    const auto truncated=WrapUtf8Text("alpha beta gamma",7,2,codepointWidth);
    assert(truncated.size()==2&&truncated[0]=="alpha");
    assert(truncated[1]=="beta\xE2\x80\xA6");

    const auto exact=WrapUtf8Text("hello",5,1,codepointWidth);
    assert(exact.size()==1&&exact[0]=="hello");

    const auto paragraphs=WrapUtf8Text("one\ntwo",10,2,codepointWidth);
    assert(paragraphs.size()==2&&paragraphs[0]=="one"&&paragraphs[1]=="two");

    assert(WrapUtf8Text("hello",10,0,codepointWidth).empty());

    const auto malformed=WrapUtf8Text(std::string("A\xC3",2),8,2,codepointWidth);
    assert(malformed.size()==1&&malformed[0].size()==2);
    return 0;
}
