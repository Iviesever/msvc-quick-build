#include "mqb/orchestration/ArtifactGenerationArchive.hpp"
#include "ArtifactGenerationArchiveInternal.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <utility>

namespace mqb::orchestration {
namespace {
using Code = ArtifactGenerationArchiveErrorCode;
using Error = ArtifactGenerationArchiveError;
using Limits = ArtifactGenerationArchiveLimits;
namespace fs = std::filesystem;
constexpr std::string_view magic = "MQBGARCH";
constexpr std::uint32_t version = 1;
constexpr std::uint8_t ordinary_claims = 1;

struct Rejected { Error error; };
struct Budget {
    std::size_t text{Limits::text_bytes};
    std::size_t items{Limits::items};
    std::size_t fields{Limits::fields};
    std::size_t stages{ArtifactGenerationLimits::stages};
    std::size_t paths{ArtifactGenerationLimits::paths};
};

// Text validation only, never a second JSON parser. Binary framing is not UTF-8.
bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first == 0) return false;
        if (first < 0x80) continue;
        unsigned remaining{};
        std::uint32_t cp{}, minimum{};
        if (first >= 0xc2 && first <= 0xdf) { remaining=1; cp=first & 0x1f; minimum=0x80; }
        else if (first >= 0xe0 && first <= 0xef) { remaining=2; cp=first & 0x0f; minimum=0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { remaining=3; cp=first & 0x07; minimum=0x10000; }
        else return false;
        if (remaining > text.size() - i) return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (next & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return false;
    }
    return true;
}

// Wire identifiers are explicit positions in these v1 tables, starting at 1.
// They do not depend on compiler enum ordinals or the in-memory recipe framing.
template<class E> constexpr auto enum_values() {
    if constexpr (std::is_same_v<E, BuildConfiguration>) return std::array{E::debug,E::release};
    else if constexpr (std::is_same_v<E, Architecture>) return std::array{E::x86,E::x64};
    else if constexpr (std::is_same_v<E, CppStandard>) return std::array{E::cpp14,E::cpp17,E::cpp20,E::cpp23,E::latest};
    else if constexpr (std::is_same_v<E, RuntimeLibrary>) return std::array{E::md,E::mdd,E::mt,E::mtd};
    else if constexpr (std::is_same_v<E, TargetKind>) return std::array{E::executable,E::dynamic_library}; // LINK options only.
    else if constexpr (std::is_same_v<E, LinkSubsystem>) return std::array{E::console,E::windows};
    else if constexpr (std::is_same_v<E, PrecompiledHeaderRole>) return std::array{E::create,E::use};
    else if constexpr (std::is_same_v<E, TranslationUnitKind>) return std::array{E::source,E::module_interface};
    else if constexpr (std::is_same_v<E, HeaderUnitLookupMethod>) return std::array{E::angle,E::quote};
    else if constexpr (std::is_same_v<E, ArtifactKind>) return std::array{E::object,E::module_interface,E::precompiled_header,E::executable,E::dynamic_library,E::static_library};
    else if constexpr (std::is_same_v<E, ArtifactCompletion>) return std::array{E::executed,E::reused};
    else if constexpr (std::is_same_v<E, ArtifactCacheState>) return std::array{E::saved,E::reused,E::save_failed};
    else if constexpr (std::is_same_v<E, CompileCacheEvidenceState>) return std::array{E::reused,E::saved,E::save_failed};
    else if constexpr (std::is_same_v<E, IncrementalCompileWarningCode> ||
                       std::is_same_v<E, IncrementalLinkWarningCode> ||
                       std::is_same_v<E, IncrementalArchiveWarningCode>)
        return std::array{E::cache_load_failed,E::cache_save_failed,E::file_snapshot_failed};
    else if constexpr (std::is_same_v<E, CompileCacheFileErrorCode>)
        return std::array{E::file_open_failed,E::file_read_failed,E::file_write_failed,E::invalid_magic,
                          E::unsupported_version,E::corrupt_data,E::replace_failed};
}
template<class T> struct Optional : std::false_type {};
template<class T> struct Optional<std::optional<T>> : std::true_type { using value_type=T; };
template<class T> struct Vector : std::false_type {};
template<class T, class A> struct Vector<std::vector<T,A>> : std::true_type { using value_type=T; };
template<class T> struct Variant : std::false_type {};
template<class... T> struct Variant<std::variant<T...>> : std::true_type {};
template<class IO, class T> void transfer(IO& io, T& value);

struct Cursor {
    Budget budget;
    std::size_t position{};
    std::optional<std::size_t> record_index;
    [[noreturn]] void fail(Code code, std::string message) const {
        std::optional<RecordedArtifactGenerationError> nested;
        if (record_index) nested=RecordedArtifactGenerationError{message,record_index,{}};
        throw Rejected{{code,std::move(message),position,std::move(nested)}};
    }
    void take(std::size_t& left, std::size_t n) {
        if (n > left) fail(Code::limit_exceeded,"archive resource limit exceeded");
        left -= n;
    }
    void field() { take(budget.fields,1); }
    void count(std::size_t n, std::size_t maximum) {
        if (n > maximum) fail(Code::limit_exceeded,"archive vector count exceeds its limit");
        take(budget.items,n);
    }
    void text(std::string_view value, Code invalid) {
        if (value.size() > Limits::string_bytes) fail(Code::limit_exceeded,"archive string exceeds its limit");
        take(budget.text,value.size());
        if (!valid_utf8(value)) fail(invalid,"invalid UTF-8 or NUL in archive text");
    }
};

template<bool Emit> struct Writer : Cursor {
    std::string bytes;
    void raw(std::string_view value) {
        if (value.size() > Limits::document_bytes-position)
            fail(Code::limit_exceeded,"archive document exceeds its limit");
        position += value.size();
        if constexpr (Emit) bytes.append(value);
    }
    void number(std::uint64_t value, unsigned width) {
        field();
        std::array<char,8> data{};
        for (unsigned i=0;i<width;++i) data[i]=static_cast<char>((value>>(8*i)) & 0xff);
        raw({data.data(),width});
    }
    template<class... T> void operator()(const T&... value) { (transfer(*this,value),...); }
    void boolean(bool value) { number(value ? 1 : 0,1); }
    void string(std::string_view value) {
        text(value,Code::invalid_evidence); number(value.size(),4); raw(value);
    }
    void path(const fs::path& value) {
        take(budget.paths,1);
        // Bound native code units before the temporary UTF-8 conversion;
        // string() below then checks exact UTF-8 byte limits. No locale,
        // canonical/current-path lookup, separator rewriting or filesystem IO.
        if (value.native().size() > Limits::string_bytes || value.native().size() > budget.text)
            fail(Code::limit_exceeded,"archive path exceeds its limit");
        const auto utf8=value.u8string();
        string({reinterpret_cast<const char*>(utf8.data()),utf8.size()});
    }
    template<class E> void enumeration(E value) {
        constexpr auto values=enum_values<E>();
        for (std::size_t i=0;i<values.size();++i)
            if (values[i]==value) { number(i+1,1); return; }
        fail(Code::invalid_evidence,"unknown enum in archive evidence");
    }
    template<class T> void optional(const std::optional<T>& value) {
        boolean(value.has_value()); if (value) (*this)(*value);
    }
    template<class T> void sequence(const T& values, std::size_t maximum=Limits::items) {
        count(values.size(),maximum); number(values.size(),4);
        for (const auto& value:values) (*this)(value);
    }
    template<class T> void sources(const T& values) {
        if (values.size() >= ArtifactGenerationLimits::stages)
            fail(Code::limit_exceeded,"archive source count exceeds its limit");
        take(budget.stages,values.size()+1);
        sequence(values,ArtifactGenerationLimits::stages-1);
    }
    template<class... T> void variant(const std::variant<T...>& value) {
        if (value.valueless_by_exception()) fail(Code::invalid_evidence,"valueless target claim");
        number(value.index()+1,1);
        std::visit([&](const auto& target) { (*this)(target); },value);
    }
    void signature(const BuildSignature& value) { number(value.digest().high,8); number(value.digest().low,8); }
    void offset(std::size_t value) { number(static_cast<std::uint64_t>(value),8); }
    template<class T> void absent(const std::optional<T>& value) {
        // Measurement never walks an unsupported module graph. Let the shared
        // strict projector report its original record/source issue before any
        // owning copy. Actual encoding is reachable only after that admission.
        if constexpr (Emit) {
            if (value) fail(Code::invalid_evidence,"ordinary archive cannot carry module scan evidence");
        }
        boolean(value.has_value());
    }
    void snapshot(const LinkFactSnapshot& value) {
        // The established codec validates and bounds its own temporary string.
        // The snapshot remains unverified history; correspondence is modelled
        // by the shared generation rules, not silently repaired here.
        const auto encoded=encode_link_fact_snapshot(value);
        if (!encoded) fail(Code::invalid_evidence,"invalid link snapshot: "+encoded.error().message);
        string(*encoded);
    }
};

template<bool Own> struct Reader : Cursor {
    std::string_view bytes;
    explicit Reader(std::string_view input):bytes(input) {
        if (bytes.size() > Limits::document_bytes) fail(Code::limit_exceeded,"archive document exceeds its limit");
    }
    std::string_view raw(std::size_t size) {
        if (size > bytes.size()-position) fail(Code::invalid_document,"truncated archive");
        const auto value=bytes.substr(position,size); position+=size; return value;
    }
    std::uint64_t number(unsigned width) {
        field(); const auto data=raw(width); std::uint64_t value{};
        for (unsigned i=0;i<width;++i)
            value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i])) << (8*i);
        return value;
    }
    template<class... T> void operator()(T&... value) { (transfer(*this,value),...); }
    bool flag() {
        const auto value=number(1);
        if (value>1) fail(Code::invalid_document,"invalid optional or boolean tag");
        return value!=0;
    }
    void boolean(bool& value) { value=flag(); }
    std::string_view text_view() {
        const auto size=number(4);
        if (size>Limits::string_bytes || size>budget.text) fail(Code::limit_exceeded,"archive string exceeds its limit");
        const auto value=raw(static_cast<std::size_t>(size));
        text(value,Code::invalid_document); return value;
    }
    void string(std::string& value) {
        const auto selected=text_view();
        if constexpr (Own) value.assign(selected);
    }
    void path(fs::path& value) {
        take(budget.paths,1); const auto selected=text_view();
        if constexpr (Own) {
            std::u8string utf8; utf8.reserve(selected.size());
            for (const unsigned char ch:selected) utf8.push_back(static_cast<char8_t>(ch));
            value=fs::path{utf8};
        }
    }
    template<class E> void enumeration(E& value) {
        constexpr auto values=enum_values<E>(); const auto selected=number(1);
        if (selected==0 || selected>values.size()) fail(Code::invalid_document,"unknown archive enum");
        value=values[static_cast<std::size_t>(selected)-1];
    }
    template<class T> void optional(std::optional<T>& value) {
        if (!flag()) { if constexpr (Own) value.reset(); return; }
        if constexpr (Own) { value.emplace(); (*this)(*value); }
        else { T scratch{}; (*this)(scratch); }
    }
    template<class T> void sequence(std::vector<T>& values, std::size_t maximum=Limits::items) {
        const auto size=static_cast<std::size_t>(number(4)); count(size,maximum);
        if constexpr (Own) {
            values.reserve(size);
            for (std::size_t i=0;i<size;++i) { T value{}; (*this)(value); values.push_back(std::move(value)); }
        } else {
            for (std::size_t i=0;i<size;++i) { T scratch{}; (*this)(scratch); }
        }
    }
    template<class T> void sources(std::vector<T>& values) {
        const auto size=static_cast<std::size_t>(number(4)); count(size,ArtifactGenerationLimits::stages-1);
        take(budget.stages,size+1);
        if constexpr (Own) {
            values.reserve(size);
            for (std::size_t i=0;i<size;++i) { T value{}; (*this)(value); values.push_back(std::move(value)); }
        } else {
            for (std::size_t i=0;i<size;++i) { T scratch{}; (*this)(scratch); }
        }
    }
    void variant(std::variant<ArchivedTargetClaim,ArchivedStaticTargetClaim>& value) {
        const auto tag=number(1);
        if (tag==1) {
            if constexpr (Own) { value.template emplace<0>(); (*this)(std::get<0>(value)); }
            else { ArchivedTargetClaim scratch; (*this)(scratch); }
        } else if (tag==2) {
            if constexpr (Own) { value.template emplace<1>(); (*this)(std::get<1>(value)); }
            else { ArchivedStaticTargetClaim scratch; (*this)(scratch); }
        } else fail(Code::invalid_document,"unknown ordinary target claim type");
    }
    void signature(BuildSignature& value) {
        const auto high=number(8); const auto low=number(8);
        value=BuildSignature::from_digest({high,low}); // Copy an opaque digest, never recompute a recipe.
    }
    void offset(std::size_t& value) {
        const auto selected=number(8);
        if (selected>(std::numeric_limits<std::size_t>::max)()) fail(Code::invalid_document,"diagnostic offset overflow");
        value=static_cast<std::size_t>(selected);
    }
    template<class T> void absent(std::optional<T>& value) {
        if (flag()) fail(Code::invalid_document,"ordinary archive cannot carry module scan evidence");
        if constexpr (Own) value.reset();
    }
    void snapshot(LinkFactSnapshot& value) {
        const auto selected=text_view();
        if (selected.size()>LinkFactSnapshotLimits::document_bytes) fail(Code::limit_exceeded,"snapshot exceeds its limit");
        if constexpr (Own) {
            const auto decoded=decode_link_fact_snapshot(selected);
            if (!decoded) fail(Code::invalid_document,"invalid link snapshot: "+decoded.error().message);
            value=*decoded;
        }
    }
    void finish() {
        if (position!=bytes.size()) fail(Code::invalid_document,"trailing archive data");
    }
};

