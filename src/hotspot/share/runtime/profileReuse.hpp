#pragma once
#include "memory/allStatic.hpp"
#include <cstdio>

class Klass;

// Bump this whenever the on-disk layout of any record changes.
static const int PROFILE_REUSE_FORMAT_VERSION = 1;

static const int PR_MAX_NAME_LEN = 256;
static const int PR_MAX_CELLS = 16; // max bound for MultiBranchData etc.
static const int PR_MAX_ROWS = 8;   // max bound for TypeProfileWidth

// ---------------------------------------------------------------------------
struct MethodRecord {
  char className[PR_MAX_NAME_LEN];
  char methodName[PR_MAX_NAME_LEN];
  char descriptor[PR_MAX_NAME_LEN];
  int invocationCount;
  int backedgeCount;
  int compLevel;

  void write(FILE *f) const {
    fprintf(f, "METHOD\t%s\t%s\t%s\t%d\t%d\t%d\n", className, methodName,
            descriptor, invocationCount, backedgeCount, compLevel);
  }
};

// ---------------------------------------------------------------------------
struct CounterRecord {
  char className[PR_MAX_NAME_LEN];
  char methodName[PR_MAX_NAME_LEN];
  char descriptor[PR_MAX_NAME_LEN];
  int bci;
  int tag;
  long cells[PR_MAX_CELLS];
  int cellCount;

  void write(FILE *f) const {
    fprintf(f, "COUNTER\t%s\t%s\t%s\t%d\t%d\t", className, methodName,
            descriptor, bci, tag);
    for (int i = 0; i < cellCount; i++) {
      fprintf(f, "%s%ld", i == 0 ? "" : ",", cells[i]);
    }
    fprintf(f, "\n");
  }
};

// ---------------------------------------------------------------------------
struct ReceiverRow {
  char receiverClass[PR_MAX_NAME_LEN]; // empty string means "null"/unfilled
  unsigned int count;
};

struct ReceiverRecord {
  char className[PR_MAX_NAME_LEN];
  char methodName[PR_MAX_NAME_LEN];
  char descriptor[PR_MAX_NAME_LEN];
  int bci;
  int tag;
  int overflowCount;
  ReceiverRow rows[PR_MAX_ROWS];
  int rowCount;

  void write(FILE *f) const {
    fprintf(f, "RECEIVER\t%s\t%s\t%s\t%d\t%d\t%d\t", className, methodName,
            descriptor, bci, tag, overflowCount);
    for (int i = 0; i < rowCount; i++) {
      const char *name =
          rows[i].receiverClass[0] != '\0' ? rows[i].receiverClass : "null";
      fprintf(f, "%s%s:%u", i == 0 ? "" : "|", name, rows[i].count);
    }
    fprintf(f, "\n");
  }
};

// ---------------------------------------------------------------------------
class ProfileReuse : public AllStatic {
private:
  static FILE *_capture_file;
  static void collect_klass(Klass *k);

  static void safe_copy(char *dst, const char *src, int max_len);

public:
  static void load();
  static void capture_all();
};
