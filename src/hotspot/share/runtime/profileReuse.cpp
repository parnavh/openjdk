#include "runtime/profileReuse.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/classLoaderDataGraph.hpp"
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "code/nmethod.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.hpp"
#include "oops/method.hpp"
#include "oops/methodCounters.hpp"
#include "oops/methodData.hpp"
#include "runtime/globals.hpp"

FILE *ProfileReuse::_capture_file = nullptr;
ProfileTable *ProfileReuse::_table = nullptr;
bool ProfileReuse::_loaded = false;

void ProfileReuse::safe_copy(char *dst, const char *src, int max_len) {
  strncpy(dst, src, max_len - 1);
  dst[max_len - 1] = '\0';
}

static bool make_key(MethodKey &key, const char *cls, const char *mname,
                     const char *desc) {
  strncpy(key.className, cls, PR_MAX_NAME_LEN - 1);
  key.className[PR_MAX_NAME_LEN - 1] = '\0';
  strncpy(key.methodName, mname, PR_MAX_NAME_LEN - 1);
  key.methodName[PR_MAX_NAME_LEN - 1] = '\0';
  strncpy(key.descriptor, desc, PR_MAX_NAME_LEN - 1);
  key.descriptor[PR_MAX_NAME_LEN - 1] = '\0';
  return true;
}

void ProfileReuse::load() {
  _vm_start_ns = os::javaTimeNanos();
  _table = new (mtInternal) ProfileTable();

  if (ProfileReuseFile == nullptr)
    return;

  const char *path = ProfileReuseFile;

  FILE *f = fopen(path, "r");
  if (f == nullptr) {
    tty->print_cr("[ProfileReuse] no prior data found at %s, starting fresh",
                  path);
    _loaded = true;
    return;
  }

  char line[4096];
  int format_version = -1;
  bool tiered_at_capture = false;

  while (fgets(line, sizeof(line), f) != nullptr) {
    // Strip trailing newline.
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n')
      line[len - 1] = '\0';

    char *fields[16];
    int nfields = 0;
    char *saveptr = nullptr;
    char *tok = strtok_r(line, "\t", &saveptr);

    while (tok != nullptr && nfields < 16) {
      fields[nfields++] = tok;
      tok = strtok_r(nullptr, "\t", &saveptr);
    }

    if (nfields == 0)
      continue;

    if (strcmp(fields[0], "VERSION") == 0) {
      format_version = atoi(fields[1]);

      if (format_version != PROFILE_REUSE_FORMAT_VERSION) {
        tty->print_cr("[ProfileReuse] format version mismatch (file=%d, "
                      "expected=%d), ignoring file",
                      format_version, PROFILE_REUSE_FORMAT_VERSION);
        fclose(f);
        _loaded = true;
        return;
      }

      continue;
    }

    if (strcmp(fields[0], "TIERED") == 0) {
      tiered_at_capture = atoi(fields[1]) != 0;
      continue;
    }

    if (strcmp(fields[0], "METHOD") == 0 && nfields >= 7) {
      MethodKey key;
      make_key(key, fields[1], fields[2], fields[3]);

      bool created = false;
      MethodEntry *entry = _table->put_if_absent(key, &created);
      entry->method.invocationCount = atoi(fields[4]);
      entry->method.backedgeCount = atoi(fields[5]);
      entry->method.compLevel = atoi(fields[6]);
    } else if (strcmp(fields[0], "COUNTER") == 0 && nfields >= 6) {
      MethodKey key;
      make_key(key, fields[1], fields[2], fields[3]);

      bool created = false;
      MethodEntry *entry = _table->put_if_absent(key, &created);

      if (entry->counterCount < PR_MAX_COUNTERS_PER_METHOD) {
        CounterRecord &rec = entry->counters[entry->counterCount++];
        rec.bci = atoi(fields[4]);
        rec.tag = atoi(fields[5]);
        rec.cellCount = 0;

        if (nfields >= 7) {
          char *cell_saveptr = nullptr;
          char *cell_tok = strtok_r(fields[6], ",", &cell_saveptr);
          while (cell_tok != nullptr && rec.cellCount < PR_MAX_CELLS) {
            rec.cells[rec.cellCount++] = atol(cell_tok);
            cell_tok = strtok_r(nullptr, ",", &cell_saveptr);
          }
        }
      }
    } else if (strcmp(fields[0], "RECEIVER") == 0 && nfields >= 7) {
      MethodKey key;
      make_key(key, fields[1], fields[2], fields[3]);

      bool created = false;
      MethodEntry *entry = _table->put_if_absent(key, &created);

      if (entry->receiverCount < PR_MAX_RECEIVERS_PER_METHOD) {
        ReceiverRecord &rec = entry->receivers[entry->receiverCount++];
        rec.bci = atoi(fields[4]);
        rec.tag = atoi(fields[5]);
        rec.overflowCount = atoi(fields[6]);
        rec.rowCount = 0;

        if (nfields >= 8) {
          char *row_saveptr = nullptr;
          char *row_tok = strtok_r(fields[7], "|", &row_saveptr);
          while (row_tok != nullptr && rec.rowCount < PR_MAX_ROWS) {
            char *colon = strchr(row_tok, ':');
            if (colon != nullptr) {
              *colon = '\0';
              const char *name = row_tok;
              unsigned count = (unsigned)atol(colon + 1);

              ReceiverRow &row = rec.rows[rec.rowCount++];
              if (strcmp(name, "null") == 0) {
                row.receiverClass[0] = '\0';
              } else {
                safe_copy(row.receiverClass, name, PR_MAX_NAME_LEN);
              }
              row.count = count;
            }
            row_tok = strtok_r(nullptr, "|", &row_saveptr);
          }
        }
      }
    }
  }

  fclose(f);
  _loaded = true;

  tty->print_cr(
      "[ProfileReuse] load() done, %d methods loaded (captured with tiered=%d)",
      _table->number_of_entries(), tiered_at_capture ? 1 : 0);
}

