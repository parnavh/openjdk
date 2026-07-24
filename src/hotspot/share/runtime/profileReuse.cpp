#include "runtime/profileReuse.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/classLoaderDataGraph.hpp"
#include "code/nmethod.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/klass.hpp"
#include "oops/method.hpp"
#include "oops/methodCounters.hpp"
#include "oops/methodData.hpp"
#include "utilities/ostream.hpp"

FILE *ProfileReuse::_capture_file = nullptr;

void ProfileReuse::capture_all() {
  const char *path =
      "profile_reuse.data"; // TODO: replace with -XX:ProfileReuseFile=

  _capture_file = fopen(path, "w");
  if (_capture_file == nullptr) {
    tty->print_cr("[ProfileReuse] failed to open capture file: %s", path);
    return;
  }

  /*
   * count means different things in different scenarios:
   *    METHOD: invocation count
   *    RECEIVER: overflow count - when there are more than 2 types at the site
   *              more accurately type overflow count
   */
  fprintf(_capture_file, "recordType\tclassName\tmethodName\tdescriptor\tbci\tt"
                         "ag\tcount\textra\n");

  ClassLoaderDataGraph::classes_do(&ProfileReuse::collect_klass);

  fclose(_capture_file);
  _capture_file = nullptr;

  tty->print_cr("[ProfileReuse] capture_all() done, wrote %s", path);
}

void ProfileReuse::collect_klass(Klass *k) {
  if (!k->is_instance_klass()) {
    return;
  }
  InstanceKlass *ik = InstanceKlass::cast(k);

  // Skip bootstrap-loaded classes (java.*, jdk.internal.*, etc.)
  if (ik->class_loader_data()->is_the_null_class_loader_data()) {
    return;
  }

  if (ik->is_hidden()) {
    return;
  }

  if (ik->class_loader_data()->is_platform_class_loader_data()) {
    return;
  }

  const char *class_name = ik->name()->as_C_string();

  Array<Method *> *methods = ik->methods();
  for (int i = 0; i < methods->length(); i++) {
    Method *m = methods->at(i);

    MethodCounters *mcs = m->method_counters();
    if (mcs == nullptr) {
      continue; // never invoked
    }

    int invocation_count = mcs->invocation_counter()->count();
    int backedge_count = mcs->backedge_counter()->count();

    bool has_compiled_code = (m->code() != nullptr);
    int comp_level = has_compiled_code ? m->code()->comp_level() : 0;

    bool has_mdo_check = (m->method_data() != nullptr);

    if (invocation_count == 0 && backedge_count == 0 && !has_mdo_check &&
        !has_compiled_code) {
      continue;
    }

    const char *method_name = m->name()->as_C_string();
    const char *descriptor = m->signature()->as_C_string();

    fprintf(_capture_file,
            "METHOD\t%s\t%s\t%s\t-\t-\t%d\tbackedge=%d,compile_level=%d\n",
            class_name, method_name, descriptor, invocation_count,
            backedge_count, comp_level);

    MethodData *mdo = m->method_data();
    if (mdo == nullptr) {
      continue;
    }

    ProfileData *pdata = mdo->first_data();
    while (mdo->is_valid(pdata)) {
      int tag = pdata->tag();
      int bci = pdata->bci();

      if (tag == DataLayout::parameters_type_data_tag) {
        // Deferred for now
      } else if (tag == DataLayout::receiver_type_data_tag ||
                 tag == DataLayout::virtual_call_data_tag) {
        ReceiverTypeData *rdata = static_cast<ReceiverTypeData *>(pdata);

        stringStream extra;
        uint row_limit = ReceiverTypeData::row_limit();
        for (uint row = 0; row < row_limit; row++) {
          Klass *recv = rdata->receiver(row);
          uint count = rdata->receiver_count(row);
          if (row > 0)
            extra.print("|");
          if (recv != nullptr) {
            extra.print("%s:%u", recv->name()->as_C_string(), count);
          } else {
            extra.print("null:0");
          }
        }

        fprintf(_capture_file, "RECEIVER\t%s\t%s\t%s\t%d\t%d\t%d\t%s\n",
                class_name, method_name, descriptor, bci, tag, rdata->count(),
                extra.as_string());

      } else {
        // Generic counter-cell dump for BranchData/CounterData/JumpData/etc.
        stringStream extra;
        int cells = pdata->cell_count();
        for (int c = 0; c < cells; c++) {
          if (c > 0)
            extra.print(",");
          extra.print("%ld", (long)pdata->intptr_at_public(c));
        }

        fprintf(_capture_file, "COUNTER\t%s\t%s\t%s\t%d\t%d\t-\t%s\n",
                class_name, method_name, descriptor, bci, tag,
                extra.as_string());
      }

      pdata = mdo->next_data(pdata);
    }
  }
}