// One v1 schema traversal for measure/write/scan/read. Only explicit selected
// fields appear below. There is no serialization of object layouts or recipe
// strings, and no independently reconstructed relaxed matching algorithm.
template<class IO, class T> void transfer(IO& io, T& value) {
    using V=std::remove_cv_t<T>;
    if constexpr (std::is_same_v<V,bool>) io.boolean(value);
    else if constexpr (std::is_enum_v<V>) io.enumeration(value);
    else if constexpr (std::is_same_v<V,std::string>) io.string(value);
    else if constexpr (std::is_same_v<V,fs::path>) io.path(value);
    else if constexpr (Optional<V>::value) io.optional(value);
    else if constexpr (Vector<V>::value) io.sequence(value);
    else if constexpr (Variant<V>::value) io.variant(value);
    else if constexpr (std::is_same_v<V,BuildSignature>) io.signature(value);
    else if constexpr (std::is_same_v<V,ArtifactTargetKey>) io(value.project,value.target);
    else if constexpr (std::is_same_v<V,ArtifactGenerationKey>) io(value.target,value.generation);
    else if constexpr (std::is_same_v<V,ArtifactGenerationLabel>) io(value.target,value.generation);
    else if constexpr (std::is_same_v<V,ToolchainIdentity>) io(value.compiler,value.version,value.binary_stamp);
    else if constexpr (std::is_same_v<V,LinkerIdentity>) io(value.linker,value.version,value.binary_stamp);
    else if constexpr (std::is_same_v<V,LibrarianIdentity>) io(value.librarian,value.version,value.binary_stamp);
    else if constexpr (std::is_same_v<V,PrecompiledHeaderBinding>) io(value.header,value.artifact,value.role);
    else if constexpr (std::is_same_v<V,ExternalModuleProvider>) io(value.logical_name,value.interface_file);
    else if constexpr (std::is_same_v<V,CompilerOptions>) {
        io(value.configuration,value.architecture,value.standard,value.runtime_library,
           value.link_time_code_generation,value.defines,value.include_directories,
           value.additional_arguments,value.precompiled_header,value.external_module_providers);
    } else if constexpr (std::is_same_v<V,LinkOptions>) {
        io(value.configuration,value.architecture,value.target_kind,value.subsystem,value.link_time_code_generation,
           value.address_sanitizer_runtime_library,value.address_sanitizer_vcasan_runtime_library,
           value.fuzzer_runtime_library,value.msvc_openmp_runtime,value.library_directories,value.libraries,value.additional_arguments);
    } else if constexpr (std::is_same_v<V,HeaderUnitIdentity>) io(value.header_name,value.lookup_method);
    else if constexpr (std::is_same_v<V,ModuleReference>) io(value.logical_name,value.interface_file);
    else if constexpr (std::is_same_v<V,HeaderUnitReference>) io(value.header_name,value.lookup_method,value.interface_file);
    else if constexpr (std::is_same_v<V,Artifact>) io(value.path,value.kind);
    else if constexpr (std::is_same_v<V,TranslationUnit>) {
        io(value.source,value.kind,value.header_unit,value.dependencies,value.module_references,value.header_unit_references,value.outputs);
    } else if constexpr (std::is_same_v<V,IncrementalCompileRequest>) {
        io(value.unit,value.options,value.cache_file,value.source_dependencies_file,
           value.module_scan_output,value.working_directory,value.force_rebuild);
    } else if constexpr (std::is_same_v<V,CompileCacheEntry>) {
        io(value.source,value.kind,value.toolchain,value.signature,value.outputs,value.dependencies,value.include_search_roots);
        io.absent(value.module_scan);
    } else if constexpr (std::is_same_v<V,CompileCacheFileError>) {
        io(value.code,value.file); io.offset(value.offset); io(value.message);
    } else if constexpr (std::is_same_v<V,IncrementalCompileWarning> ||
                         std::is_same_v<V,IncrementalLinkWarning> || std::is_same_v<V,IncrementalArchiveWarning>) {
        io(value.code,value.path,value.message);
    } else if constexpr (std::is_same_v<V,ArchivedCompileEvidence> || std::is_same_v<V,CompileCacheEvidence>) {
        io(value.request,value.inspection_toolchain.identity,value.cache_entry,value.state,value.save_error);
    } else if constexpr (std::is_same_v<V,ArchivedSourceCompletion> || std::is_same_v<V,TargetCompileResult>) {
        io(value.source,value.result.compiled,value.result.warnings);
    } else if constexpr (std::is_same_v<V,SourceArtifactAssociation>) {
        io(value.source,value.object,value.dependencies,value.compile_cache,value.completion,value.has_warnings);
    } else if constexpr (std::is_same_v<V,LinkCacheEntry>) {
        io(value.linker,value.signature,value.objects,value.output,value.libraries,value.file_inputs,value.side_outputs);
    } else if constexpr (std::is_same_v<V,ArchiveCacheEntry>) io(value.librarian,value.signature,value.objects,value.output);
    else if constexpr (std::is_same_v<V,LinkArtifactRecord>) {
        io(value.completion,value.cache_state,value.association,value.options,value.cache_file,value.working_directory);
    } else if constexpr (std::is_same_v<V,ArchiveArtifactRecord>) {
        io(value.completion,value.cache_state,value.association,value.architecture,
           value.link_time_code_generation,value.additional_arguments,value.cache_file,value.working_directory);
    } else if constexpr (std::is_same_v<V,TargetArtifactRecord> || std::is_same_v<V,StaticTargetArtifactRecord>) {
        io(value.caller_label,value.compiler_options); io.sources(value.sources); io(value.additional_object_inputs);
        if constexpr (std::is_same_v<V,TargetArtifactRecord>) io(value.link); else io(value.archive);
    } else if constexpr (std::is_same_v<V,ArchivedTargetClaim> || std::is_same_v<V,RecordedTargetResult>) {
        io(value.record); io.sequence(value.cache_evidence.compiles,ArtifactGenerationLimits::stages-1);
        io.sequence(value.result.compiles,ArtifactGenerationLimits::stages-1);
        io(value.result.any_compiled,value.result.link.linked,value.result.link.warnings);
    } else if constexpr (std::is_same_v<V,ArchivedStaticTargetClaim> || std::is_same_v<V,RecordedStaticTargetResult>) {
        io(value.record); io.sequence(value.cache_evidence.compiles,ArtifactGenerationLimits::stages-1);
        io.sequence(value.result.compiles,ArtifactGenerationLimits::stages-1);
        io(value.result.any_compiled,value.result.archive.archived,value.result.archive.warnings);
    } else if constexpr (std::is_same_v<V,ArchivedArtifactGenerationInput>) {
        io(value.source_id,value.target,value.generation,value.record,value.snapshot);
    } else if constexpr (std::is_same_v<V,LinkFactSnapshot>) io.snapshot(value);
    else static_assert(sizeof(V)==0,"no v1 archive field definition for this type");
}