MethodEntry *ProfileReuse::lookup(const char *className, const char *methodName,
                                  const char *descriptor) {
  if (_table == nullptr)
    return nullptr;

  MethodKey key;
  make_key(key, className, methodName, descriptor);
  return _table->get(key);
}

void ProfileReuse::dump() {
  capture_all();
  write_measurements();
}

void ProfileReuse::capture_all() {
  if (ProfileReuseFile == nullptr)
    return;

  const char *path = ProfileReuseFile;

  _capture_file = fopen(path, "w");
  if (_capture_file == nullptr) {
    tty->print_cr("[ProfileReuse] failed to open capture file: %s", path);
    return;
  }

  fprintf(_capture_file, "VERSION\t%d\n", PROFILE_REUSE_FORMAT_VERSION);
  fprintf(_capture_file, "TIERED\t%d\n", TieredCompilation ? 1 : 0);

  ClassLoaderDataGraph::classes_do(&ProfileReuse::collect_klass);

  fclose(_capture_file);
  _capture_file = nullptr;
  tty->print_cr("[ProfileReuse] capture_all() done, wrote %s", path);
}

static bool is_safe_generic_tag(int tag) {
  return tag == DataLayout::bit_data_tag ||
         tag == DataLayout::counter_data_tag ||
         tag == DataLayout::jump_data_tag ||
         tag == DataLayout::branch_data_tag ||
         tag == DataLayout::multi_branch_data_tag ||
         tag == DataLayout::ret_data_tag ||
         tag == DataLayout::arg_info_data_tag;
}

void ProfileReuse::collect_klass(Klass *k) {
  if (!k->is_instance_klass())
    return;
  InstanceKlass *ik = InstanceKlass::cast(k);

  if (ik->class_loader_data()->is_the_null_class_loader_data())
    return;
  if (ik->is_hidden())
    return;
  if (ik->class_loader_data()->is_platform_class_loader_data())
    return;

  const char *class_name = ik->name()->as_C_string();

  Array<Method *> *methods = ik->methods();
  for (int i = 0; i < methods->length(); i++) {
    Method *m = methods->at(i);

    MethodCounters *mcs = m->method_counters();
    if (mcs == nullptr)
      continue;

    int invocation_count = mcs->invocation_counter()->count();
    int backedge_count = mcs->backedge_counter()->count();

    bool has_compiled_code = (m->code() != nullptr);
    int comp_level = has_compiled_code ? m->code()->comp_level() : 0;
    bool has_mdo = (m->method_data() != nullptr);

    if (invocation_count == 0 && backedge_count == 0 && !has_mdo &&
        !has_compiled_code) {
      continue;
    }

    const char *method_name = m->name()->as_C_string();
    const char *descriptor = m->signature()->as_C_string();

    MethodRecord mrec;
    mrec.invocationCount = invocation_count;
    mrec.backedgeCount = backedge_count;
    mrec.compLevel = comp_level;
    mrec.write(_capture_file, class_name, method_name, descriptor);

    if (!has_mdo)
      continue;

    MethodData *mdo = m->method_data();
    ProfileData *pdata = mdo->first_data();

    while (mdo->is_valid(pdata)) {
      int tag = pdata->tag();
      int bci = pdata->bci();

      if (tag == DataLayout::parameters_type_data_tag) {
        // Deferred.

      } else if (tag == DataLayout::receiver_type_data_tag ||
                 tag == DataLayout::virtual_call_data_tag) {
        ReceiverTypeData *rdata = static_cast<ReceiverTypeData *>(pdata);

        ReceiverRecord rrec;
        rrec.bci = bci;
        rrec.tag = tag;
        rrec.overflowCount = rdata->count();

        uint row_limit = ReceiverTypeData::row_limit();
        if (row_limit > (uint)PR_MAX_ROWS)
          row_limit = PR_MAX_ROWS;
        rrec.rowCount = (int)row_limit;

        for (uint row = 0; row < row_limit; row++) {
          Klass *recv = rdata->receiver(row);
          if (recv != nullptr) {
            safe_copy(rrec.rows[row].receiverClass, recv->name()->as_C_string(),
                      PR_MAX_NAME_LEN);
          } else {
            rrec.rows[row].receiverClass[0] = '\0';
          }
          rrec.rows[row].count = rdata->receiver_count(row);
        }

        rrec.write(_capture_file, class_name, method_name, descriptor);

      } else if (is_safe_generic_tag(tag)) {
        CounterRecord crec;
        crec.bci = bci;
        crec.tag = tag;

        int cells = pdata->cell_count();
        if (cells > PR_MAX_CELLS)
          cells = PR_MAX_CELLS;
        crec.cellCount = cells;
        for (int c = 0; c < cells; c++) {
          crec.cells[c] = pdata->intptr_at_public(c);
        }

        crec.write(_capture_file, class_name, method_name, descriptor);
      }

      pdata = mdo->next_data(pdata);
    }
  }
}

