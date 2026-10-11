// One bounded child-image loader-snaps observation, never IPC qualification.
// The included 008 translation unit is separately hash-bound by the runner.
// Its client implementation is reused unchanged; its server is never invoked.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#define wmain producer_delegation_008_wmain
#include "producer_delegation_probe.cpp"
#undef wmain
#include <limits>

namespace {
constexpr unsigned startup_event_limit = 1024;
constexpr unsigned startup_main_event_limit = 960; // Reserve 64 for exit draining.
constexpr unsigned startup_wait_call_limit = 2048;
constexpr unsigned startup_main_wait_call_limit = 1792;
constexpr unsigned startup_cleanup_wait_call_limit = 256;
constexpr DWORD startup_wait_limit_ms = 50;
constexpr ULONGLONG startup_main_limit_ms = 20000;
constexpr ULONGLONG startup_cleanup_limit_ms = 5000;
constexpr DWORD startup_string_limit = 4096;
constexpr DWORD startup_strings_limit = 1024 * 1024;
constexpr DWORD startup_path_limit = 4096;
constexpr std::size_t startup_path_text_limit = 1024 * 1024;
constexpr std::uint64_t startup_image_limit = 8 * 1024 * 1024;

struct StartupDiagnostic {
    unsigned events_observed = 0;
    unsigned event_continuations = 0;
    unsigned continuation_successes = 0;
    unsigned wait_calls = 0;
    unsigned main_wait_calls = 0;
    unsigned cleanup_wait_calls = 0;
    std::size_t path_wchars_emitted = 0;
    DWORD debug_string_bytes_requested = 0;
    DWORD debug_string_bytes_read = 0;
    bool child_image_pin_bound = false;
    bool child_image_event_bound = false;
    bool child_image_final_bound = false;
    bool startup_observed = false;
    bool startup_frame_valid = false;
    bool startup_read_attempted = false;
    bool report_broken = false;
    bool report_poll_failed = false;
    bool create_event_observed = false;
    bool exit_event_observed = false;
    bool exit_event_continued = false;
    bool actual_child_exit_observed = false;
    bool cleanup_requested = false;
    bool initial_breakpoint_continued = false;
    bool continuation_failed = false;
    bool structural_observation_failed = false;
    bool bound_hit = false;
    DWORD debug_exit_code = 0;
    DWORD client_exit_code = 0;
};

bool reserve_startup_path_text(StartupDiagnostic& diagnostic, std::size_t count, const char* label) {
    const auto remaining = startup_path_text_limit - diagnostic.path_wchars_emitted;
    if (count > remaining) {
        diagnostic.bound_hit = true;
        Record("path_text_bound").text("label", label).number("requested_wchars", count)
            .number("remaining_wchars", remaining).number("aggregate_wchar_limit", startup_path_text_limit)
            .boolean("optional_text_omitted", true);
        return false;
    }
    diagnostic.path_wchars_emitted += count;
    return true;
}

struct StartupImageIdentity {
    std::uint64_t volume = 0;
    std::array<std::byte, 16> file_id{};
    std::uint64_t bytes = 0;
};

std::string startup_file_id_hex(const StartupImageIdentity& identity) {
    return bytes_hex(std::vector<std::byte>(identity.file_id.begin(), identity.file_id.end()));
}

bool same_startup_image(const StartupImageIdentity& left, const StartupImageIdentity& right) noexcept {
    return left.volume == right.volume && left.file_id == right.file_id && left.bytes == right.bytes;
}

std::uint64_t startup_decimal(const wchar_t* argument, std::uint64_t maximum, const char* label) {
    const std::wstring value(argument);
    require(!value.empty() && value.size() <= 20 && (value.size() == 1 || value.front() != L'0'), label);
    std::uint64_t result = 0;
    bool valid_decimal = true;
    for (const auto character : value) {
        if (character < L'0' || character > L'9') { valid_decimal = false; break; }
        const auto digit = static_cast<std::uint64_t>(character - L'0');
        if (digit > maximum || result > (maximum - digit) / 10) { valid_decimal = false; break; }
        result = result * 10 + digit;
    }
    require(valid_decimal, label);
    return result;
}

StartupImageIdentity expected_startup_image(const wchar_t* volume, const wchar_t* file_id, const wchar_t* bytes) {
    StartupImageIdentity result;
    result.volume = startup_decimal(volume, (std::numeric_limits<std::uint64_t>::max)(),
        "canonical bounded expected image volume decimal");
    result.bytes = startup_decimal(bytes, startup_image_limit, "canonical bounded expected image size decimal");
    require(result.bytes > 0, "expected child image size is positive");
    const std::wstring encoded(file_id);
    require(encoded.size() == 32, "expected image file id contains exactly 16 bytes");
    bool valid_hex = true;
    for (std::size_t i = 0; i < result.file_id.size(); ++i) {
        unsigned decoded = 0;
        for (std::size_t j = 0; j < 2; ++j) {
            const auto character = encoded[i * 2 + j];
            unsigned digit = 0;
            if (character >= L'0' && character <= L'9') digit = static_cast<unsigned>(character - L'0');
            else if (character >= L'a' && character <= L'f') digit = static_cast<unsigned>(character - L'a') + 10;
            else { valid_hex = false; }
            decoded = decoded * 16 + digit;
        }
        result.file_id[i] = static_cast<std::byte>(decoded);
    }
    require(valid_hex, "expected image file id is canonical lowercase hexadecimal in storage byte order");
    const auto file_id_hex = startup_file_id_hex(result);
    Record("expected_child_image").text("volume_serial_decimal", std::to_string(result.volume))
        .text("file_id_hex", file_id_hex).number("bytes", result.bytes)
        .boolean("identity_supplied_by_reviewed_runner", true).boolean("native_pe_content_verification", false);
    return result;
}

StartupImageIdentity startup_image_identity(HANDLE file, const char* label) {
    require(valid(file), "usable child image handle for actual identity observation");
    FILE_ID_INFO id{};
    FILE_STANDARD_INFO standard{};
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    checked(GetFileInformationByHandleEx(file, FileIdInfo, &id, sizeof(id)),
        "GetFileInformationByHandleEx(FileIdInfo)", label);
    checked(GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard)),
        "GetFileInformationByHandleEx(FileStandardInfo)", label);
    checked(GetFileInformationByHandleEx(file, FileAttributeTagInfo, &attributes, sizeof(attributes)),
        "GetFileInformationByHandleEx(FileAttributeTagInfo)", label);
    StartupImageIdentity result;
    result.volume = id.VolumeSerialNumber;
    static_assert(sizeof(id.FileId.Identifier) == sizeof(result.file_id), "native FileIdInfo identity size");
    std::memcpy(result.file_id.data(), id.FileId.Identifier, result.file_id.size());
    const auto file_id_hex = startup_file_id_hex(result);
    Record("child_image_identity").text("label", label).text("handle_decimal", handle_decimal(file))
        .text("volume_serial_decimal", std::to_string(result.volume)).text("file_id_hex", file_id_hex)
        .text("end_of_file_decimal", std::to_string(standard.EndOfFile.QuadPart))
        .text("allocation_size_decimal", std::to_string(standard.AllocationSize.QuadPart))
        .number("number_of_links", standard.NumberOfLinks).boolean("delete_pending", standard.DeletePending != FALSE)
        .boolean("directory", standard.Directory != FALSE).number("attributes", attributes.FileAttributes)
        .number("reparse_tag", attributes.ReparseTag).boolean("read_only_observation", true);
    require(standard.EndOfFile.QuadPart > 0 &&
        static_cast<std::uint64_t>(standard.EndOfFile.QuadPart) <= startup_image_limit,
        "actual image size is positive and within fixed image bound");
    require(standard.NumberOfLinks == 1 && !standard.DeletePending && !standard.Directory,
        "actual image has one link and is neither directory nor delete pending");
    require((attributes.FileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0 &&
        attributes.ReparseTag == 0, "actual image is not a directory or reparse object");
    result.bytes = static_cast<std::uint64_t>(standard.EndOfFile.QuadPart);
    return result;
}

