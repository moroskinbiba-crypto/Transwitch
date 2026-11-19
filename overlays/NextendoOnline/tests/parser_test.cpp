#include <nextendo/parser.hpp>
#include <cassert>
#include <cstdio>

int main() {
    nextendo::PlayerSnapshot snapshot;
    const std::string json = R"({"ok":true,"counts":{"0100152000022000":12,"0100C2500FC20000":7}})";
    assert(nextendo::parser::parseCounts(json, snapshot));
    assert(snapshot.perTitle.size() == 2);
    assert(snapshot.perTitle[0x0100152000022000ull] == 12);
    assert(snapshot.perTitle[0x0100C2500FC20000ull] == 7);
    assert(nextendo::parser::plausible(snapshot));

    const std::string merge = R"({"counts":{"0100152000022000":99,"01006A800016E000":3}})";
    assert(nextendo::parser::parseCounts(merge, snapshot, true));
    assert(snapshot.perTitle[0x0100152000022000ull] == 12);
    assert(snapshot.perTitle[0x01006A800016E000ull] == 3);

    const std::string invalid = R"({"counts":{"not-a-title":3}})";
    nextendo::PlayerSnapshot empty;
    assert(nextendo::parser::parseCounts(invalid, empty));
    assert(empty.perTitle.empty());
    assert(!nextendo::parser::parseCounts(R"({"counts":{"0100152000022000":-1}})", empty));
    assert(!nextendo::parser::parseCounts(R"({"counts":[1,2,3]})", empty));

    unsigned connected = 0;
    assert(nextendo::parser::readConnected(R"({"connected":42})", connected));
    assert(connected == 42);
    assert(nextendo::parser::readBoolField(R"({"online":true})", "online"));
    assert(!nextendo::parser::readBoolField(R"({"online":false})", "online"));

    std::puts("nextendo_overlay_parser_test: ok");
    return 0;
}
