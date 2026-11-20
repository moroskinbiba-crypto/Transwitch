#include <desknx/LaunchHistory.hpp>
#include <desknx/Layout.hpp>
#include <desknx/Model.hpp>
#include <desknx/PersistentFiles.hpp>
#include <desknx/SettingsFile.hpp>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

void writeFile(const std::string& path,const std::string& contents) {
    std::ofstream out(path.c_str(),std::ios::binary|std::ios::trunc);
    assert(out.good());
    out.write(contents.data(),static_cast<std::streamsize>(contents.size()));
    out.close();
    assert(out.good());
}
std::string readFile(const std::string& path) {
    std::ifstream in(path.c_str(),std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>());
}
bool exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(),&st)==0;
}

bool validateSettings(const std::string& contents,void*) {
    std::vector<desknx::settings_file::Entry> entries;
    return desknx::settings_file::parse(contents,entries);
}
bool validateLayout(const std::string& contents,void* context) {
    auto* parsed=static_cast<desknx::layout::LoadResult*>(context);
    return parsed&&desknx::layout::parse(contents,*parsed);
}
struct ActiveContext {
    const std::vector<desknx::model::Workspace>* workspaces;
    int index;
};
bool validateActive(const std::string& contents,void* opaque) {
    auto* context=static_cast<ActiveContext*>(opaque);
    if(!context||!context->workspaces)return false;
    int candidate=context->index;
    if(!desknx::layout::parseActiveWorkspaceState(contents,*context->workspaces,candidate))
        return false;
    context->index=candidate;
    return true;
}
bool validateHistoryNroPath(const std::string& path,void*) {
    return path=="sdmc:/switch/emulator.nro";
}
bool validateHistoryFile(const std::string& contents,void* opaque) {
    auto* output=static_cast<std::vector<desknx::launch_history::Entry>*>(opaque);
    return output&&desknx::launch_history::parse(
        contents,*output,validateHistoryNroPath,nullptr);
}

struct CommitPlan {
    int calls{0};
    int failAt{0};
    std::vector<std::string> phases;
};
bool commitStub(void* opaque,const char* phase) {
    auto* plan=static_cast<CommitPlan*>(opaque);
    ++plan->calls;
    plan->phases.emplace_back(phase?phase:"");
    return plan->failAt==0||plan->calls!=plan->failAt;
}

} // namespace