struct StartupModule {
    std::uintptr_t base = 0;
    bool active = false;
    std::wstring path;
};

// Only this guard continues a received event. The event's process/thread
// handles are borrowed; Windows closes them after the corresponding exit
// event is continued. LOAD_DLL / CREATE_PROCESS hFile is separately owned.
class StartupEvent {
    const DEBUG_EVENT& event;
    StartupDiagnostic& diagnostic;
    DWORD status = DBG_CONTINUE;
    bool attempted = false;
public:
    StartupEvent(const DEBUG_EVENT& value, StartupDiagnostic& state, DWORD child_pid) noexcept
        : event(value), diagnostic(state) {
        if (event.dwDebugEventCode == EXCEPTION_DEBUG_EVENT) {
            status = DBG_EXCEPTION_NOT_HANDLED;
            const auto& exception = event.u.Exception;
            if (event.dwProcessId == child_pid && exception.dwFirstChance != 0 &&
                exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT &&
                !diagnostic.initial_breakpoint_continued && !diagnostic.startup_observed) {
                diagnostic.initial_breakpoint_continued = true;
                status = DBG_CONTINUE;
            }
        }
    }
    StartupEvent(const StartupEvent&) = delete;
    ~StartupEvent() noexcept { if (!attempted) continue_once(); }
    bool continue_once() noexcept {
        if (attempted) return false;
        attempted = true;
        ++diagnostic.event_continuations;
        const BOOL ok = ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status);
        const DWORD error = ok ? 0 : GetLastError();
        if (ok) {
            ++diagnostic.continuation_successes;
            if (event.dwDebugEventCode == EXIT_PROCESS_DEBUG_EVENT)
                diagnostic.exit_event_continued = diagnostic.exit_event_observed;
        } else {
            ++errors;
            diagnostic.continuation_failed = true;
            diagnostic.structural_observation_failed = true;
        }
        try {
            Record("debug_continue").number("event_index", diagnostic.events_observed)
                .number("pid", event.dwProcessId).number("tid", event.dwThreadId)
                .number("event_code", event.dwDebugEventCode).number("continue_status", status)
                .boolean("ok", ok != FALSE).number("error", error)
                .boolean("exactly_one_call_for_this_event", true);
        } catch (...) { ++errors; }
        return ok != FALSE;
    }
};

void observe_kernel_dacl(HANDLE object, const char* label) {
    DWORD needed = 0;
    const BOOL sized = GetKernelObjectSecurity(object, DACL_SECURITY_INFORMATION, nullptr, 0, &needed);
    const DWORD size_error = sized ? 0 : GetLastError();
    Record("kernel_dacl_size").text("label", label).text("handle_decimal", handle_decimal(object))
        .boolean("ok", sized != FALSE).number("error", size_error).number("required_bytes", needed);
    require(!sized && size_error == ERROR_INSUFFICIENT_BUFFER && needed > 0 && needed <= 65536,
        "bounded actual process/thread security descriptor");
    std::vector<std::byte> descriptor(needed);
    DWORD returned = 0;
    checked(GetKernelObjectSecurity(object, DACL_SECURITY_INFORMATION, descriptor.data(), needed, &returned),
        "GetKernelObjectSecurity", label);
    require(returned > 0 && returned <= needed, "actual kernel descriptor complete read");
    descriptor.resize(returned);
    require(IsValidSecurityDescriptor(descriptor.data()) != FALSE, "valid actual kernel security descriptor");
    BOOL present = FALSE, defaulted = FALSE;
    PACL dacl = nullptr;
    checked(GetSecurityDescriptorDacl(descriptor.data(), &present, &dacl, &defaulted),
        "GetSecurityDescriptorDacl", label);
    require(present && dacl && within(descriptor, dacl, sizeof(ACL)), "actual kernel DACL header observed");
    require(dacl->AclSize >= sizeof(ACL) && within(descriptor, dacl, dacl->AclSize) && IsValidAcl(dacl),
        "complete valid actual kernel DACL");
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision = 0;
    checked(GetSecurityDescriptorControl(descriptor.data(), &control, &revision),
        "GetSecurityDescriptorControl", label);
    const auto* first = reinterpret_cast<const std::byte*>(dacl);
    const std::vector<std::byte> acl_bytes(first, first + dacl->AclSize);
    const auto hex = bytes_hex(acl_bytes);
    const auto sddl = rendered_dacl(descriptor.data());
    Record("actual_kernel_dacl").text("label", label).text("handle_decimal", handle_decimal(object))
        .number("security_descriptor_bytes", returned).number("control", control).number("revision", revision)
        .boolean("dacl_present", present != FALSE).boolean("dacl_defaulted", defaulted != FALSE)
        .wide("dacl_sddl", sddl).text("acl_hex", hex).boolean("read_only_observation", true);
}

class StartupSession {
    Handle& process;
    Handle& job;
    HANDLE report;
    HANDLE expected_input;
    HANDLE expected_output;
    DWORD child_pid;
    ULONGLONG started;
    StartupDiagnostic& diagnostic;
    const StartupImageIdentity expected_image;
    bool armed;
    bool finished = false;
    std::vector<StartupModule> modules;

