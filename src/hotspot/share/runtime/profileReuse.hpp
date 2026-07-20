#pragma once
#include "memory/allStatic.hpp"

class ProfileReuse : public AllStatic {
private:
  static int _methods_seen_before_load;
  static bool _loaded;

public:
  static void load();
  static void note_method_seen();
};