int main() {
    char directoryTemplate[]="/tmp/desknx-production-persistence-XXXXXX";
    char* directory=mkdtemp(directoryTemplate);
    assert(directory);
    const std::string root(directory);
    const std::string primary=root+"/state.dat";
    const std::string backup=root+"/state.bak";
    const std::string temporary=root+"/state.tmp";

    // Settings: a valid backup is selected if the primary has a bad signature.
    const std::string validSettings="DESKNXSET1\ntheme=3\nvisual_theme=socoma\nlanguage=ru\n";
    writeFile(primary,"INVALID SETTINGS\n");
    writeFile(backup,validSettings);
    std::string loaded="unchanged";
    auto source=desknx::persist::readValidatedWithBackup(
        primary.c_str(),backup.c_str(),desknx::settings_file::MaxBytes,
        validateSettings,nullptr,loaded);
    assert(source==desknx::persist::FileSource::Backup);
    assert(loaded==validSettings);
    std::vector<desknx::settings_file::Entry> settings;
    assert(desknx::settings_file::parse(loaded,settings));
    assert(desknx::settings_file::find(settings,"theme")&&
           *desknx::settings_file::find(settings,"theme")=="3");
    assert(desknx::settings_file::find(settings,"visual_theme")&&
           *desknx::settings_file::find(settings,"visual_theme")=="socoma");

    // A valid primary always wins, and a malformed oversized primary cannot
    // suppress a valid backup. Failed reads leave the caller's output intact.
    writeFile(primary,"DESKNXSET1\ntheme=5\n");
    source=desknx::persist::readValidatedWithBackup(
        primary.c_str(),backup.c_str(),desknx::settings_file::MaxBytes,
        validateSettings,nullptr,loaded);
    assert(source==desknx::persist::FileSource::Primary);
    assert(loaded=="DESKNXSET1\ntheme=5\n");
    writeFile(primary,std::string(5000,'X'));
    source=desknx::persist::readValidatedWithBackup(
        primary.c_str(),backup.c_str(),desknx::settings_file::MaxBytes,
        validateSettings,nullptr,loaded);
    assert(source==desknx::persist::FileSource::Backup);
    writeFile(backup,"also invalid");
    loaded="preserve-me";
    source=desknx::persist::readValidatedWithBackup(
        primary.c_str(),backup.c_str(),desknx::settings_file::MaxBytes,
        validateSettings,nullptr,loaded);
    assert(source==desknx::persist::FileSource::None&&loaded=="preserve-me");

    // Production layout parser is the validator for backup recovery.
    using namespace desknx::model;
    std::vector<Workspace> workspaces{
        Workspace{"Games",{Tile{Kind::Game,"Test","0100000000000001",0,0,1,1,0}}},
        Workspace{"Homebrew",{}}
    };
    const std::string serializedLayout=desknx::layout::serialize(workspaces,1);
    assert(!serializedLayout.empty());
    writeFile(primary,"BROKEN LAYOUT\n");
    writeFile(backup,serializedLayout);
    desknx::layout::LoadResult layoutResult{};
    source=desknx::persist::readValidatedWithBackup(
        primary.c_str(),backup.c_str(),1024*1024,validateLayout,&layoutResult,loaded);
    assert(source==desknx::persist::FileSource::Backup);
    assert(layoutResult.workspaces.size()==2&&layoutResult.activeWorkspace==1);
    assert(layoutResult.workspaces[0].tiles.size()==1);

    // Active workspace state is validated against the actual workspace shape;
    // a syntactically valid marker for another layout is rejected.
    const std::string marker=desknx::layout::serializeActiveWorkspaceState(workspaces,1);
    assert(!marker.empty());
    ActiveContext active{&workspaces,0};
    writeFile(primary,"DESKNX-ACTIVE1\n0\n2\n0\nGames\n");
    writeFile(backup,marker);
    source=desknx::persist::readValidatedWithBackup(
        primary.c_str(),backup.c_str(),512,validateActive,&active,loaded);
    assert(source==desknx::persist::FileSource::Backup&&active.index==1);

    // History parser preserves the legacy H1 format and writes timestamped H2.
    using desknx::launch_history::Entry;
    const std::string legacyHistory=
        "DESKNXH1\nG\tZelda\t0100000000000001\nN\tRetro\tsdmc:/switch/emulator.nro\n";
    std::vector<Entry> history;
    assert(desknx::launch_history::parse(legacyHistory,history,validateHistoryNroPath,nullptr));
    assert(history.size()==2&&history[0].timestamp==0);
    history[0].timestamp=1770000000;
    const std::string savedHistory=desknx::launch_history::serialize(history);
    assert(savedHistory.find("DESKNXH2\n")==0);
    std::vector<Entry> historyRoundTrip;
    assert(desknx::launch_history::parse(savedHistory,historyRoundTrip,validateHistoryNroPath,nullptr));
    assert(historyRoundTrip.size()==2&&historyRoundTrip[0].timestamp==1770000000);
    writeFile(primary,"BAD HISTORY\n");
    writeFile(backup,savedHistory);
    history.clear();
    source=desknx::persist::readValidatedWithBackup(
        primary.c_str(),backup.c_str(),desknx::launch_history::MaxFileBytes,
        validateHistoryFile,&history,loaded);
    assert(source==desknx::persist::FileSource::Backup&&history.size()==2);

    // Inject failure at the first SD durability boundary: publication must not
    // begin and the prior destination remains byte-for-byte intact.
    writeFile(primary,"old-state");
    CommitPlan firstCommitFailure{0,1,{}};
    auto writeStatus=desknx::persist::writeAtomically(
        temporary.c_str(),primary.c_str(),backup.c_str(),"new-state",
        commitStub,&firstCommitFailure,"settings-temp","settings-publish",true);
    assert(writeStatus==desknx::persist::WriteStatus::TempCommitFailed);
    assert(readFile(primary)=="old-state");
    assert(!exists(temporary));
    assert(firstCommitFailure.calls==1&&
           firstCommitFailure.phases[0]=="settings-temp");

    // Inject failure after rename: the new primary and old backup both remain.
    CommitPlan publishFailure{0,2,{}};
    writeStatus=desknx::persist::writeAtomically(
        temporary.c_str(),primary.c_str(),backup.c_str(),"published-new-state",
        commitStub,&publishFailure,"history-temp","history-publish",true);
    assert(writeStatus==desknx::persist::WriteStatus::PublishCommitFailed);
    assert(readFile(primary)=="published-new-state");
    assert(readFile(backup)=="old-state");
    assert(!exists(temporary));
    assert(publishFailure.calls==2&&
           publishFailure.phases[0]=="history-temp"&&
           publishFailure.phases[1]=="history-publish");

    // A subsequent successful write rotates the last committed primary into
    // the backup rather than discarding recovery material.
    CommitPlan success{};
    writeStatus=desknx::persist::writeAtomically(
        temporary.c_str(),primary.c_str(),backup.c_str(),"latest-state",
        commitStub,&success,"layout-temp","layout-publish",true);
    assert(writeStatus==desknx::persist::WriteStatus::Success);
    assert(readFile(primary)=="latest-state");
    assert(readFile(backup)=="published-new-state");
    assert(success.calls==2);

    // Marker writes deliberately use a single post-publication commit. A failed
    // commit still retains the previous marker in the backup path.
    CommitPlan markerFailure{0,1,{}};
    writeStatus=desknx::persist::writeAtomically(
        temporary.c_str(),primary.c_str(),backup.c_str(),"new-marker",
        commitStub,&markerFailure,"marker-temp","marker-publish",false);
    assert(writeStatus==desknx::persist::WriteStatus::PublishCommitFailed);
    assert(readFile(primary)=="new-marker");
    assert(readFile(backup)=="latest-state");
    assert(markerFailure.calls==1&&markerFailure.phases[0]=="marker-publish");

    // An unwritable temp path is reported before any commit or destination change.
    CommitPlan noCalls{};
    const std::string missingDirectory=root+"/missing/state.tmp";
    writeStatus=desknx::persist::writeAtomically(
        missingDirectory.c_str(),primary.c_str(),backup.c_str(),"blocked",
        commitStub,&noCalls,"temp","publish",true);
    assert(writeStatus==desknx::persist::WriteStatus::OpenFailed);
    assert(noCalls.calls==0&&readFile(primary)=="new-marker");

    std::remove(primary.c_str());
    std::remove(backup.c_str());
    std::remove(temporary.c_str());
    rmdir(root.c_str());
    return 0;
}
