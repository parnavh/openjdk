#include "runtime/profileReuse.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/classLoaderDataGraph.hpp"
#include "code/nmethod.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.hpp"
#include "oops/method.hpp"
#include "oops/methodCounters.hpp"
#include "oops/methodData.hpp"

FILE *ProfileReuse::_capture_file = nullptr;

void ProfileReuse::safe_copy(char *dst, const char *src, int max_len) {
  size_t len = MIN2(strlen(src), (size_t)max_len - 1);
  memcpy(dst, src, len);
  dst[len] = '\0';
}

void ProfileReuse::capture_all() {
  const char *path = "profile_reuse.data"; // TODO: -XX:ProfileReuseFile=

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

    MethodRecord mrec;
    safe_copy(mrec.className, ik->name()->as_C_string(), PR_MAX_NAME_LEN);
    safe_copy(mrec.methodName, m->name()->as_C_string(), PR_MAX_NAME_LEN);
    safe_copy(mrec.descriptor, m->signature()->as_C_string(), PR_MAX_NAME_LEN);
    mrec.invocationCount = invocation_count;
    mrec.backedgeCount = backedge_count;
    mrec.compLevel = comp_level;
    mrec.write(_capture_file);

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
        safe_copy(rrec.className, mrec.className, PR_MAX_NAME_LEN);
        safe_copy(rrec.methodName, mrec.methodName, PR_MAX_NAME_LEN);
        safe_copy(rrec.descriptor, mrec.descriptor, PR_MAX_NAME_LEN);
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

        rrec.write(_capture_file);

      } else {
        CounterRecord crec;
        safe_copy(crec.className, mrec.className, PR_MAX_NAME_LEN);
        safe_copy(crec.methodName, mrec.methodName, PR_MAX_NAME_LEN);
        safe_copy(crec.descriptor, mrec.descriptor, PR_MAX_NAME_LEN);
        crec.bci = bci;
        crec.tag = tag;

        int cells = pdata->cell_count();
        if (cells > PR_MAX_CELLS)
          cells = PR_MAX_CELLS;
        crec.cellCount = cells;
        for (int c = 0; c < cells; c++) {
          crec.cells[c] = pdata->intptr_at_public(c);
        }

        crec.write(_capture_file);
      }

      pdata = mdo->next_data(pdata);
    }
  }
}
