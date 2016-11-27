/* -*-mode:c++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

#ifndef TC_RULE_HH
#define TC_RULE_HH

#include <vector>
#include <string>

#include "child_process.hh"

void add_tc_rule( std::string device, uint64_t delay, uint64_t rate, uint64_t loss = 0);

#endif /* TC_RULE_HH */
