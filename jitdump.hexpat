#pragma description jitdump

#include <std/mem.pat>
#include <std/string.pat>
#include <type/fmt.pat>

using Str = std::string::NullString;
using Addr = type::Formatted<u64, "0x{:08X}">;

namespace impl {
  fn format_Record(ref auto rec) {
    return rec.header.type;
  };

  fn format_DebugEntry(ref auto entry) {
    return std::format("{}:{}:{}", entry.name, entry.line, entry.discrim);
  };
}

enum RecordType : u32 {
  JIT_CODE_LOAD = 0,
  JIT_CODE_MOVE,
  JIT_CODE_DEBUG_INFO,
  JIT_CODE_CLOSE,
  JIT_CODE_UNWINDING_INFO,
};

struct Header {
  u32 magic;
  u32 version;
  u32 total_size;
  u32 elf_mach;
  u32 pad1;
  u32 pid;
  u64 timestamp;
  u64 flags;
};

struct RecordHeader {
  RecordType type;
  u32 total_size;
  u64 timestamp;
};

struct CodeLoad {
  u32 pid;
  u32 tid;
  Addr vma;
  Addr addr;
  u64 size;
  u64 index;
  Str name;
};

struct DebugEntry {
  Addr addr;
  u32 line;
  u32 discrim;
  Str name;
} [[format("impl::format_DebugEntry")]];

struct DebugInfo {
  Addr addr;
  u64 nr_entry;
  DebugEntry entries[nr_entry];
};

struct Record {
  RecordHeader header [[inline]];
  match (header.type) {
    (RecordType::JIT_CODE_LOAD): CodeLoad code_load [[inline, no_unique_address]];
    (RecordType::JIT_CODE_DEBUG_INFO): DebugInfo debug_info [[inline, no_unique_address]];
  }
  u8 raw[header.total_size - sizeof(RecordHeader)];
} [[format("impl::format_Record")]];

struct Jitdump {
  Header header;
  Record records[while(!std::mem::eof())];
};

Jitdump dump @ 0x00;
