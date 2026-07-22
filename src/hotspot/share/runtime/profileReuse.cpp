#include "runtime/profileReuse.hpp"
#include "classfile/classLoaderData.hpp"
#include "classfile/classLoaderDataGraph.hpp"
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

  fprintf(_capture_file, "className\tmethodName\tdescriptor\tinvocationCount\tb"
                         "ackedgeCount\thasMethodData\tmdoRecords\n");

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

  Array<Method *> *methods = ik->methods();
  for (int i = 0; i < methods->length(); i++) {
    Method *m = methods->at(i);

    MethodCounters *mcs = m->method_counters();
    if (mcs == nullptr) {
      continue; // never invoked
    }

    int invocation_count = mcs->invocation_counter()->count();
    int backedge_count = mcs->backedge_counter()->count();
    bool has_mdo = (m->method_data() != nullptr);

    if (invocation_count == 0) {
      continue;
    }

    stringStream mdo_stream;
    bool first_record = true;

    if (has_mdo) {
      MethodData *mdo = m->method_data();
      ProfileData *pdata = mdo->first_data();

      while (mdo->is_valid(pdata)) {
        int tag = pdata->tag();

        if (tag == DataLayout::parameters_type_data_tag) {
          // Deferred for now
        } else if (tag == DataLayout::receiver_type_data_tag ||
                   tag == DataLayout::virtual_call_data_tag) {
          ReceiverTypeData *rdata = static_cast<ReceiverTypeData *>(pdata);

          if (!first_record)
            mdo_stream.print(";");

          first_record = false;

          mdo_stream.print("%d:%d:%d:", pdata->bci(), tag, rdata->count());

          uint row_limit = ReceiverTypeData::row_limit();
          for (uint row = 0; row < row_limit; row++) {
            Klass *recv = rdata->receiver(row);
            uint count = rdata->receiver_count(row);
            if (row > 0)
              mdo_stream.print(",");
            if (recv != nullptr) {
              mdo_stream.print("%s=%u", recv->name()->as_C_string(), count);
            } else {
              mdo_stream.print("null=0");
            }
          }

        } else {
          // generic counter-cell dump for BranchData/CounterData/JumpData/etc.
          if (!first_record)
            mdo_stream.print(";");
          first_record = false;

          mdo_stream.print("%d:%d:", pdata->bci(), tag);

          int cells = pdata->cell_count();
          for (int c = 0; c < cells; c++) {
            mdo_stream.print("%s%ld", c == 0 ? "" : ",",
                             (long)pdata->intptr_at_public(c));
          }
        }

        pdata = mdo->next_data(pdata);
      }
    }

    fprintf(_capture_file, "%s\t%s\t%s\t%d\t%d\t%d\t%s\n",
            ik->name()->as_C_string(), m->name()->as_C_string(),
            m->signature()->as_C_string(), invocation_count, backedge_count,
            has_mdo ? 1 : 0, mdo_stream.as_string());
  }
}
