#pragma once
#include "script_vm.h"
namespace dokodemo::recovered {
struct GameValueArrayResult { int32_t selected=0;uint32_t rows=0,predicates=0,writes=0;std::vector<uint32_t> indices; };
// Native typed-domain counterparts of7773C/7A64C. Same live aliases,ordered
// predicates/actions and results;owned C++ strings replace source heap words.
// Unrecovered selectors and conversions throw instead of returning success.
GameValueArrayResult GameValueArray97(script::Vm&,const std::vector<script::Value>&);
GameValueArrayResult GameValueArray98(script::Vm&,const std::vector<script::Value>&);
GameValueArrayResult GameValueArray96(script::Vm&,const std::vector<script::Value>&);
} // namespace dokodemo::recovered
