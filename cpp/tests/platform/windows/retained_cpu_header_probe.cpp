// Offline reader only: never enable providers, record events or start a child.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <evntrace.h>
#include <evntcons.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
namespace fs = std::filesystem;
constexpr GUID provider{0x22fb2cd6,0x0e7b,0x422b,{0xa0,0xc7,0x2f,0xad,0x1f,0xd0,0xe7,0x16}};
void need(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string hex(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    constexpr char digits[] = "0123456789abcdef";
    std::string out; out.reserve(size * 2);
    for (std::size_t i = 0; i < size; ++i) { out += digits[bytes[i] >> 4]; out += digits[bytes[i] & 15]; }
    return out;
}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) ::CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
struct Trace {
    TRACEHANDLE value{INVALID_PROCESSTRACE_HANDLE};
    ~Trace() { if (value != INVALID_PROCESSTRACE_HANDLE) ::CloseTrace(value); }
};
struct Reader {
    std::ostream& out;
    std::uint64_t total{}, selected{}, errors{};
    explicit Reader(std::ostream& stream) : out(stream) {}
    static void WINAPI callback(EVENT_RECORD* event) noexcept {
        auto& self = *static_cast<Reader*>(event->UserContext);
        ++self.total; // Same global callback ordinal as the original decoder.
        const auto& h = event->EventHeader;
        if (!::IsEqualGUID(h.ProviderId, provider) || h.EventDescriptor.Id < 1 || h.EventDescriptor.Id > 4) return;
        ++self.selected;
        if (self.errors) return;
        try {
            need(self.selected <= 500000 && event->UserData && event->UserDataLength, "invalid or excessive records");
            self.out << "{\"sequence\":" << self.total << ",\"provider\":\"process\",\"id\":" << h.EventDescriptor.Id
                << ",\"version\":" << unsigned(h.EventDescriptor.Version) << ",\"flags\":" << h.Flags
                << ",\"pid\":" << h.ProcessId << ",\"tid\":" << h.ThreadId << ",\"qpc\":" << h.TimeStamp.QuadPart
                << ",\"raw_payload\":\"" << hex(event->UserData, event->UserDataLength) << "\""
                << ",\"kernel_units\":" << h.KernelTime << ",\"user_units\":" << h.UserTime
                << ",\"processor_time_raw\":" << h.ProcessorTime << ",\"header_size\":" << h.Size
                << ",\"header_type\":" << h.HeaderType << ",\"event_property\":" << h.EventProperty << "}\n";
            need(bool(self.out), "cannot write event output");
        } catch (...) { ++self.errors; }
    }
};
void self_test() {
    std::ostringstream out; Reader reader(out); EVENT_RECORD e{};
    unsigned char payload[]{0, 0xff}; e.UserData=payload; e.UserDataLength=2; e.UserContext=&reader;
    e.EventHeader.ProviderId=provider; e.EventHeader.EventDescriptor.Id=4;
    e.EventHeader.ProcessId=31; e.EventHeader.ThreadId=47; e.EventHeader.KernelTime=2; e.EventHeader.UserTime=3;
    e.EventHeader.Flags=EVENT_HEADER_FLAG_64_BIT_HEADER | EVENT_HEADER_FLAG_NO_CPUTIME;
    Reader::callback(&e);
    need(reader.total==1 && reader.selected==1 && reader.errors==0, "callback accounting");
    need(out.str().find("\"kernel_units\":2,\"user_units\":3,\"processor_time_raw\":12884901890")!=std::string::npos, "raw union retention");
    need(out.str().find("\"pid\":31,\"tid\":47")!=std::string::npos, "emitter identity retention");
    need(out.str().find("\"raw_payload\":\"00ff\"")!=std::string::npos, "payload hex retention");
    e.EventHeader.ProviderId={}; Reader::callback(&e);
    need(reader.total==2 && reader.selected==1, "provider filter");
    e.EventHeader.ProviderId=provider; e.EventHeader.EventDescriptor.Id=5; Reader::callback(&e);
    need(reader.total==3 && reader.selected==1, "event filter");
    e.EventHeader.EventDescriptor.Id=4; e.UserData=nullptr; Reader::callback(&e);
    need(reader.errors==1, "invalid data rejection");
    std::cout << "Offline CPU-header reader memory self-tests passed; no trace captured.\n";
}
void read(const fs::path& input, const fs::path& output) {
    // Deny writes/deletion while the Windows consumer opens the SAME local file.
    Handle pin(::CreateFileW(input.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    need(pin.value!=INVALID_HANDLE_VALUE, "cannot read-pin input ETL");
    LARGE_INTEGER bytes{}; need(::GetFileSizeEx(pin.value, &bytes)!=0 && bytes.QuadPart>0 && bytes.QuadPart<256LL*1024*1024, "invalid ETL length");
    std::ofstream records(output/"headers.jsonl", std::ios::binary); need(bool(records), "cannot create records");
    Reader reader(records); EVENT_TRACE_LOGFILEW log{}; auto path=input.wstring(); log.LogFileName=path.data();
    log.ProcessTraceMode=PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    log.EventRecordCallback=Reader::callback; log.Context=&reader;
    Trace trace; trace.value=::OpenTraceW(&log);
    const ULONG open_error=trace.value==INVALID_PROCESSTRACE_HANDLE ? ::GetLastError() : ERROR_SUCCESS;
    ULONG processed=ERROR_INVALID_HANDLE, closed=ERROR_INVALID_HANDLE;
    if (open_error==ERROR_SUCCESS) {
        processed=::ProcessTrace(&trace.value, 1, nullptr, nullptr);
        closed=::CloseTrace(trace.value); trace.value=INVALID_PROCESSTRACE_HANDLE;
    }
    records.flush();
    std::ofstream summary(output/"reader.json", std::ios::binary);
    summary << "{\"schema\":1,\"open_status\":" << open_error << ",\"process_trace_status\":" << processed
        << ",\"close_trace_status\":" << closed << ",\"total_events\":" << reader.total
        << ",\"selected_events\":" << reader.selected << ",\"errors\":" << reader.errors
        << ",\"etl_bytes\":" << bytes.QuadPart << ",\"log_header_events_lost\":" << log.LogfileHeader.EventsLost
        << ",\"timer_resolution_100ns\":" << log.LogfileHeader.TimerResolution
        << ",\"perf_frequency\":" << log.LogfileHeader.PerfFreq.QuadPart
        << ",\"pointer_size\":" << log.LogfileHeader.PointerSize
        << ",\"log_file_mode\":" << log.LogfileHeader.LogFileMode
        << ",\"reserved_flags\":" << log.LogfileHeader.ReservedFlags
        << ",\"buffers_read\":" << log.BuffersRead
        << ",\"new_trace_captures\":0,\"release_authorized\":false}\n";
    summary.flush();
    need(bool(records) && bool(summary), "output write failure");
    need(open_error==0 && processed==0 && closed==0 && reader.errors==0 && log.LogfileHeader.EventsLost==0,
         "offline read failed; retain outputs, do not interpret missing CPU counters");
}
}
int wmain(int argc, wchar_t** argv) {
    fs::path output; bool created=false;
    try {
        if (argc==2 && std::wstring_view(argv[1])==L"--self-test") { self_test(); return 0; }
        need(argc==3, "use retained_cpu_header_probe INPUT-ETL NEW-OUTPUT-DIRECTORY");
        const auto input=fs::absolute(argv[1]); output=fs::absolute(argv[2]);
        need(fs::is_regular_file(input), "input ETL missing or not a regular file");
        created=fs::create_directory(output); need(created, "output already exists; no overwrite");
        read(input, output); return 0;
    } catch (const std::exception& e) {
        if (created) {
            std::ofstream failure(output/"failure.json", std::ios::binary);
            failure << "{\"status\":\"failed\",\"release_authorized\":false}\n";
        }
        std::cerr << "OFFLINE_CPU_HEADER_FAILURE " << e.what() << '\n'; return 1;
    }
}
