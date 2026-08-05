#pragma once
#include "memory/allStatic.hpp"
#include "oops/method.hpp"
#include "utilities/hashTable.hpp"
#include <cstdio>
#include <cstring>

class Klass;

static const int PROFILE_REUSE_FORMAT_VERSION = 1;

static const int PR_MAX_NAME_LEN = 256;
static const int PR_MAX_CELLS = 16;
static const int PR_MAX_ROWS = 8;
static const int PR_MAX_COUNTERS_PER_METHOD = 64;
static const int PR_MAX_RECEIVERS_PER_METHOD = 32;

// ---------------------------------------------------------------------------
struct MethodRecord {
  int invocationCount;
  int backedgeCount;
  int compLevel;

  void write(FILE *f, const char *cls, const char *mname, const char *desc,
             const jlong id) const {
    fprintf(f, "%ld\tMETHOD\t%s\t%s\t%s\t%d\t%d\t%d\n", id, cls, mname, desc,
            invocationCount, backedgeCount, compLevel);
  }
};

struct CounterRecord {
  int bci;
  int tag;
  long cells[PR_MAX_CELLS];
  int cellCount;

  void write(FILE *f, const char *cls, const char *mname, const char *desc,
             const jlong id) const {
    fprintf(f, "%ld\tCOUNTER\t%s\t%s\t%s\t%d\t%d\t", id, cls, mname, desc, bci,
            tag);
    for (int i = 0; i < cellCount; i++) {
      fprintf(f, "%s%ld", i == 0 ? "" : ",", cells[i]);
    }
    fprintf(f, "\n");
  }
};

struct ReceiverRow {
  char receiverClass[PR_MAX_NAME_LEN]; // empty = "null"
  unsigned int count;
};

struct ReceiverRecord {
  int bci;
  int tag;
  int overflowCount;
  ReceiverRow rows[PR_MAX_ROWS];
  int rowCount;

  void write(FILE *f, const char *cls, const char *mname, const char *desc,
             const jlong id) const {
    fprintf(f, "%ld\tRECEIVER\t%s\t%s\t%s\t%d\t%d\t%d\t", id, cls, mname, desc,
            bci, tag, overflowCount);
    for (int i = 0; i < rowCount; i++) {
      const char *name =
          rows[i].receiverClass[0] != '\0' ? rows[i].receiverClass : "null";
      fprintf(f, "%s%s:%u", i == 0 ? "" : "|", name, rows[i].count);
    }
    fprintf(f, "\n");
  }
};

struct MethodKey {
  char className[PR_MAX_NAME_LEN];
  char methodName[PR_MAX_NAME_LEN];
  char descriptor[PR_MAX_NAME_LEN];
};

inline unsigned hash_method_key(MethodKey const &k) {
  unsigned h = 0;
  for (const char *p = k.className; *p; p++)
    h = h * 31 + (unsigned char)*p;
  for (const char *p = k.methodName; *p; p++)
    h = h * 31 + (unsigned char)*p;
  for (const char *p = k.descriptor; *p; p++)
    h = h * 31 + (unsigned char)*p;
  return h;
}

inline bool equals_method_key(MethodKey const &a, MethodKey const &b) {
  return strcmp(a.className, b.className) == 0 &&
         strcmp(a.methodName, b.methodName) == 0 &&
         strcmp(a.descriptor, b.descriptor) == 0;
}

struct TierEvent {
  int tier;
  jlong elapsedNanos;
};

struct MethodEntry {
  MethodRecord method;

  CounterRecord counters[PR_MAX_COUNTERS_PER_METHOD];
  int counterCount = 0;

  ReceiverRecord receivers[PR_MAX_RECEIVERS_PER_METHOD];
  int receiverCount = 0;

  TierEvent tierEvents[8];
  int tierEventCount = 0;
};

using ProfileTable = HashTable<MethodKey, MethodEntry, 1024, AnyObj::C_HEAP,
                               mtInternal, hash_method_key, equals_method_key>;

// ---------------------------------------------------------------------------
class ProfileReuse : public AllStatic {
private:
  static FILE *_capture_file;
  static ProfileTable *_table;
  static bool _loaded;
  static jlong _vm_start_ns;

  static void collect_klass(Klass *k);
  static void safe_copy(char *dst, const char *src, int max_len);

public:
  static void load();
  static void dump();
  static void capture_all();
  static MethodEntry *lookup(const char *className, const char *methodName,
                             const char *descriptor);
  static void restore_method_data(Method *m, MethodEntry *entry);
  static void record_tier_event(Method *m, int tier);
  static void write_measurements();
};
