#include "runtime/profileReuse.hpp"
#include "utilities/ostream.hpp"

int ProfileReuse::_methods_seen_before_load = 0;
bool ProfileReuse::_loaded = false;

void ProfileReuse::load() {
  tty->print_cr("[ProfileReuse] load() called, methods_seen_so_far=%d",
                _methods_seen_before_load);
  _loaded = true;
}

void ProfileReuse::note_method_seen() { _methods_seen_before_load++; }