    std::wstring image_path(HANDLE file, const char* label) {
        if (!valid(file)) {
            Record("debug_image_file").text("label", label).text("handle_decimal", handle_decimal(file))
                .boolean("path_observed", false).text("reason", "event supplied no usable hFile");
            ++errors; // Optional observation unavailable; still continue later events.
            return {};
        }
        std::array<wchar_t, startup_path_limit> buffer{};
        const DWORD length = GetFinalPathNameByHandleW(file, buffer.data(), static_cast<DWORD>(buffer.size()),
            FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
        const DWORD error = length ? 0 : GetLastError();
        const bool complete = length > 0 && length < buffer.size();
        std::wstring path;
        if (complete) path.assign(buffer.data(), length);
        const bool emitted = complete && reserve_startup_path_text(diagnostic, path.size(), label);
        Record record("debug_image_file");
        record.text("label", label).text("handle_decimal", handle_decimal(file))
            .number("returned_length_raw", length).number("buffer_wchars", buffer.size())
            .number("error", error).boolean("path_observed", complete)
            .boolean("path_bound_exceeded", length >= buffer.size()).boolean("path_text_emitted", emitted);
        if (emitted) record.wide("path", path);
        if (!complete) ++errors;
        return path;
    }

    void add_module(const void* base, std::wstring path) {
        require(modules.size() < startup_event_limit, "bounded observed module list");
        modules.push_back(StartupModule{reinterpret_cast<std::uintptr_t>(base), true, std::move(path)});
    }

    void exception_record(const EXCEPTION_DEBUG_INFO& exception) {
        const auto& raw = exception.ExceptionRecord;
        std::ostringstream parameters;
        parameters << '[';
        const DWORD bounded_count = std::min<DWORD>(raw.NumberParameters, EXCEPTION_MAXIMUM_PARAMETERS);
        for (DWORD i = 0; i < bounded_count; ++i) {
            if (i) parameters << ',';
            parameters << escaped(std::to_string(raw.ExceptionInformation[i]));
        }
        parameters << ']';
        Record("debug_exception").number("code", raw.ExceptionCode).number("flags", raw.ExceptionFlags)
            .number("first_chance_raw", exception.dwFirstChance)
            .text("address_decimal", std::to_string(reinterpret_cast<std::uintptr_t>(raw.ExceptionAddress)))
            .number("parameter_count_raw", raw.NumberParameters).raw("parameters_decimal", parameters.str())
            .boolean("parameter_count_within_bound", raw.NumberParameters <= EXCEPTION_MAXIMUM_PARAMETERS);
        if (raw.NumberParameters > EXCEPTION_MAXIMUM_PARAMETERS) {
            ++errors; diagnostic.structural_observation_failed = true;
        }
        MEMORY_BASIC_INFORMATION information{};
        const SIZE_T returned = VirtualQueryEx(process.get(), raw.ExceptionAddress, &information, sizeof(information));
        const DWORD error = returned ? 0 : GetLastError();
        const bool complete = returned == sizeof(information);
        const bool allocation_defined = complete && information.State != MEM_FREE;
        const bool image_allocation = allocation_defined && information.State == MEM_COMMIT && information.Type == MEM_IMAGE;
        const auto base = reinterpret_cast<std::uintptr_t>(information.AllocationBase);
        const auto address = reinterpret_cast<std::uintptr_t>(raw.ExceptionAddress);
        const StartupModule* match = nullptr;
        if (image_allocation && address >= base) {
            for (const auto& module : modules) if (module.active && module.base == base) match = &module;
        }
        // Reserve before constructing Record: a rejected reservation emits
        // its own row and must not reorder the consecutive JSONL sequence.
        const bool module_path_emitted = match &&
            reserve_startup_path_text(diagnostic, match->path.size(), "exception-observed-module");
        Record record("exception_allocation");
        record.number("query_bytes_returned", returned).number("error", error).boolean("complete", complete)
            .text("address_decimal", std::to_string(address)).boolean("allocation_fields_defined", allocation_defined)
            .boolean("observed_image_module_matched", match != nullptr)
            .boolean("module_match_is_initialization_cause", false);
        if (complete) record.number("state", information.State);
        if (allocation_defined) {
            record.number("type", information.Type).text("allocation_base_decimal", std::to_string(base));
            if (information.State == MEM_COMMIT) record.number("protect", information.Protect);
        }
        if (match) {
            record.text("rva_decimal", std::to_string(address - base))
                .boolean("observed_module_path_emitted", module_path_emitted);
            if (module_path_emitted) record.wide("observed_module_path", match->path);
        }
        if (!complete) ++errors;
    }

    void debug_string(const OUTPUT_DEBUG_STRING_INFO& string) {
        // nDebugStringLength is the low 16 bits of the BYTE count, including
        // for Unicode events. It is never multiplied by sizeof(wchar_t).
        const DWORD reported = string.nDebugStringLength;
        const DWORD remaining = startup_strings_limit - diagnostic.debug_string_bytes_requested;
        const DWORD requested = std::min<DWORD>(reported, std::min<DWORD>(startup_string_limit, remaining));
        diagnostic.debug_string_bytes_requested += requested;
        std::vector<std::byte> bytes(requested);
        SIZE_T read = 0;
        BOOL ok = TRUE;
        DWORD error = 0;
        if (requested != 0) {
            ok = ReadProcessMemory(process.get(), string.lpDebugStringData, bytes.data(), requested, &read);
            error = ok ? 0 : GetLastError();
        }
        const bool read_bound_valid = read <= requested;
        if (!read_bound_valid) { ++errors; read = 0; }
        bytes.resize(read);
        diagnostic.debug_string_bytes_read += static_cast<DWORD>(read);
        const auto hex = bytes_hex(bytes);
        const bool budget_truncated = requested < reported;
        const bool complete_prefix = ok && read_bound_valid && read == reported && !budget_truncated;
        const bool terminal_nul = complete_prefix &&
            (string.fUnicode != 0 ? (read >= 2 && read % 2 == 0 && bytes[read - 1] == std::byte{0} &&
                bytes[read - 2] == std::byte{0}) : (read >= 1 && bytes[read - 1] == std::byte{0}));
        Record("debug_string").text("address_decimal", handle_decimal(string.lpDebugStringData))
            .number("unicode_flag_raw", string.fUnicode).number("length_low16_bytes_raw", reported)
            .boolean("reported_length_is_low16", true).boolean("full_string_length_known", false)
            .number("bytes_requested", requested).number("bytes_read", read)
            .boolean("read_attempted", requested != 0).boolean("read_ok", ok != FALSE).number("error", error)
            .boolean("read_count_within_bound", read_bound_valid)
            .boolean("prefix_truncated_by_budget", budget_truncated)
            .boolean("prefix_read_incomplete", read != requested)
            .boolean("reported_prefix_fully_captured", complete_prefix)
            .boolean("terminal_nul_observed", terminal_nul).text("raw_hex", hex);
        if (budget_truncated) diagnostic.bound_hit = true;
        if (!ok || read != requested) ++errors;
    }

    void observe_event(const DEBUG_EVENT& event, bool cleanup) {
        // Construct ownership before any record allocation or validation can
        // throw. Each valid image file handle closes before event continuation.
        HANDLE raw_file = nullptr;
        if (event.dwDebugEventCode == CREATE_PROCESS_DEBUG_EVENT) raw_file = event.u.CreateProcessInfo.hFile;
        else if (event.dwDebugEventCode == LOAD_DLL_DEBUG_EVENT) raw_file = event.u.LoadDll.hFile;
        Handle image_file(raw_file, "debug-event-image-file");
        Record("debug_event").number("event_index", diagnostic.events_observed)
            .number("event_code", event.dwDebugEventCode).number("pid", event.dwProcessId).number("tid", event.dwThreadId)
            .number("wait_call_index", diagnostic.wait_calls).boolean("cleanup_phase", cleanup);
        require(event.dwProcessId == child_pid, "debug event belongs to the actual created child only");
        switch (event.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT: {
            require(!diagnostic.create_event_observed, "single actual child create debug event");
            diagnostic.create_event_observed = true;
            const auto& info = event.u.CreateProcessInfo;
            Record("debug_process_created").text("borrowed_process_handle_decimal", handle_decimal(info.hProcess))
                .text("borrowed_thread_handle_decimal", handle_decimal(info.hThread))
                .text("actual_pi_process_handle_decimal", handle_decimal(process.get()))
                .text("image_base_decimal", handle_decimal(info.lpBaseOfImage))
                .text("start_address_decimal", std::to_string(reinterpret_cast<std::uintptr_t>(info.lpStartAddress)))
                .boolean("event_process_thread_handles_owned_by_windows", true);
            const auto actual_image = startup_image_identity(image_file.get(), "actual-create-process-image");
            const bool equal = same_startup_image(actual_image, expected_image);
            Record("actual_child_image_binding").boolean("same_as_held_child_image_pin", equal)
                .boolean("child_pin_already_bound", diagnostic.child_image_pin_bound)
                .boolean("event_hfile_is_actual_image_observation", true)
                .boolean("A2_or_A4_authority", false);
            require(diagnostic.child_image_pin_bound && equal,
                "actual CREATE_PROCESS image identity equals the retained admitted child pin");
            diagnostic.child_image_event_bound = true;
            add_module(info.lpBaseOfImage, image_path(image_file.get(), "create-process-image"));
            break;
        }
        case CREATE_THREAD_DEBUG_EVENT:
            Record("debug_thread_created").text("borrowed_thread_handle_decimal", handle_decimal(event.u.CreateThread.hThread))
                .text("start_address_decimal", std::to_string(reinterpret_cast<std::uintptr_t>(event.u.CreateThread.lpStartAddress)))
                .boolean("event_thread_handle_owned_by_windows", true);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            Record("debug_dll_loaded").text("image_base_decimal", handle_decimal(event.u.LoadDll.lpBaseOfDll))
                .text("image_file_handle_decimal", handle_decimal(image_file.get()));
            add_module(event.u.LoadDll.lpBaseOfDll, image_path(image_file.get(), "load-dll-image"));
            break;
        case UNLOAD_DLL_DEBUG_EVENT: {
            const auto base = reinterpret_cast<std::uintptr_t>(event.u.UnloadDll.lpBaseOfDll);
            bool matched = false;
            for (auto& module : modules) if (module.active && module.base == base) { module.active = false; matched = true; }
            Record("debug_dll_unloaded").text("image_base_decimal", std::to_string(base))
                .boolean("matched_observed_module", matched);
            break;
        }
        case EXCEPTION_DEBUG_EVENT: exception_record(event.u.Exception); break;
        case OUTPUT_DEBUG_STRING_EVENT: debug_string(event.u.DebugString); break;
        case EXIT_THREAD_DEBUG_EVENT:
            Record("debug_thread_exit").number("exit_code", event.u.ExitThread.dwExitCode);
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            require(!diagnostic.exit_event_observed, "single actual child exit debug event");
            diagnostic.exit_event_observed = true;
            diagnostic.debug_exit_code = event.u.ExitProcess.dwExitCode;
            Record("debug_process_exit").number("exit_code", diagnostic.debug_exit_code)
                .boolean("diagnostic_cleanup_preceded_event", diagnostic.cleanup_requested);
            break;
        case RIP_EVENT:
            Record("debug_rip").number("error", event.u.RipInfo.dwError).number("type", event.u.RipInfo.dwType);
            ++errors; diagnostic.structural_observation_failed = true;
            break;
        default:
            Record("debug_unrecognized_event").number("event_code", event.dwDebugEventCode);
            ++errors; diagnostic.structural_observation_failed = true;
            break;
        }
        if (!image_file.close()) diagnostic.structural_observation_failed = true;
    }

    void poll_startup() {
        if (diagnostic.startup_read_attempted || diagnostic.report_broken || diagnostic.report_poll_failed) return;
        DWORD available = 0;
        const BOOL peeked = PeekNamedPipe(report, nullptr, 0, nullptr, &available, nullptr);
        const DWORD peek_error = peeked ? 0 : GetLastError();
        Record("startup_peek").boolean("ok", peeked != FALSE).number("error", peek_error)
            .number("available_bytes", available).boolean("read_attempted", false);
        if (!peeked) {
            if (peek_error == ERROR_BROKEN_PIPE) diagnostic.report_broken = true;
            else {
                ++errors;
                diagnostic.report_poll_failed = true;
                diagnostic.structural_observation_failed = true;
            }
            return;
        }
        if (available == 0) return;
        diagnostic.startup_read_attempted = true;
        const DWORD requested = std::min<DWORD>(available, sizeof(StartupSnapshot));
        std::vector<std::byte> bytes(requested);
        DWORD read = 0;
        const BOOL ok = ReadFile(report, bytes.data(), requested, &read, nullptr);
        const DWORD error = ok ? 0 : GetLastError();
        const bool read_bound_valid = read <= requested;
        if (!read_bound_valid) { ++errors; read = 0; }
        bytes.resize(read);
        diagnostic.startup_observed = read != 0;
        const auto hex = bytes_hex(bytes);
        Record("startup_transport").number("phase", startup_handles).boolean("ok", ok != FALSE)
            .number("error", error).number("available_before_read", available).number("bytes_requested", requested)
            .number("bytes_read_raw", read).text("raw_hex", hex).boolean("read_count_within_bound", read_bound_valid)
            .boolean("read_preceded_by_sufficient_peek", requested <= available);
        if (!ok || read != sizeof(StartupSnapshot) || available > sizeof(StartupSnapshot)) {
            ++errors; diagnostic.structural_observation_failed = true; return;
        }
        StartupSnapshot snapshot{};
        std::memcpy(&snapshot, bytes.data(), sizeof(snapshot));
        const bool input_valid = valid(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(snapshot.input)));
        const bool output_valid = valid(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(snapshot.output)));
        const bool frame = snapshot.magic == protocol_magic && snapshot.command == protocol_startup &&
            snapshot.phase == startup_handles && snapshot.argc == 6 && snapshot.input_ok <= 1 && snapshot.output_ok <= 1 &&
            snapshot.input == reinterpret_cast<std::uintptr_t>(expected_input) &&
            snapshot.output == reinterpret_cast<std::uintptr_t>(expected_output);
        const bool predicates = input_valid && output_valid && snapshot.input != snapshot.output &&
            snapshot.input_ok && snapshot.input_error == 0 && (snapshot.input_flags & HANDLE_FLAG_INHERIT) &&
            snapshot.output_ok && snapshot.output_error == 0 && (snapshot.output_flags & HANDLE_FLAG_INHERIT);
        diagnostic.startup_frame_valid = frame && predicates;
        Record("client_startup").number("magic", snapshot.magic).number("command", snapshot.command)
            .number("phase", snapshot.phase).number("argc", snapshot.argc)
            .text("input_handle_decimal", std::to_string(snapshot.input)).text("output_handle_decimal", std::to_string(snapshot.output))
            .boolean("input_parse_valid", input_valid).boolean("output_parse_valid", output_valid)
            .boolean("handles_distinct", snapshot.input != snapshot.output)
            .number("input_query_ok_raw", snapshot.input_ok).number("input_query_error", snapshot.input_error)
            .number("input_flags_raw", snapshot.input_flags).number("output_query_ok_raw", snapshot.output_ok)
            .number("output_query_error", snapshot.output_error).number("output_flags_raw", snapshot.output_flags)
            .boolean("framing_valid", frame).boolean("original_handle_predicates_valid", predicates);
        if (!diagnostic.startup_frame_valid) { ++errors; diagnostic.structural_observation_failed = true; }
    }

    bool receive_event(ULONGLONG deadline, bool cleanup) {
        if (diagnostic.wait_calls >= startup_wait_call_limit ||
            (cleanup ? diagnostic.cleanup_wait_calls >= startup_cleanup_wait_call_limit :
                diagnostic.main_wait_calls >= startup_main_wait_call_limit)) {
            diagnostic.bound_hit = true;
            Record("debug_wait_call_bound").boolean("cleanup_phase", cleanup)
                .number("wait_calls", diagnostic.wait_calls).number("main_wait_calls", diagnostic.main_wait_calls)
                .number("cleanup_wait_calls", diagnostic.cleanup_wait_calls)
                .number("total_call_limit", startup_wait_call_limit)
                .number("main_call_limit", startup_main_wait_call_limit)
                .number("cleanup_call_limit", startup_cleanup_wait_call_limit);
            return false;
        }
        const auto now = GetTickCount64();
        if (now >= deadline) {
            diagnostic.bound_hit = true;
            Record("debug_wait_bound").boolean("cleanup_phase", cleanup).boolean("deadline_reached", true)
                .number("events_observed", diagnostic.events_observed);
            return false;
        }
        const DWORD timeout = static_cast<DWORD>(std::min<ULONGLONG>(startup_wait_limit_ms, deadline - now));
        DEBUG_EVENT event{};
        ++diagnostic.wait_calls;
        if (cleanup) ++diagnostic.cleanup_wait_calls;
        else ++diagnostic.main_wait_calls;
        const BOOL received = WaitForDebugEventEx(&event, timeout);
        const DWORD error = received ? 0 : GetLastError();
        if (!received) {
            const bool timeout_only = error == ERROR_SEM_TIMEOUT || error == ERROR_TIMEOUT;
            Record("debug_wait").number("timeout_ms", timeout).boolean("cleanup_phase", cleanup)
                .number("wait_call_index", diagnostic.wait_calls)
                .boolean("ok", false).number("error", error).boolean("bounded_timeout", timeout_only);
            if (!timeout_only) { ++errors; diagnostic.structural_observation_failed = true; return false; }
            if (!cleanup) poll_startup();
            return true;
        }
        ++diagnostic.events_observed;
        StartupEvent pending(event, diagnostic, child_pid);
        try { observe_event(event, cleanup); }
        catch (const std::exception& exception) {
            diagnostic.structural_observation_failed = true;
            ++errors; Record("debug_observation_exception").text("message", exception.what());
        } catch (...) {
            diagnostic.structural_observation_failed = true;
            ++errors; Record("debug_observation_exception").text("message", "unknown exception while recording event");
        }
        const bool continued = pending.continue_once();
        if (!cleanup) poll_startup();
        return continued;
    }

    void actual_exit(ULONGLONG deadline) {
        // Windows has not completed exit until its EXIT_PROCESS event has
        // been continued. Never substitute a default exit value for observation.
        if (!diagnostic.exit_event_continued) {
            Record("actual_child_exit_unobserved").text("reason", "actual EXIT_PROCESS event was not continued")
                .boolean("process_exit_query_attempted", false);
            return;
        }
        const auto now = GetTickCount64();
        const DWORD timeout = now < deadline ?
            static_cast<DWORD>(std::min<ULONGLONG>(1000, deadline - now)) : 0;
        const DWORD waited = WaitForSingleObject(process.get(), timeout);
        const DWORD wait_error = waited == WAIT_FAILED ? GetLastError() : 0;
        Record("actual_child_wait").text("process_handle_decimal", handle_decimal(process.get()))
            .number("maximum_wait_ms", timeout)
            .boolean("exit_event_already_continued", diagnostic.exit_event_continued)
            .number("result", waited).number("error", wait_error);
        DWORD code = 0;
        const BOOL queried = GetExitCodeProcess(process.get(), &code);
        const DWORD query_error = queried ? 0 : GetLastError();
        {
            Record record("actual_child_exit");
            record.boolean("ok", queried != FALSE).number("error", query_error)
                .boolean("process_signaled", waited == WAIT_OBJECT_0);
            if (queried) record.number("exit_code_raw", code);
        }
        if (waited == WAIT_OBJECT_0 && queried) {
            diagnostic.actual_child_exit_observed = true;
            diagnostic.client_exit_code = code;
        } else { ++errors; }
        FILETIME created{}, exited{}, kernel{}, user{};
        const BOOL times_ok = GetProcessTimes(process.get(), &created, &exited, &kernel, &user);
        const DWORD times_error = times_ok ? 0 : GetLastError();
        {
            Record record("actual_child_times");
            record.text("process_handle_decimal", handle_decimal(process.get()))
                .boolean("ok", times_ok != FALSE).number("error", times_error)
                .boolean("exit_time_observed", times_ok && waited == WAIT_OBJECT_0);
            if (times_ok) {
                record.text("creation_time_decimal", std::to_string(filetime_value(created)))
                    .text("kernel_time_decimal", std::to_string(filetime_value(kernel)))
                    .text("user_time_decimal", std::to_string(filetime_value(user)));
                if (waited == WAIT_OBJECT_0) record.text("exit_time_decimal", std::to_string(filetime_value(exited)));
            } else { ++errors; }
        }
        if (diagnostic.exit_event_observed && diagnostic.actual_child_exit_observed) {
            const bool equal = diagnostic.client_exit_code == diagnostic.debug_exit_code;
            Record("actual_debug_exit_comparison").number("debug_exit_code", diagnostic.debug_exit_code)
                .number("actual_exit_code", diagnostic.client_exit_code).boolean("equal", equal);
            if (!equal) ++errors;
        }
    }

