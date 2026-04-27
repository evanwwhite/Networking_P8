#pragma once

// AI assistance note: AI was used to help structure these networking tests and
// clarify expected packet behavior. The tests were adapted to this kernel and
// verified by the team.

#include "ext2.h"

void net_run_selected_tests(StrongRef<Ext2> fs);
