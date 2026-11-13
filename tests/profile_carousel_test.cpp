#include <desknx/ProfileCarousel.hpp>
#include <cassert>
#include <cstdio>

using namespace desknx::profile;

Profile makeProfile(std::uint64_t lo, const char* name) {
    Profile p;
    p.uid.lo = lo;
    p.uid.hi = 0;
    p.nickname = name;
    p.avatarCacheKey = std::string("avatar_") + name;
    return p;
}

void testEmpty() {
    CarouselState c;
    assert(c.tileCount() == 0);
    assert(c.selectedProfile() == nullptr);
}

void testSingleProfile() {
    CarouselState c;
    c.setProfiles({ makeProfile(1, "Alice") }, false);
    assert(c.tileCount() == 1);
    assert(c.selectedKind() == CardKind::Profile);
    assert(c.selected() == 0);
    const Profile* p = c.selectedProfile();
    assert(p != nullptr && p->nickname == "Alice");
}

void testProfilePlusAddUser() {
    CarouselState c;
    c.setProfiles({ makeProfile(1, "Alice"), makeProfile(2, "Bob") }, true);
    assert(c.tileCount() == 3);
    assert(c.tileKind(0) == CardKind::Profile);
    assert(c.tileKind(1) == CardKind::Profile);
    assert(c.tileKind(2) == CardKind::AddUser);
}

void testNavigationWraps() {
    CarouselState c;
    c.setProfiles({ makeProfile(1, "Alice"), makeProfile(2, "Bob") }, false);
    assert(c.selected() == 0);
    c.moveHorizontal(-1);
    assert(c.selected() == 1);  // wrapped to end
    c.moveHorizontal(1);
    assert(c.selected() == 0);  // back
}

void testShowHide() {
    CarouselState c;
    c.setProfiles({ makeProfile(1, "Alice") }, false);
    assert(!c.isActive());
    c.show();
    assert(c.isActive());
    assert(c.isOpen());
    c.hide();
    assert(c.isActive());
    assert(!c.isOpen());
    c.finishHide();
    assert(!c.isActive());
}

void testLockToggle() {
    CarouselState c;
    c.setProfiles({ makeProfile(1, "Alice"), makeProfile(2, "Bob") }, false);
    assert(!c.lockedUid().has_value());
    c.selectTile(0);
    c.toggleLockSelected();
    assert(c.lockedUid().has_value());
    assert(c.lockedUid()->lo == 1);
    assert(c.selectedIsLocked());
    // move to Bob — not locked
    c.selectTile(1);
    assert(!c.selectedIsLocked());
    // unlock Alice by selecting and toggling again
    c.selectTile(0);
    c.toggleLockSelected();
    assert(!c.lockedUid().has_value());
}

void testLockOnAddUserIsNoop() {
    CarouselState c;
    c.setProfiles({ makeProfile(1, "Alice") }, true);
    c.selectTile(1);  // "Add user"
    assert(c.selectedKind() == CardKind::AddUser);
    c.toggleLockSelected();
    assert(!c.lockedUid().has_value());  // unchanged
}

void testSetProfilesClampsSelection() {
    CarouselState c;
    c.setProfiles({ makeProfile(1, "A"), makeProfile(2, "B"), makeProfile(3, "C") }, false);
    c.selectTile(2);
    // Replace with a shorter list
    c.setProfiles({ makeProfile(1, "A") }, false);
    assert(c.selected() == 0);  // clamped
}

void testAnnouncement() {
    CarouselState c;
    c.setProfiles({ makeProfile(1, "Alice"), makeProfile(2, "Bob") }, true);
    c.selectTile(0);
    assert(c.announcement() == "Alice (1 of 2)");
    c.selectTile(1);
    assert(c.announcement() == "Bob (2 of 2)");
    c.selectTile(2);  // Add user
    assert(c.announcement() == "Add user");
}

void testProfileAt() {
    CarouselState c;
    c.setProfiles({ makeProfile(10, "Alice"), makeProfile(20, "Bob") }, false);
    assert(c.profileAt(0)->uid.lo == 10);
    assert(c.profileAt(1)->uid.lo == 20);
    assert(c.profileAt(2) == nullptr);
    assert(c.profileAt(-1) == nullptr);
}

int main() {
    testEmpty();
    testSingleProfile();
    testProfilePlusAddUser();
    testNavigationWraps();
    testShowHide();
    testLockToggle();
    testLockOnAddUserIsNoop();
    testSetProfilesClampsSelection();
    testAnnouncement();
    testProfileAt();
    std::puts("profile_carousel_test: ok");
    return 0;
}

