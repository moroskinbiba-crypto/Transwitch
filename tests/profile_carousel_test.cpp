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
