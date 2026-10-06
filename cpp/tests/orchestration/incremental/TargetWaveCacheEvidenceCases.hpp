#pragma once

#include <sstream>
#include <stdexcept>
#include "mqb/msvc/MsvcLibrarian.hpp"
#include "../../e2e/TargetWaveCacheEvidenceChecks.hpp"

// Included by the existing target test TU, after its deterministic fixture
// definitions. No new test executable or external compiler process is added.
namespace {
void target_wave_cache_cases() {
    namespace check = target_wave_cache_checks;
    using namespace mqb;
    using namespace mqb::orchestration;
    const auto evidence=fs::current_path()/"storage-evidence/target-wave-cache";
    check::require(!fs::exists(evidence), "fresh target wave evidence directory");
    fs::create_directories(evidence);
    unsigned calls=0;
    const auto invoke=[&](auto& target,const auto& request,const char* phase) {
        check::require(++calls<=15, "fixed additional deterministic target call budget");
        return check::recorded(target,request,evidence/phase);
    };
    const auto counts=[&](const char* phase,std::uint64_t reads,std::uint64_t writes) {
        const auto actual=check::bytes(evidence/(std::string{phase}+".compile-counts.txt"));
        check::require(actual.find("\nreads="+std::to_string(reads)+"\nwrites="+std::to_string(writes)+"\n")!=std::string::npos,
            "cache capture must not add reads or hide save attempts");
    };
    {
        AdmissionFixture f(2);
        auto cold=invoke(f.target,f.request,"01-direct-cold");
        check::require(cold.has_value(), "recorded direct cold success"); counts("01-direct-cold",0,2);
        auto warm=invoke(f.target,f.request,"02-direct-warm");
        check::require(warm && !warm->result.any_compiled,"recorded direct warm success"); counts("02-direct-warm",2,0);
        f.request.force_downstream_rebuild=true;
        auto forced=invoke(f.target,f.request,"03-direct-forced");
        check::require(forced && forced->result.any_compiled,"recorded direct forced success"); counts("03-direct-forced",0,2);
        check::require(f.runner.tools.compile_calls()==4 && f.runner.tools.link_calls()==2,
            "direct/forced recording keeps original tool count");
        check::history(evidence/"01-direct-cold",*cold);
        check::history(evidence/"02-direct-warm",*warm);
    }
    {
        AdmissionFixture f;
        // Force process completion B before A, not timing sleeps or scheduler
        // luck. The callback order is not the output vector's source order.
        struct OrderedRunner final : process::ProcessRunner {
            AdmissionToolRunner& original;
            std::binary_semaphore b_finished{0};
            std::mutex mutex;
            std::vector<std::string> finishes;
            bool order{true};
            bool fail_terminal{false};
            explicit OrderedRunner(AdmissionToolRunner& r):original(r){}
            std::expected<process::ProcessResult,process::ProcessError> run(const process::ProcessSpec& spec) override {
                const bool compile=spec.executable==original.compiler;
                if (!compile && fail_terminal) return std::unexpected(process::ProcessError{
                    .code=process::ProcessErrorCode::launch_failed,.native_code=123,.message="original terminal failure"});
                const auto name=compile?utf8_path(spec.arguments.back()).filename().string():std::string{};
                if (order && name=="a.cpp" && !b_finished.try_acquire_for(std::chrono::seconds{10}))
                    throw std::runtime_error("controlled B-before-A rendezvous failed");
                auto value=original.run(spec);
                if (compile && order) {
                    { std::scoped_lock lock(mutex); finishes.push_back(name); }
                    if (name=="b.cpp") b_finished.release();
                }
                return value;
            }
        } runner(f.runner);
        msvc::MsvcCompileExecutor executor(f.toolchain,runner);
        MsvcIncrementalCompileCoordinator compile(f.toolchain,executor);
        msvc::MsvcLinker linker(f.toolchain,runner);
        MsvcIncrementalLinkCoordinator linking(f.toolchain,linker);
        MsvcIncrementalTargetCoordinator target(compile,linking);
        f.request.max_parallel_compiles=3;
        auto cold=invoke(target,f.request,"04-split-cold");
        check::require(cold.has_value(),"recorded split cold success"); counts("04-split-cold",0,4);
        check::require(std::find(runner.finishes.begin(),runner.finishes.end(),"b.cpp") <
                       std::find(runner.finishes.begin(),runner.finishes.end(),"a.cpp"),"controlled completion order actually reversed");
        runner.order=false;
        auto warm=invoke(target,f.request,"05-split-warm");
        check::require(warm && !warm->result.any_compiled,"recorded all-hit split skips execution"); counts("05-split-warm",4,0);
        check::require(fs::remove(f.request.sources[2].artifacts.object),"prepare one actual missing object");
        auto one=invoke(target,f.request,"06-one-miss");
        check::require(one && one->result.compiles[2].result.compiled &&
            !one->result.compiles[0].result.compiled,"single original miss index executed"); counts("06-one-miss",4,1);
        check::require(fs::remove(f.request.sources[1].artifacts.object) &&
                       fs::remove(f.request.sources[3].artifacts.object),"prepare two noncontiguous misses");
        auto two=invoke(target,f.request,"07-two-misses");
        check::require(two && two->result.compiles[1].result.compiled && two->result.compiles[3].result.compiled &&
                       !two->result.compiles[0].result.compiled,"compact miss slots return in original source order"); counts("07-two-misses",4,2);
        check::require(fs::remove(f.request.sources[0].artifacts.object),"prepare race inside the only miss");
        const auto old_time=fs::last_write_time(f.header);
        f.runner.tools.mutate_on_next_compile([&]{fs::last_write_time(f.header,old_time+std::chrono::seconds{2});});
        f.request.max_parallel_compiles=1;
        const int before=f.runner.tools.compile_calls();
        auto raced=invoke(target,f.request,"08-conservative-rebuild");
        check::require(raced && f.runner.tools.compile_calls()==before+5,
            "post-execution barrier retains one miss plus four forced compiles"); counts("08-conservative-rebuild",4,5);
        for (const auto& e:raced->cache_evidence.compiles) {
            check::require(e.request.force_rebuild && e.state==CompileCacheEvidenceState::saved,
                "only second-wave forced records survive, never invalidated first-wave hits");
            check::require(std::any_of(e.cache_entry.dependencies.begin(),e.cache_entry.dependencies.end(),
                [&](const auto& d){return d==f.header;}),
                "final record keeps actual dependency path, not fabricated snapshots");
        }
        const auto blocked=f.request.sources[1].artifacts.compile_cache;
        fs::remove(blocked); write_text(blocked/"KEEP","keep original obstruction");
        f.request.force_downstream_rebuild=true;
        auto save_failed=invoke(target,f.request,"09-save-failure");
        check::require(save_failed && save_failed->cache_evidence.compiles[1].save_error &&
                       save_failed->cache_evidence.compiles[1].state==CompileCacheEvidenceState::save_failed,
                       "successful compile still owns attempted entry and original save failure"); counts("09-save-failure",0,4);
        check::require(read_bytes(blocked/"KEEP")=="keep original obstruction","cache failure preserves original blocker");
        check::require(std::any_of(save_failed->result.compiles[1].result.warnings.begin(),save_failed->result.compiles[1].result.warnings.end(),
            [](const auto& warning){return warning.code==IncrementalCompileWarningCode::cache_save_failed;}),"original save warning survives");
        auto owned=warm->cache_evidence;
        owned.compiles[0].cache_entry.outputs.clear(); owned.compiles[0].request.unit.source="different.cpp";
        owned.compiles[0].inspection_toolchain.environment.push_back({"SYNTHETIC_ONLY","not dumped"});
        check::require(warm->cache_evidence.compiles[0].cache_entry.outputs.size()==1 &&
            warm->cache_evidence.compiles[0].request.unit.source==f.request.sources[0].source &&
            warm->cache_evidence.compiles[0].inspection_toolchain.environment.empty(),"deep owning request/cache/toolchain values");
        const int links=f.runner.tools.link_calls();
        f.runner.tools.fail_sources({"b.cpp","c.cpp"});
        auto failed=invoke(target,f.request,"10-original-compile-error");
        check::require(!failed && failed.error().code==IncrementalTargetErrorCode::compile_failed &&
            failed.error().source==f.request.sources[1].source && f.runner.tools.link_calls()==links,
            "original source-order compile failure and no partial public record");
        f.runner.tools.fail_sources({}); runner.fail_terminal=true;
        auto terminal=invoke(target,f.request,"11-original-terminal-error");
        check::require(!terminal && terminal.error().code==IncrementalTargetErrorCode::link_failed,
                       "original terminal error discards private successful wave values");
        check::history(evidence/"04-split-cold",*cold); check::history(evidence/"05-split-warm",*warm);
        check::require(!f.runner.saw_execution_snapshot && detail::active_filesystem_evidence_table==nullptr,
                       "original TLS suspension/restoration preserved during all executions");
    }
    {
        AdmissionFixture f; f.toolchain.librarian=f.link;
        msvc::MsvcLibrarian librarian(f.toolchain,f.runner);
        MsvcIncrementalArchiveCoordinator archiving(f.toolchain,librarian);
        MsvcIncrementalStaticTargetCoordinator target(f.compile,archiving);
        IncrementalStaticTargetRequest request;
        request.sources=f.request.sources; request.compiler_options=f.request.compiler_options;
        request.target={.executable=f.dir.path()/".mqb/bin/recorded.lib",.link_cache=f.dir.path()/".mqb/cache/lib/recorded.cache"};
        request.working_directory=f.dir.path(); request.max_parallel_compiles=1;
        auto cold=invoke(target,request,"12-static-cold");
        check::require(cold.has_value(),"static target shares captured compile wave"); counts("12-static-cold",0,4);
        auto warm=invoke(target,request,"13-static-warm");
        check::require(warm && !warm->result.any_compiled,"static target all-hit recording"); counts("13-static-warm",4,0);
        fs::remove(request.sources[0].artifacts.object);
        const auto time=fs::last_write_time(f.header);
        f.runner.tools.mutate_on_next_compile([&]{fs::last_write_time(f.header,time+std::chrono::seconds{2});});
        auto raced=invoke(target,request,"14-static-rebuild");
        check::require(raced.has_value(),"static conservative wave recording"); counts("14-static-rebuild",4,5);
        for (const auto& e:raced->cache_evidence.compiles)
            check::require(e.request.force_rebuild && e.state==CompileCacheEvidenceState::saved,"static records only final forced wave");
        f.toolchain.librarian.clear();
        auto failed=invoke(target,request,"15-static-terminal-error");
        check::require(!failed && failed.error().code==IncrementalStaticTargetErrorCode::archive_failed,
                       "static terminal error returns no partial success record");
        check::history(evidence/"12-static-cold",*cold); check::history(evidence/"13-static-warm",*warm);
    }
    check::require(calls==15,"fixed fifteen deterministic target invocations complete");
    check::write(evidence/"completed.txt","15 deterministic product target invocations; fake process boundary; no cl/link/lib executed\n");
    std::cout << "target_wave_cache_cases: 15 deterministic target invocations passed\n";
}
} // namespace
