// Standalone TEST-ONLY differential probe, deliberately not a *_tests.cpp product
// test entry. Built by the dedicated correctness workflow through pinned MQB.
#include <algorithm>
#include <array>
#include <climits>
#include <codecvt>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <typeinfo>
#include "v9_reader_prototype.hpp"
namespace old = mqb_v9_oracle;
namespace trial = mqb_v9_prototype;
namespace fs = std::filesystem;

struct Observed {
    std::optional<old::CacheRecord> record;
    int exception{};
    int code{};
    trial::Route route{trial::Route::byte_fallback};
    std::string exception_type;
    std::string error_category;
};
template<class F> Observed observe(F&& f) {
    try { const auto x = f(); return {x.record, 0, 0, x.route, {}, {}}; }
    catch (const std::bad_alloc&) { throw; }
    catch (const fs::filesystem_error& e) { return {std::nullopt, 1, e.code().value(), trial::Route::byte_fallback, typeid(e).name(), e.code().category().name()}; }
    catch (const std::ios_base::failure& e) { return {std::nullopt, 2, e.code().value(), trial::Route::byte_fallback, typeid(e).name(), e.code().category().name()}; }
    catch (const std::system_error& e) { return {std::nullopt, 3, e.code().value(), trial::Route::byte_fallback, typeid(e).name(), e.code().category().name()}; }
    catch (const std::exception& e) { return {std::nullopt, 4, 0, trial::Route::byte_fallback, typeid(e).name(), {}}; }
}
bool equal(const std::optional<old::CacheRecord>& a, const std::optional<old::CacheRecord>& b) {
    if (a.has_value() != b.has_value()) return false;
    if (!a) return true;
    if (a->target_architecture != b->target_architecture || a->host_architecture != b->host_architecture ||
        a->preference != b->preference || a->vc_tools_root != b->vc_tools_root ||
        a->binary_stamp != b->binary_stamp || a->ambient_path != b->ambient_path ||
        a->effective_path != b->effective_path || a->environment.size() != b->environment.size()) return false;
    for (std::size_t i = 0; i < a->environment.size(); ++i)
        if (a->environment[i].name != b->environment[i].name || a->environment[i].value != b->environment[i].value ||
            a->environment[i].remove != b->environment[i].remove) return false;
    return true;
}
void need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Totals {
    std::size_t cases{}, fast{}, fallback{}, refused{}, accepted{}, exceptions{}, file_cases{}, transport_controls{};
} totals;
Observed reference_bytes(std::string_view bytes, const std::locale& locale) {
    return observe([&]() -> trial::Result {
        if (bytes.size() > old::max_cache_size) return {std::nullopt, trial::Route::transport_refused};
        std::istringstream input{std::string(bytes)}; input.imbue(locale);
        return {old::read_record(input), trial::Route::byte_fallback};
    });
}
void compare(const Observed& ref, const Observed& candidate) {
    ++totals.cases;
    if (ref.exception != candidate.exception || ref.code != candidate.code ||
        ref.exception_type != candidate.exception_type || ref.error_category != candidate.error_category ||
        !equal(ref.record, candidate.record)) {
        std::cerr << "Mismatch at deterministic case " << totals.cases << "; no input values emitted.\n";
        throw std::runtime_error("Differential mismatch");
    }
    if (candidate.exception) ++totals.exceptions;
    else {
        if (candidate.record) ++totals.accepted;
        switch (candidate.route) {
        case trial::Route::canonical: ++totals.fast; break;
        case trial::Route::transport_refused: ++totals.refused; break;
        default: ++totals.fallback;
        }
    }
}
Observed check(std::string_view bytes, const std::locale& locale = std::locale::classic(), int expected_route = -1) {
    const auto ref = reference_bytes(bytes, locale);
    const auto candidate = observe([&] { return trial::parse_bytes(bytes, locale); });
    compare(ref, candidate);
    if (expected_route >= 0) need(candidate.exception == 0 && static_cast<int>(candidate.route) == expected_route, "Unexpected path selection");
    return candidate;
}
std::string serialized(const old::CacheRecord& record, const std::locale& locale = std::locale::classic()) {
    std::ostringstream output; output.imbue(locale);
    need(old::write_record(output, record), "Synthetic writer failed"); return output.str();
}
old::CacheRecord fixture() {
    old::CacheRecord r;
    r.target_architecture="x64"; r.host_architecture="x64"; r.preference=1;
    r.vc_tools_root=old::detail::path_from_utf8("C:/Synthetic/VC/Tools/14.50/");
    r.binary_stamp="synthetic-stamp"; r.ambient_path="C:\\Synthetic\\bin;D:\\Other";
    r.effective_path="C:\\Synthetic\\bin;D:\\Other;C:\\Toolset";
    for (auto name : {"INCLUDE","LIB","LIBPATH","VCToolsInstallDir","WindowsSdkDir","WindowsSDKVersion","UniversalCRTSdkDir","UCRTVersion","NETFXSDKDir"})
        r.environment.push_back({name, "Synthetic value with spaces \\\""});
    return r;
}
std::string replace_once(std::string input, std::string_view from, std::string_view to) {
    const auto at = input.find(from); need(at != std::string::npos, "Bad synthetic mutation anchor");
    input.replace(at, from.size(), to); return input;
}
class AtWhitespace final : public std::ctype<char> {
    static const mask* masks() {
        static const auto value = [] {
            std::array<mask, table_size> result{};
            std::copy_n(classic_table(), table_size, result.data());
            result[static_cast<unsigned char>('@')] |= space; return result;
        }();
        return value.data();
    }
public: AtWhitespace() : std::ctype<char>(masks()) {}
};
class Grouping final : public std::numpunct<char> {
    char do_thousands_sep() const override { return ','; }
    std::string do_grouping() const override { return "\3"; }
};
class ShiftNumber final : public std::num_get<char> {
    iter_type do_get(iter_type first, iter_type last, std::ios_base& io, std::ios_base::iostate& state, long& value) const override {
        const auto end = std::num_get<char>::do_get(first,last,io,state,value);
        if (!(state & std::ios::failbit) && value < LONG_MAX) ++value;
        return end;
    }
};
// Non-noconv facet proves the file fallback does not bypass filebuf conversion.
class AtCodecvt final : public std::codecvt<char,char,std::mbstate_t> {
    result do_in(std::mbstate_t&, const char* first, const char* last, const char*& next,
                 char* output, char* end, char*& written) const override {
        next=first; written=output;
        while (next != last && written != end) { const char c=*next++; *written++=(c=='@' ? ' ' : c); }
        return next == last ? ok : partial;
    }
    bool do_always_noconv() const noexcept override { return false; }
    int do_encoding() const noexcept override { return 1; }
    int do_max_length() const noexcept override { return 1; }
    int do_length(std::mbstate_t&, const char* first, const char* last, std::size_t max) const override {
        return static_cast<int>(std::min(static_cast<std::size_t>(last-first), max));
    }
};
void check_file(const fs::path& file, const std::string& bytes, const std::locale& locale, int expected_route) {
    { std::ofstream output(file, std::ios::binary | std::ios::trunc);
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size())); need(bool(output), "Synthetic write failed"); }
    const auto ref=observe([&]() -> trial::Result {
        if (fs::file_size(file) > old::max_cache_size) return {std::nullopt,trial::Route::transport_refused};
        std::ifstream input; input.imbue(locale); input.open(file,std::ios::binary);
        return {old::read_record(input),trial::Route::stream_fallback};
    });
    const auto candidate=observe([&] {
        std::ifstream input; input.imbue(locale); input.open(file,std::ios::binary);
        return trial::read_prechecked(input,fs::file_size(file));
    });
    compare(ref,candidate); ++totals.file_cases;
    need(candidate.exception == 0 && static_cast<int>(candidate.route) == expected_route,"File route mismatch");
}
class BrokenRead final : public std::streambuf {
    std::streamsize xsgetn(char*,std::streamsize) override { throw std::runtime_error("Injected read failure"); }
};
void transport(const std::string& base) {
    auto refuse=[&](std::istream& stream,std::uintmax_t size) {
        const auto result=trial::read_prechecked(stream,size);
        need(!result.record && result.route==trial::Route::transport_refused,"Failed transport was accepted/retried");
        ++totals.transport_controls;
    };
    std::istringstream shorter(base.substr(0,base.size()-1));refuse(shorter,base.size());
    std::istringstream longer(base+"x");refuse(longer,base.size());
    std::istringstream too_big(base);refuse(too_big,old::max_cache_size+1);
    need(too_big.tellg()==0,"Oversize admission consumed bytes");
    std::istringstream bad(base);bad.setstate(std::ios::badbit);refuse(bad,base.size());
    BrokenRead broken;std::istream broken_input(&broken);refuse(broken_input,base.size());
    std::istringstream empty; const auto result=trial::read_prechecked(empty,0);
    need(!result.record && result.route==trial::Route::byte_fallback,"Empty input was not rejected by old grammar");++totals.transport_controls;
}
void run(const fs::path& work) {
    const auto classic=std::locale::classic();
    const std::locale whitespace(classic,new AtWhitespace), grouping(classic,new Grouping),
        numeric(classic,new ShiftNumber), codecvt(classic,new AtCodecvt);
    auto r=fixture();const auto base=serialized(r);
    need(check(base,classic,0).record.has_value(),"Canonical fixture rejected");
    // Delimiter/escape/control bytes, including embedded NUL, survive fields.
    for (unsigned int byte=0;byte<256;++byte) {
        r=fixture();r.environment[0].value=std::string("x")+static_cast<char>(byte)+"y";
        need(check(serialized(r),classic,0).record.has_value(),"Quoted byte lost");
    }
    for (std::size_t n : {0u,1u,63u,64u}) {
        r=fixture();r.environment.assign(n,{"include",""});check(serialized(r),classic,0);
    }
    r=fixture();r.environment.assign(65,{"INCLUDE",""});
    // Original writer refuses65; exercise old reader independently via mutation.
    auto sixtyfour=fixture();sixtyfour.environment.assign(64,{"INCLUDE","x"});
    const auto excessive=replace_once(serialized(sixtyfour),"environment 64\n","environment 65\n");
    need(!check(excessive).record,"65 entries accepted");
    for (const auto token : {"-1","-0","+1","01"," 1","1 ","2147483647","2147483648","-2147483648","-2147483649","1x","0x1","1,000","","999999999999999999999999999"})
        for (const auto& loc : {classic,grouping,numeric}) check(replace_once(base,"preference 1\n",std::string("preference ")+token+"\n"),loc);
    for (const auto token : {"-1","-0","+9","09","0","64","65","18446744073709551615","18446744073709551616","-18446744073709551615","9x","0x9","9,000"})
        for (const auto& loc : {classic,grouping}) check(replace_once(base,"environment 9\n",std::string("environment ")+token+"\n"),loc);
    for (const auto suffix : {"", " ","\t\r\n\v\f", "trailing", "\""}) check(base+suffix);
    check(base.substr(0,base.size()-1),classic,1); // Missing final LF remains accepted.
    check(replace_once(base,"target \"x64\"","target x64"),classic,1);
    check(replace_once(base,"target \"x64\"","target \"x\\q64\""),classic,1);
    for (const auto blank : {" ","\t","\n","\v","\r","\f","@"})
        for (const auto& loc : {classic,whitespace}) check(replace_once(base,"target \"",std::string("target")+blank+"\""),loc);
    check(replace_once(base,"MQB_TOOLCHAIN_CACHE_V9\n","MQB_TOOLCHAIN_CACHE_V9\r\n"));
    check(replace_once(base,"env_name \"INCLUDE\"","env_name \"PATH\""));
    check(replace_once(base,"env_value ","env_name "));
    check(replace_once(base,"host \"x64\"\n", ""));
    check(replace_once(base,"target \"x64\"\n", "host \"x64\"\n"));
    for (auto value : {"", "/", "C:/", "C:/a/../b/", "C:/a//b/.", "\\\\server\\share\\a", "CON", ".."}) {
        r=fixture();r.vc_tools_root=old::detail::path_from_utf8(value);check(serialized(r));
    }
    // Invalid UTF-8 uses each platform's original path conversion/exception policy.
    check(replace_once(base,"C:/Synthetic/VC/Tools/14.50/",std::string(1,static_cast<char>(0xff))));
    r=fixture();r.effective_path.clear();need(!check(serialized(r)).record,"Empty effective PATH accepted");
    for (std::size_t n : {old::max_cache_string-1,old::max_cache_string}) {
        r=fixture();r.environment[0].value.assign(n,'x');check(serialized(r),classic,0);
    }
    auto short_record=fixture();short_record.environment.clear();
    auto s=serialized(short_record);
    check(replace_once(s,"target \"x64\"","target \""+std::string(old::max_cache_string+1,'x')+"\""));
    const auto limit=static_cast<std::size_t>(old::max_cache_size);
    check(s+std::string(limit-s.size(),' ')); // total exactly1MiB, old whitespace grammar
    check(s+std::string(limit+1-s.size(),' '));
    for (std::size_t n=0;n<base.size();++n) check(std::string_view(base).substr(0,n));
    // Fixed, bounded mutation corpus: no adaptive expansion or performance clock.
    std::uint32_t state=0x935a219du;
    const auto next=[&]() { state^=state<<13;state^=state>>17;state^=state<<5;return state; };
    for (unsigned int i=0;i<4096;++i) {
        auto value=base;const auto at=next()%value.size();const char byte=static_cast<char>(next()&255);
        switch (i%3) { case 0:value[at]=byte;break;case 1:value.insert(value.begin()+at,byte);break;default:value.erase(at,1); }
        check(value); if (i<128) check(value,whitespace);
    }
    for (const auto& loc : {whitespace,grouping,numeric}) {check(base,loc,1);check(serialized(fixture(),loc),loc,1);}
    check_file(work/"canonical.cache",base,classic,0);
    check_file(work/"unquoted.cache",replace_once(base,"target \"x64\"","target x64"),classic,1);
    check_file(work/"grouping.cache",replace_once(base,"preference 1\n","preference 1,000\n"),grouping,2);
    check_file(work/"space.cache",replace_once(base,"target \"","target@\""),whitespace,2);
    check_file(work/"numeric.cache",base,numeric,2);
    check_file(work/"codecvt.cache",replace_once(base,"target \"","target@\""),codecvt,2);
    check_file(work/"oversize.cache",s+std::string(limit+1-s.size(),' '),classic,3);
    transport(base);
}
int main(int argc,char** argv) {
    try {
        need(argc==2,"Supply one new synthetic working directory");
        fs::path work=argv[1];need(!fs::exists(work),"Refuse an existing work directory");fs::create_directories(work);
        run(work);
        std::cout << "{\"schema\":1,\"comparisons\":" << totals.cases << ",\"accepted\":" << totals.accepted
          << ",\"fast\":" << totals.fast << ",\"fallback\":" << totals.fallback << ",\"transport_refused\":" << totals.refused
          << ",\"matched_exceptions\":" << totals.exceptions << ",\"file_cases\":" << totals.file_cases
          << ",\"transport_controls\":" << totals.transport_controls
          << ",\"mismatches\":0,\"synthetic_only\":true,\"new_study_calls\":0,\"new_etw_sessions\":0,\"performance_verified\":false,\"clears_hold\":false}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Probe failed: " << error.what() << "\n";return 1;
    }
}