public:
    StartupSession(Handle& actual_process, Handle& owned_job, HANDLE reports, HANDLE input, HANDLE output,
        DWORD pid, ULONGLONG start, StartupDiagnostic& state, const StartupImageIdentity& image, bool created) noexcept
        : process(actual_process), job(owned_job), report(reports), expected_input(input), expected_output(output),
          child_pid(pid), started(start), diagnostic(state), expected_image(image), armed(created) {}
    StartupSession(const StartupSession&) = delete;
    ~StartupSession() noexcept {
        if (armed && !finished) finish("admission-or-observation-exception");
    }
    void observe() {
        const auto deadline = started + startup_main_limit_ms;
        // Missing optional path/allocation/debug-string fields keep their exact
        // errors and forbid capture_complete, but must not destroy the startup
        // path whose later exception/exit events this diagnostic is observing.
        while (!diagnostic.exit_event_continued && !diagnostic.startup_read_attempted &&
            !diagnostic.structural_observation_failed) {
            if (GetTickCount64() >= deadline || diagnostic.events_observed >= startup_main_event_limit) {
                diagnostic.bound_hit = true;
                Record("debug_main_bound").number("events_observed", diagnostic.events_observed)
                    .number("main_event_limit", startup_main_event_limit).text("reason", "main deadline or reserved event bound");
                break;
            }
            if (!receive_event(deadline, false)) break;
        }
        const auto main_finished = GetTickCount64();
        if (main_finished >= deadline) {
            diagnostic.bound_hit = true;
            Record("debug_main_end_bound").boolean("deadline_reached", true)
                .boolean("exit_event_continued", diagnostic.exit_event_continued)
                .boolean("startup_read_attempted", diagnostic.startup_read_attempted)
                .number("elapsed_ms_descriptive_only", main_finished - started);
        }
        const char* reason = diagnostic.exit_event_continued ? "actual-child-exit-event-continued" :
            (diagnostic.startup_read_attempted ? "startup-observation-complete-no-client-command-sent" :
                (diagnostic.bound_hit ? "main-observation-bound" : "diagnostic-observation-error"));
        finish(reason);
    }
    void finish(const char* reason) noexcept {
        if (!armed || finished) return;
        finished = true;
        const auto cleanup_started = GetTickCount64();
        const auto deadline = std::min<ULONGLONG>(cleanup_started + startup_cleanup_limit_ms,
            started + startup_main_limit_ms + startup_cleanup_limit_ms);
        try {
            Record("diagnostic_cleanup").text("reason", reason).boolean("no_client_command_sent", true)
                .boolean("counts_as_natural_completion", false).number("remaining_budget_ms", deadline > cleanup_started ? deadline - cleanup_started : 0);
            if (!diagnostic.exit_event_continued) {
                diagnostic.cleanup_requested = true;
                const BOOL terminated = TerminateProcess(process.get(), 3);
                const DWORD error = terminated ? 0 : GetLastError();
                Record("diagnostic_child_termination").text("process_handle_decimal", handle_decimal(process.get()))
                    .number("pid", child_pid).number("requested_exit_code", 3)
                    .boolean("ok", terminated != FALSE).number("error", error)
                    .text("diagnostic_cleanup_reason", reason).boolean("qualification_evidence", false);
                if (!terminated) ++errors;
            }
            while (!diagnostic.exit_event_continued && !diagnostic.continuation_failed &&
                diagnostic.events_observed < startup_event_limit &&
                diagnostic.wait_calls < startup_wait_call_limit &&
                diagnostic.cleanup_wait_calls < startup_cleanup_wait_call_limit && GetTickCount64() < deadline) {
                if (!receive_event(deadline, true)) break;
            }
            if (!diagnostic.exit_event_continued) {
                diagnostic.bound_hit = true;
                ++errors;
                Record("debug_cleanup_incomplete").number("events_observed", diagnostic.events_observed)
                    .boolean("event_limit_hit", diagnostic.events_observed >= startup_event_limit)
                    .boolean("wait_limit_hit", diagnostic.wait_calls >= startup_wait_call_limit ||
                        diagnostic.cleanup_wait_calls >= startup_cleanup_wait_call_limit)
                    .boolean("deadline_hit", GetTickCount64() >= deadline)
                    .boolean("continuation_failed", diagnostic.continuation_failed);
            }
            // One last nonblocking observation can read a startup frame already
            // buffered before a natural failure/exit event. No command is sent.
            poll_startup();
            actual_exit(deadline);
            record_client_job_snapshot(job.get(), "after-debug-exit-before-owned-Job-close");
        } catch (const std::exception& exception) {
            ++errors;
            try { Record("debug_cleanup_exception").text("message", exception.what()); } catch (...) { ++errors; }
        } catch (...) { ++errors; }
        // Close the kill-on-close Job while the parent command write end is
        // still held. EOF must not release the original client's first command
        // read and make it emit the later, out-of-scope protocol snapshot.
        job.close();
        const auto cleanup_finished = GetTickCount64();
        if (cleanup_finished >= deadline) {
            diagnostic.bound_hit = true;
            try {
                Record("debug_cleanup_end_bound").boolean("deadline_reached", true)
                    .boolean("exit_event_continued", diagnostic.exit_event_continued)
                    .boolean("actual_child_exit_observed", diagnostic.actual_child_exit_observed)
                    .number("cleanup_elapsed_ms_descriptive_only", cleanup_finished - cleanup_started)
                    .number("elapsed_ms_descriptive_only", cleanup_finished - started);
            } catch (...) { ++errors; }
        }
    }
};

