#pragma once
#include "memory/allStatic.hpp"
#include <cstdio>

class Klass;

class ProfileReuse : public AllStatic {
private:
  static FILE *_capture_file;
  static void collect_klass(Klass *k); // callback for classes_do

public:
  static void load();
  static void capture_all();
};