{"content":"#include <desknx/ProfileCarousel.hpp>\n#include <cassert>\n#include <cstdio>\n\nusing namespace desknx::profile;\n\nProfile makeProfile(std::uint64_t lo, const char* name) {\n    Profile p;\n    p.uid.lo = lo;\n    p.uid.hi = 0;\n    p.nickname = name;\n    p.avatarCacheKey = std::string(\"avatar_\") + name;\n    return p;\n}\n\nvoid testEmpty() {\n    CarouselState c;\n    assert(c.tileCount() == 0);\n    assert(c.selectedProfile() == nullptr);\n}\n\nvoid testSingleProfile() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(1, \"Alice\") }, false);\n    assert(c.tileCount() == 1);\n    assert(c.selectedKind() == CardKind::Profile);\n    assert(c.selected() == 0);\n    const Profile* p = c.selectedProfile();\n    assert(p != nullptr && p->nickname == \"Alice\");\n}\n\nvoid testProfilePlusAddUser() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(1, \"Alice\"), makeProfile(2, \"Bob\") }, true);\n    assert(c.tileCount() == 3);\n    assert(c.tileKind(0) == CardKind::Profile);\n    assert(c.tileKind(1) == CardKind::Profile);\n    assert(c.tileKind(2) == CardKind::AddUser);\n}\n\nvoid testNavigationWraps() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(1, \"Alice\"), makeProfile(2, \"Bob\") }, false);\n    assert(c.selected() == 0);\n    c.moveHorizontal(-1);\n    assert(c.selected() == 1);  // wrapped to end\n    c.moveHorizontal(1);\n    assert(c.selected() == 0);  // back\n}\n\nvoid testShowHide() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(1, \"Alice\") }, false);\n    assert(!c.isActive());\n    c.show();\n    assert(c.isActive());\n    assert(c.isOpen());\n    c.hide();\n    assert(c.isActive());\n    assert(!c.isOpen());\n    c.finishHide();\n    assert(!c.isActive());\n}\n\nvoid testLockToggle() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(1, \"Alice\"), makeProfile(2, \"Bob\") }, false);\n    assert(!c.lockedUid().has_value());\n    c.selectTile(0);\n    c.toggleLockSelected();\n    assert(c.lockedUid().has_value());\n    assert(c.lockedUid()->lo == 1);\n    assert(c.selectedIsLocked());\n    // move to Bob — not locked\n    c.selectTile(1);\n    assert(!c.selectedIsLocked());\n    // unlock Alice by selecting and toggling again\n    c.selectTile(0);\n    c.toggleLockSelected();\n    assert(!c.lockedUid().has_value());\n}\n\nvoid testLockOnAddUserIsNoop() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(1, \"Alice\") }, true);\n    c.selectTile(1);  // \"Add user\"\n    assert(c.selectedKind() == CardKind::AddUser);\n    c.toggleLockSelected();\n    assert(!c.lockedUid().has_value());  // unchanged\n}\n\nvoid testSetProfilesClampsSelection() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(1, \"A\"), makeProfile(2, \"B\"), makeProfile(3, \"C\") }, false);\n    c.selectTile(2);\n    // Replace with a shorter list\n    c.setProfiles({ makeProfile(1, \"A\") }, false);\n    assert(c.selected() == 0);  // clamped\n}\n\nvoid testAnnouncement() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(1, \"Alice\"), makeProfile(2, \"Bob\") }, true);\n    c.selectTile(0);\n    assert(c.announcement() == \"Alice (1 of 2)\");\n    c.selectTile(1);\n    assert(c.announcement() == \"Bob (2 of 2)\");\n    c.selectTile(2);  // Add user\n    assert(c.announcement() == \"Add user\");\n}\n\nvoid testProfileAt() {\n    CarouselState c;\n    c.setProfiles({ makeProfile(10, \"Alice\"), makeProfile(20, \"Bob\") }, false);\n    assert(c.profileAt(0)->uid.lo == 10);\n    assert(c.profileAt(1)->uid.lo == 20);\n    assert(c.profileAt(2) == nullptr);\n    assert(c.profileAt(-1) == nullptr);\n}\n\nint main() {\n    testEmpty();\n    testSingleProfile();\n    testProfilePlusAddUser();\n    testNavigationWraps();\n    testShowHide();\n    testLockToggle();\n    testLockOnAddUserIsNoop();\n    testSetProfilesClampsSelection();\n    testAnnouncement();\n    testProfileAt();\n    std::puts(\"profile_carousel_test: ok\");\n    return 0;\n}\n"}