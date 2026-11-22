#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace desknx::ui {

inline std::size_t Utf8CodepointLength(const std::string& value,
                                       std::size_t offset) noexcept {
    if(offset>=value.size())return 0;
    const auto lead=static_cast<unsigned char>(value[offset]);
    if(lead<0x80)return 1;
    std::size_t length=0;
    if(lead>=0xC2&&lead<=0xDF)length=2;
    else if(lead>=0xE0&&lead<=0xEF)length=3;
    else if(lead>=0xF0&&lead<=0xF4)length=4;
    else return 1;
    if(offset+length>value.size())return 1;
    for(std::size_t i=1;i<length;++i) {
        const auto c=static_cast<unsigned char>(value[offset+i]);
        if((c&0xC0)!=0x80)return 1;
    }
    const auto second=static_cast<unsigned char>(value[offset+1]);
    if((lead==0xE0&&second<0xA0)||(lead==0xED&&second>=0xA0)||
       (lead==0xF0&&second<0x90)||(lead==0xF4&&second>=0x90))return 1;
    return length;
}

inline std::size_t PreviousUtf8CodepointStart(const std::string& value,
                                               std::size_t end) noexcept {
    if(end==0)return 0;
    std::size_t position=end-1;
    while(position>0&&
          (static_cast<unsigned char>(value[position])&0xC0)==0x80)--position;
    return position;
}

inline bool IsInlineTextWhitespace(char value) noexcept {
    return value==' '||value=='\t'||value=='\f'||value=='\v';
}

// Measure(candidate) returns the rendered width in pixels. Injecting the
// measurement policy makes layout deterministic and host-testable without SDL.
template<typename Measure>
std::vector<std::string> WrapUtf8Text(const std::string& value,
                                      int maxWidth,int maxLines,
                                      Measure measure,
                                      const std::string& ellipsis="\xE2\x80\xA6") {
    std::vector<std::string> lines;
    if(value.empty()||maxWidth<=0||maxLines<=0)return lines;
    std::string currentLine;
    std::size_t position=0;
    bool truncated=false;
    const auto pushLine=[&]() {
        lines.push_back(std::move(currentLine));
        currentLine.clear();
    };

    while(position<value.size()) {
        if(value[position]=='\n'||value[position]=='\r') {
            if(value[position]=='\r'&&position+1<value.size()&&
               value[position+1]=='\n')position+=2;
            else ++position;
            pushLine();
            if(static_cast<int>(lines.size())>=maxLines&&position<value.size()) {
                truncated=true;
                break;
            }
            continue;
        }
        while(position<value.size()&&IsInlineTextWhitespace(value[position]))
            ++position;
        if(position>=value.size())break;
        if(value[position]=='\n'||value[position]=='\r')continue;

        const std::size_t wordStart=position;
        while(position<value.size()&&value[position]!='\n'&&value[position]!='\r'&&
              !IsInlineTextWhitespace(value[position]))
            position+=Utf8CodepointLength(value,position);
        const std::string word=value.substr(wordStart,position-wordStart);
        const std::string candidate=currentLine.empty()?word:currentLine+" "+word;
        if(measure(candidate)<=maxWidth) {
            currentLine=candidate;
            continue;
        }
        if(!currentLine.empty()) {
            pushLine();
            if(static_cast<int>(lines.size())>=maxLines) {
                truncated=true;
                break;
            }
        }
        if(measure(word)<=maxWidth) {
            currentLine=word;
            continue;
        }

        std::string chunk;
        std::size_t wordPosition=0;
        while(wordPosition<word.size()) {
            const std::size_t length=Utf8CodepointLength(word,wordPosition);
            const std::string codepoint=word.substr(wordPosition,length);
            if(chunk.empty()||measure(chunk+codepoint)<=maxWidth) {
                chunk+=codepoint;
            } else {
                lines.push_back(std::move(chunk));
                chunk.clear();
                if(static_cast<int>(lines.size())>=maxLines) {
                    truncated=true;
                    break;
                }
                chunk=codepoint;
            }
            wordPosition+=length;
        }
        if(truncated)break;
        currentLine=std::move(chunk);
    }

    if(!truncated&&(!currentLine.empty()||lines.empty()))pushLine();
    if(truncated&&!lines.empty()) {
        std::string& lastLine=lines.back();
        if(measure(ellipsis)<=maxWidth) {
            while(!lastLine.empty()&&measure(lastLine+ellipsis)>maxWidth)
                lastLine.resize(PreviousUtf8CodepointStart(lastLine,lastLine.size()));
            if(measure(lastLine+ellipsis)<=maxWidth)lastLine+=ellipsis;
            else lastLine=ellipsis;
        } else {
            lastLine.clear();
        }
    }
    return lines;
}

} // namespace desknx::ui