template<bool Emit> void write_header(Writer<Emit>& io) {
    io.raw(magic); io.number(version,4); io.number(ordinary_claims,1);
    io.number(0,1); // All four authority flags are false; no writable authority field.
}
template<bool Own> void read_header(Reader<Own>& io) {
    if (io.raw(magic.size())!=magic) io.fail(Code::invalid_document,"invalid archive magic");
    if (io.number(4)!=version) io.fail(Code::unsupported_version,"unsupported archive version");
    if (io.number(1)!=ordinary_claims) io.fail(Code::invalid_document,"unknown archive document type");
    if (io.number(1)!=0) io.fail(Code::invalid_document,"archive cannot grant authority");
}
template<bool Emit> void write_document(Writer<Emit>& io, const ArtifactGenerationArchive& value) {
    write_header(io); io(value.lexical_root);
    io.count(value.records.size(),ArtifactGenerationLimits::records); io.number(value.records.size(),4);
    for (std::size_t i=0;i<value.records.size();++i) { io.record_index=i; io(value.records[i]); }
    io.record_index.reset(); io.sequence(value.retain,ArtifactGenerationLimits::records);
}
template<bool Own> void read_document(Reader<Own>& io, ArtifactGenerationArchive& value) {
    read_header(io); io(value.lexical_root);
    const auto size=static_cast<std::size_t>(io.number(4)); io.count(size,ArtifactGenerationLimits::records);
    if constexpr (Own) value.records.reserve(size);
    for (std::size_t i=0;i<size;++i) {
        io.record_index=i; ArchivedArtifactGenerationInput record; io(record);
        if constexpr (Own) value.records.push_back(std::move(record));
    }
    io.record_index.reset(); io.sequence(value.retain,ArtifactGenerationLimits::records); io.finish();
}
Error semantic_error(const RecordedArtifactGenerationError& value) {
    return {Code::invalid_evidence,value.message,0,value};
}
} // namespace

