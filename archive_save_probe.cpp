#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include "mqb/core/ArchiveCacheFile.hpp"
int main(int argc,char** argv) {
    if(argc!=2) return 2;
    const std::filesystem::path file{argv[1]};
    mqb::ArchiveCacheEntry entry{
        .librarian={.librarian="tools/lib.exe",.version="14.51",.binary_stamp="same-stamp"},
        .signature=mqb::BuildSignature::from_digest({42,99}),
        .objects={},.output="bin/report.lib"};
    for(int i=0;i<129;++i) entry.objects.emplace_back("project/path/to/objects/unit_"+std::to_string(i)+".obj");
    if(!mqb::ArchiveCacheFile::save(file,entry)) return 3;
    using Clock=std::chrono::steady_clock;
    const auto start=Clock::now();
    for(int i=0;i<32;++i) if(!mqb::ArchiveCacheFile::save(file,entry)) return 4;
    const double ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    const auto loaded=mqb::ArchiveCacheFile::load(file);
    if(!loaded||!*loaded||(**loaded).objects!=entry.objects||(**loaded).signature!=entry.signature) return 5;
    std::cout<<"{\"save_batch_ms\":"<<ms<<",\"calls\":32,\"bytes\":"<<std::filesystem::file_size(file)<<"}\n";
}