// Admission below deliberately reuses the exact 008 token, explicit kernel
// DACL, default-DACL amendment, nested Job and inherited-channel predicates.
// The separately prepared child image has one loader flag enabled. Its
// read-only identity binding is diagnostic evidence, never A2/A4 authority.
// No leaf exists and no command enters the original client's IPC loop.
void run_startup_parent(const std::wstring& root, const std::wstring& image,
    const StartupImageIdentity& expected_image, StartupDiagnostic& diagnostic, ULONGLONG started) {
    require(root.size() < 30000 && root.find(L'"') == std::wstring::npos, "bounded fixture root argument");
    require(!image.empty() && image.size() < 30000 && image.find(L'"') == std::wstring::npos,
        "bounded separately prepared child image argument");
    HANDLE image_raw = CreateFileW(image.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    const DWORD image_error = valid(image_raw) ? 0 : GetLastError();
    Handle child_image_pin(image_raw, "child-image-read-pin");
    Record("child_image_pin_open").text("handle_decimal", handle_decimal(child_image_pin.get()))
        .boolean("ok", valid(child_image_pin.get())).number("error", image_error)
        .number("desired_access", GENERIC_READ).number("share_mode", FILE_SHARE_READ)
        .number("creation_disposition", OPEN_EXISTING).number("flags", FILE_FLAG_OPEN_REPARSE_POINT)
        .boolean("inheritable_requested", false).boolean("A2_or_A4_authority", false);
    require(valid(child_image_pin.get()), "retain one read-only child image pin before child creation");
    DWORD pin_flags = 0;
    checked(GetHandleInformation(child_image_pin.get(), &pin_flags), "GetHandleInformation", "child image pin");
    require((pin_flags & HANDLE_FLAG_INHERIT) == 0, "child image pin is non-inheritable");
    const auto pinned_image = startup_image_identity(child_image_pin.get(), "child-image-before-launch");
    require(same_startup_image(pinned_image, expected_image),
        "retained child image pin equals the runner supplied exact physical identity and size");
    diagnostic.child_image_pin_bound = true;
    checked(CreateDirectoryW(root.c_str(), nullptr), "CreateDirectoryW", "fresh fixture root");
    const std::wstring leaf = root + L"\\delegated-metadata.bin";
    HANDLE raw_token = nullptr;
    // CreateRestrictedToken returns the input handle's access rights. Only the
    // new restricted token is passed to SetTokenInformation(TokenDefaultDacl).
    checked(OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE | TOKEN_ASSIGN_PRIMARY |
        TOKEN_ADJUST_PRIVILEGES | TOKEN_ADJUST_DEFAULT, &raw_token), "OpenProcessToken", "server actual primary token");
    Handle host_token(raw_token, "server-primary-token");
    const auto host_claim = actual_claim(host_token.get(), "server-primary-before-filter");
    const auto user = token_user(host_token.get());
    const auto host_authentication = authentication_id(host_token.get(), "server-primary");
    const auto groups = token_sids(host_token.get(), TokenGroups, "server-groups");
    require(token_sids(host_token.get(), TokenRestrictedSids, "server-restrictions").empty(),
        "unrestricted server baseline token");
    LUID unique{};
    checked(AllocateLocallyUniqueId(&unique), "AllocateLocallyUniqueId", "fresh restricting SID discriminator");
    const auto restricting_sid = L"S-1-5-21-" + std::to_wstring(static_cast<DWORD>(unique.HighPart)) +
        L"-" + std::to_wstring(unique.LowPart) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-1048577";
    require(user != restricting_sid, "restricting SID is not actual user");
    for (const auto& group : groups) require(group != restricting_sid, "restricting SID not an enabled group");
    PSID raw_sid = nullptr;
    checked(ConvertStringSidToSidW(restricting_sid.c_str(), &raw_sid), "ConvertStringSidToSidW", "fresh restricting SID");
    LocalMemory sid_owner; sid_owner.value = raw_sid;
    const auto host_default_before = token_default_dacl(host_token.get(), raw_sid, "server-default-before-filter");
    SID_AND_ATTRIBUTES restriction{raw_sid, 0};
    HANDLE raw_restricted = nullptr;
    constexpr DWORD restriction_flags = DISABLE_MAX_PRIVILEGE | WRITE_RESTRICTED;
    checked(CreateRestrictedToken(host_token.get(), restriction_flags, 0, nullptr, 0, nullptr,
        1, &restriction, &raw_restricted), "CreateRestrictedToken", "WRITE_RESTRICTED plus disabled privileges");
    Handle restricted(raw_restricted, "pre-birth-restricted-primary-token");
    Record("restricted_token_created").number("flags", restriction_flags).wide("restricting_sid", restricting_sid);
    require(actual_claim(restricted.get(), "pre-birth-filtered-token") == host_claim,
        "filtered pre-birth claim remains distinguishable from child claim");
    const auto before_restrictions = token_sids(restricted.get(), TokenRestrictedSids, "pre-birth-restrictions");
    require(before_restrictions.size() == 1 && before_restrictions[0] == restricting_sid,
        "exact single restricting SID before process birth");
    const auto expected_default_dacl = add_restricted_default_dacl_sid(restricted.get(), raw_sid);

    Handle job(CreateJobObjectW(nullptr, nullptr), "client-only-job");
    require(valid(job.get()), "create owned client-only Job");
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    limits.BasicLimitInformation.ActiveProcessLimit = 1;
    checked(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)),
        "SetInformationJobObject", "one child, no child descendants, kill on owned close");
    BOOL server_in_job = TRUE;
    checked(IsProcessInJob(GetCurrentProcess(), job.get(), &server_in_job), "IsProcessInJob", "server outside client Job");
    require(!server_in_job, "server is not a client Job member");

    SECURITY_ATTRIBUTES inheritable{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE command_read_raw = nullptr, command_write_raw = nullptr;
    checked(CreatePipe(&command_read_raw, &command_write_raw, &inheritable, 4096), "CreatePipe", "owned command channel");
    Handle command_read(command_read_raw, "parent-command-read"), command_write(command_write_raw, "parent-command-write");
    checked(SetHandleInformation(command_write.get(), HANDLE_FLAG_INHERIT, 0), "SetHandleInformation", "parent command end not inherited");
    HANDLE report_read_raw = nullptr, report_write_raw = nullptr;
    checked(CreatePipe(&report_read_raw, &report_write_raw, &inheritable, 4096), "CreatePipe", "owned report channel");
    Handle report_read(report_read_raw, "parent-report-read"), report_write(report_write_raw, "parent-report-write");
    checked(SetHandleInformation(report_read.get(), HANDLE_FLAG_INHERIT, 0), "SetHandleInformation", "parent report end not inherited");
    const std::array<HANDLE, 2> inherited{command_read.get(), report_write.get()};
    const std::array<HANDLE, 1> birth_jobs{job.get()};
    AttributeList attributes;
    checked(UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(inherited.data()), sizeof(inherited), nullptr, nullptr),
        "UpdateProcThreadAttribute", "exact two protocol handles");
    checked(UpdateProcThreadAttribute(attributes.value, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST,
        const_cast<HANDLE*>(birth_jobs.data()), sizeof(birth_jobs), nullptr, nullptr),
        "UpdateProcThreadAttribute", "atomic birth into exact client-only Job");
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdInput = command_read.get();
    startup.StartupInfo.hStdOutput = report_write.get();
    startup.StartupInfo.hStdError = report_write.get();
    startup.lpAttributeList = attributes.value;
    std::vector<wchar_t> image_buffer(32768);
    const DWORD image_length = GetModuleFileNameW(nullptr, image_buffer.data(), static_cast<DWORD>(image_buffer.size()));
    require(image_length > 0 && image_length < image_buffer.size(), "exact current fixture image path");
    const std::wstring parent_image(image_buffer.data(), image_length);
    require(image != parent_image, "separately prepared child path differs from current parent path");
    const auto stem = L"\\\\.\\pipe\\mqb-loader-snaps-010-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(static_cast<DWORD>(unique.HighPart)) + L"-" + std::to_wstring(unique.LowPart);
    auto command_line = quote_argument(image) + L" --client " + quote_argument(leaf) + L" " + quote_argument(stem) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(command_read.get())) + L" " +
        std::to_wstring(reinterpret_cast<std::uintptr_t>(report_write.get()));
    Security process_security(L"D:P(A;;RC;;;OW)(XA;;0x001fffff;;;" + user + L";" + host_claim.expression() + L")");
    PROCESS_INFORMATION process_info{};
    const DWORD creation_flags = CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT | DEBUG_ONLY_THIS_PROCESS;
    const BOOL created = CreateProcessAsUserW(restricted.get(), image.c_str(), command_line.data(), &process_security.attributes,
        &process_security.attributes, TRUE, creation_flags, nullptr, root.c_str(), &startup.StartupInfo, &process_info);
    const DWORD creation_error = created ? 0 : GetLastError();
    Handle process(process_info.hProcess, "actual-client-process"), thread(process_info.hThread, "actual-client-thread");
    // Install the cleanup guard before the first post-creation record or query.
    // It dies before PI handles and before either parent protocol end.
    StartupSession session(process, job, report_read.get(), inherited[0], inherited[1],
        process_info.dwProcessId, started, diagnostic, pinned_image, created != FALSE);
    Record("api").text("operation", "CreateProcessAsUserW").text("label", "one suspended restricted debug client")
        .boolean("ok", created != FALSE).number("error", creation_error);
    require(created != FALSE, "one suspended restricted debug client created");
    const bool image_text_emitted = reserve_startup_path_text(diagnostic, image.size(), "created-child-image");
    const bool parent_text_emitted = reserve_startup_path_text(diagnostic, parent_image.size(), "current-parent-image");
    const bool command_text_emitted = reserve_startup_path_text(diagnostic, command_line.size(), "created-child-command-line");
    {
        Record record("client_created");
        record.number("pid", process_info.dwProcessId).number("tid", process_info.dwThreadId)
            .number("flags", creation_flags).number("inherited_handle_count", inherited.size())
            .boolean("image_text_emitted", image_text_emitted).boolean("parent_image_text_emitted", parent_text_emitted)
            .boolean("command_line_text_emitted", command_text_emitted).boolean("resumed", false);
        if (image_text_emitted) record.wide("image", image);
        if (parent_text_emitted) record.wide("parent_image", parent_image);
        if (command_text_emitted) record.wide("command_line", command_line);
    }
    BOOL child_in_job = FALSE;
    checked(IsProcessInJob(process.get(), job.get(), &child_in_job), "IsProcessInJob", "actual client Job membership");
    require(child_in_job != FALSE, "client is in its owned Job before resume");
    HANDLE child_token_raw = nullptr;
    checked(OpenProcessToken(process.get(), TOKEN_QUERY, &child_token_raw), "OpenProcessToken", "suspended actual client primary token");
    Handle child_token(child_token_raw, "actual-client-primary-query-only");
    const auto client_claim = actual_claim(child_token.get(), "actual-child-primary-after-birth");
    require(client_claim.values != host_claim.values, "new process actual claim differs from server/filter token");
    require(token_user(child_token.get()) == user, "client and server share authenticated user");
    require(authentication_id(child_token.get(), "actual-child-primary") == host_authentication,
        "actual child has the server authentication context");
    const auto child_restrictions = token_sids(child_token.get(), TokenRestrictedSids, "actual-child-restrictions");
    require(child_restrictions.size() == 1 && child_restrictions[0] == restricting_sid, "actual child has exact restricting SID");
    const auto child_type_buffer = token_information(child_token.get(), TokenType);
    require(child_type_buffer.size() >= sizeof(TOKEN_TYPE), "child token type length");
    TOKEN_TYPE child_type{}; std::memcpy(&child_type, child_type_buffer.data(), sizeof(child_type));
    require(child_type == TokenPrimary, "actual child token is primary");
    require(enabled_privileges(child_token.get(), "actual-child-privileges", true) <= 1,
        "child has no extra enabled privilege");
    const auto actual_child_default = token_default_dacl(child_token.get(), raw_sid, "actual-child-default-before-resume");
    require(same_default_dacl_aces(actual_child_default, expected_default_dacl),
        "actual C primary default DACL equals expected revision and complete ordered ACE bytes");
    require(command_read.close() && report_write.close(), "parent closes child protocol ends");
    require(restricted.close() && child_token.close(), "no pre-birth or actual-client token handle retained for impersonation");

    SetLastError(ERROR_SUCCESS);
    const BOOL disabled = AdjustTokenPrivileges(host_token.get(), TRUE, nullptr, 0, nullptr, nullptr);
    const DWORD disabled_error = GetLastError();
    Record("disable_server_privileges").boolean("ok", disabled != FALSE).number("error", disabled_error);
    require(disabled && disabled_error == ERROR_SUCCESS, "disable all privileges in this fixture server only");
    require(enabled_privileges(host_token.get(), "server-before-IPC") == 0, "no server privilege enabled for IPC");
    require(actual_claim(host_token.get(), "server-after-disabling-privileges") == host_claim, "server claim unchanged by privilege disable");
    const auto host_default_after = token_default_dacl(host_token.get(), raw_sid, "server-default-before-client-resume");
    require(same_default_dacl_aces(host_default_after, host_default_before),
        "server primary default DACL revision and complete ordered ACE bytes unchanged");
    no_thread_token("server before client resume");
    observe_kernel_dacl(process.get(), "actual-process-before-resume");
    observe_kernel_dacl(thread.get(), "actual-thread-before-resume");
    record_client_job_snapshot(job.get(), "before-client-resume");
    const DWORD suspend_count = ResumeThread(thread.get());
    const DWORD resume_error = suspend_count == static_cast<DWORD>(-1) ? GetLastError() : 0;
    Record("client_resume").number("previous_suspend_count", suspend_count).number("error", resume_error);
    require(suspend_count == 1, "single admitted client resume");
    require(thread.close(), "parent closes client thread handle after resume");

    session.observe();
    const auto final_image = startup_image_identity(child_image_pin.get(), "child-image-after-drain-and-owned-Job-close");
    require(same_startup_image(final_image, pinned_image), "child image pin has the same identity and size after cleanup");
    diagnostic.child_image_final_bound = true;
    require(command_write.close() && report_read.close() && process.close() && host_token.close() && child_image_pin.close(),
        "startup diagnostic remaining owned handles close");
}