namespace detail {
std::expected<void, Error>
preflight_recorded_generation_archive(std::span<const RecordedArtifactGenerationInput> records,
                                      std::span<const ArtifactGenerationKey> retain,
                                      const fs::path& lexical_root) {
    try {
        Writer<false> measure; write_header(measure); measure(lexical_root);
        measure.count(records.size(),ArtifactGenerationLimits::records); measure.number(records.size(),4);
        for (std::size_t i=0;i<records.size();++i) {
            measure.record_index=i; const auto& input=records[i];
            measure(input.source_id,input.target,input.generation);
            if (input.record.valueless_by_exception()) measure.fail(Code::invalid_evidence,"valueless live target");
            measure.number(input.record.index()+1,1);
            std::visit([&](const auto& borrowed) { measure(borrowed.get()); },input.record);
            measure(input.snapshot);
        }
        measure.record_index.reset(); measure.sequence(retain,ArtifactGenerationLimits::records);
        return {};
    } catch (const Rejected& rejected) { return std::unexpected(rejected.error); }
}
} // namespace detail

std::expected<std::string, Error>
encode_artifact_generation_archive(const ArtifactGenerationArchive& archive, const StoragePathKey& key) {
    try {
        Writer<false> measure; write_document(measure,archive);
        const auto model=model_archived_artifact_generations(archive,key);
        if (!model) return std::unexpected(semantic_error(model.error()));
        Writer<true> output; output.bytes.reserve(measure.position); write_document(output,archive);
        return std::move(output.bytes);
    } catch (const Rejected& rejected) { return std::unexpected(rejected.error); }
}

std::expected<ArtifactGenerationArchive, Error>
decode_artifact_generation_archive(std::string_view bytes, const StoragePathKey& key) {
    try {
        // The first pass never assigns document strings/paths or grows vectors.
        // All optional/enum tags, UTF-8, cumulative counts and the final boundary
        // are checked before the owning pass or the lexical callback can run.
        Reader<false> scan(bytes); ArtifactGenerationArchive scratch; read_document(scan,scratch);
        Reader<true> input(bytes); ArtifactGenerationArchive archive; read_document(input,archive);
        const auto model=model_archived_artifact_generations(archive,key);
        if (!model) return std::unexpected(semantic_error(model.error()));
        return archive;
    } catch (const Rejected& rejected) { return std::unexpected(rejected.error); }
}
} // namespace mqb::orchestration