void ProfileReuse::restore_method_data(Method *m, MethodEntry *entry) {
  MethodData *mdo = m->method_data();
  if (mdo == nullptr)
    return;

  Handle loader_handle(Thread::current(), m->method_holder()->class_loader());

  ProfileData *pdata = mdo->first_data();
  while (mdo->is_valid(pdata)) {
    int tag = pdata->tag();
    int bci = pdata->bci();

    if (tag == DataLayout::receiver_type_data_tag ||
        tag == DataLayout::virtual_call_data_tag) {
      ReceiverTypeData *rdata = static_cast<ReceiverTypeData *>(pdata);

      for (int i = 0; i < entry->receiverCount; i++) {
        ReceiverRecord &rec = entry->receivers[i];
        if (rec.bci == bci && rec.tag == tag) {
          uint row_limit = ReceiverTypeData::row_limit();
          for (uint row = 0; row < row_limit && (int)row < rec.rowCount;
               row++) {
            if (rec.rows[row].receiverClass[0] != '\0') {
              Symbol *class_sym =
                  SymbolTable::new_symbol(rec.rows[row].receiverClass);
              InstanceKlass *k = SystemDictionary::find_instance_klass(
                  Thread::current(), class_sym, loader_handle);

              if (k != nullptr) {
                rdata->set_receiver(row, k);
                rdata->set_receiver_count(row, rec.rows[row].count);
              }
              // else: not loaded under this loader (yet, or at all) — skip row.
            }
          }
          break;
        }
      }

    } else if (is_safe_generic_tag(tag)) {
      for (int i = 0; i < entry->counterCount; i++) {
        CounterRecord &rec = entry->counters[i];
        if (rec.bci == bci && rec.tag == tag) {
          int cells = pdata->cell_count();
          for (int c = 0; c < cells && c < rec.cellCount; c++) {
            pdata->set_intptr_at_public(c, rec.cells[c]);
          }
          break;
        }
      }
    }

    pdata = mdo->next_data(pdata);
  }
}

jlong ProfileReuse::_vm_start_ns = 0;

void ProfileReuse::record_tier_event(Method *m, int tier) {
  if (!ProfileReuseMeasureFile || _table == nullptr)
    return;

  InstanceKlass *ik = m->method_holder();
  if (ik->class_loader_data()->is_the_null_class_loader_data())
    return;
  if (ik->is_hidden())
    return;
  if (ik->class_loader_data()->is_platform_class_loader_data())
    return;

  MethodKey key;
  make_key(key, ik->name()->as_C_string(), m->name()->as_C_string(),
           m->signature()->as_C_string());

  bool created = false;
  MethodEntry *entry = _table->put_if_absent(key, &created);

  if (entry->tierEventCount < 8) {
    jlong elapsed = os::javaTimeNanos() - _vm_start_ns;
    entry->tierEvents[entry->tierEventCount].tier = tier;
    entry->tierEvents[entry->tierEventCount].elapsedNanos = elapsed;
    entry->tierEventCount++;
  }
}

void ProfileReuse::write_measurements() {
  if (!ProfileReuseMeasureFile)
    return;

  const char *path = ProfileReuseMeasureFile;
  FILE *f = fopen(path, "a");
  if (f == nullptr) {
    tty->print_cr("[ProfileReuse] failed to open measure file: %s", path);
    return;
  }

  _table->iterate_all([&](MethodKey &key, MethodEntry &entry) {
    for (int i = 0; i < entry.tierEventCount; i++) {
      fprintf(f, "%s\t%s\t%s\t%d\t%ld\n", key.className, key.methodName,
              key.descriptor, entry.tierEvents[i].tier,
              (long)entry.tierEvents[i].elapsedNanos);
    }
  });

  fclose(f);
  tty->print_cr("[ProfileReuse] write_measurements() done, wrote %s", path);
}