DWORD WINAPI startup_watchdog(LPVOID parameter) {
    const DWORD result = WaitForSingleObject(static_cast<HANDLE>(parameter), 30000);
    if (result != WAIT_OBJECT_0) {
        constexpr char message[] = "producer-loader-snaps-010 watchdog expired or failed\n";
        DWORD written = 0;
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), message, sizeof(message) - 1, &written, nullptr);
        TerminateProcess(GetCurrentProcess(), 124);
    }
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc == 6 && std::wcscmp(argv[1], L"--client") == 0)
        return producer_delegation_008_wmain(argc, argv);
    if (argc != 7 || std::wcscmp(argv[1], L"--parent") != 0) return 3;
    const auto started = GetTickCount64();
    StartupDiagnostic diagnostic;
    Handle stop(CreateEventW(nullptr, TRUE, FALSE, nullptr), "startup-watchdog-stop");
    Handle watch;
    try {
        require(valid(stop.get()), "startup watchdog event created");
        watch = Handle(CreateThread(nullptr, 0, startup_watchdog, stop.get(), 0, nullptr), "startup-watchdog-thread");
        require(valid(watch.get()), "one auxiliary startup watchdog thread");
        Record("scope").text("attempt", "producer-loader-snaps-010").boolean("diagnostic_only", true)
            .number("max_processes", 2).number("test_named_pipes", 0).number("protocol_channels", 2)
            .number("ipc_cases_executed", 0).number("max_startup_protocol_bytes", sizeof(StartupSnapshot))
            .number("max_fixture_files", 0).number("max_fixture_payload_bytes", 0)
            .number("max_debug_events", startup_event_limit).number("main_debug_event_limit", startup_main_event_limit)
            .number("max_debug_wait_calls", startup_wait_call_limit)
            .number("main_debug_wait_call_limit", startup_main_wait_call_limit)
            .number("cleanup_debug_wait_call_limit", startup_cleanup_wait_call_limit)
            .number("main_deadline_ms", startup_main_limit_ms).number("cleanup_max_ms", startup_cleanup_limit_ms)
            .number("wait_max_ms", startup_wait_limit_ms).number("watchdog_seconds", 30)
            .number("debug_string_bytes_per_event", startup_string_limit).number("debug_string_bytes_total", startup_strings_limit)
            .number("module_path_wchars", startup_path_limit).number("path_text_wchars_total", startup_path_text_limit)
            .number("max_child_image_bytes", startup_image_limit).boolean("child_image_pin_inherited", false)
            .boolean("deletion_enabled", false);
        const auto expected_image = expected_startup_image(argv[4], argv[5], argv[6]);
        run_startup_parent(argv[2], argv[3], expected_image, diagnostic, started);
        const auto parent_finished = GetTickCount64();
        if (parent_finished >= started + startup_main_limit_ms + startup_cleanup_limit_ms) {
            diagnostic.bound_hit = true;
            Record("diagnostic_finalization_bound").boolean("deadline_reached", true)
                .boolean("child_image_final_bound", diagnostic.child_image_final_bound)
                .boolean("includes_final_image_queries_and_owned_handle_release", true)
                .number("elapsed_ms_descriptive_only", parent_finished - started);
        }
    } catch (const std::exception& exception) {
        ++errors; Record("exception").text("message", exception.what());
    } catch (...) {
        ++errors; Record("exception").text("message", "unknown startup diagnostic exception");
    }
    if (valid(stop.get())) {
        const BOOL set = SetEvent(stop.get());
        const DWORD error = set ? 0 : GetLastError();
        Record("watchdog_stop").boolean("ok", set != FALSE).number("error", error);
        if (!set) ++errors;
    }
    if (valid(watch.get())) {
        const auto now = GetTickCount64();
        const DWORD timeout = now < started + 29000 ?
            static_cast<DWORD>(std::min<ULONGLONG>(1000, started + 29000 - now)) : 0;
        const DWORD waited = WaitForSingleObject(watch.get(), timeout);
        const DWORD error = waited == WAIT_FAILED ? GetLastError() : 0;
        Record("watchdog_drain").number("maximum_wait_ms", timeout).number("result", waited).number("error", error);
        if (waited != WAIT_OBJECT_0) { ++errors; TerminateProcess(GetCurrentProcess(), 124); }
    }
    watch.close(); stop.close();
    const bool complete = errors == 0 && !diagnostic.bound_hit && !diagnostic.structural_observation_failed &&
        diagnostic.child_image_pin_bound && diagnostic.child_image_event_bound && diagnostic.child_image_final_bound &&
        diagnostic.create_event_observed &&
        diagnostic.exit_event_observed && diagnostic.exit_event_continued && diagnostic.actual_child_exit_observed &&
        diagnostic.client_exit_code == diagnostic.debug_exit_code &&
        diagnostic.events_observed == diagnostic.event_continuations &&
        diagnostic.events_observed == diagnostic.continuation_successes;
    Record summary("summary");
    summary.text("classification", "INCONCLUSIVE").boolean("diagnostic_only", true).number("errors", errors)
        .number("ipc_cases_executed", 0).number("events_observed", diagnostic.events_observed)
        .number("event_continuations", diagnostic.event_continuations).number("continuation_successes", diagnostic.continuation_successes)
        .number("wait_calls", diagnostic.wait_calls).number("main_wait_calls", diagnostic.main_wait_calls)
        .number("cleanup_wait_calls", diagnostic.cleanup_wait_calls).number("path_wchars_emitted", diagnostic.path_wchars_emitted)
        .boolean("child_image_pin_bound", diagnostic.child_image_pin_bound)
        .boolean("child_image_event_bound", diagnostic.child_image_event_bound)
        .boolean("child_image_final_bound", diagnostic.child_image_final_bound).boolean("bound_hit", diagnostic.bound_hit)
        .boolean("startup_observed", diagnostic.startup_observed).boolean("startup_frame_valid", diagnostic.startup_frame_valid)
        .boolean("exit_event_observed", diagnostic.exit_event_observed)
        .boolean("actual_child_exit_observed", diagnostic.actual_child_exit_observed)
        .boolean("cleanup_requested", diagnostic.cleanup_requested).boolean("capture_complete", complete)
        .boolean("structural_observation_failed", diagnostic.structural_observation_failed)
        .boolean("initial_breakpoint_continue_attempted", diagnostic.initial_breakpoint_continued)
        .number("debug_string_bytes_requested", diagnostic.debug_string_bytes_requested)
        .number("debug_string_bytes_read", diagnostic.debug_string_bytes_read)
        .boolean("A1", false).boolean("A2", false).boolean("A3", false).boolean("A4", false).boolean("A5", false)
        .boolean("deletion_enabled", false).number("elapsed_ms_descriptive_only", GetTickCount64() - started);
    if (diagnostic.actual_child_exit_observed) summary.number("client_exit_code", diagnostic.client_exit_code);
    return 3;
}
