#include "cpu65816.hpp"

//====================================================================
// 4-way opcode dispatch, ported from the higan/bsnes WDC65816 core
// (instruction.cpp). M and X independently select 8/16-bit behavior;
// the single 256-entry template table (instruction.hpp) is expanded
// four times via macros.
//====================================================================

namespace snes {

auto Cpu65816::instruction() -> void {
  #define opA(id, name, ...) case id: return instruction##name(__VA_ARGS__);
  if(MF) {
    #define opM(id, name, ...) case id: return instruction##name##8(__VA_ARGS__);
    #define m(name) &Cpu65816::algorithm##name##8
    if(XF) {
      #define opX(id, name, ...) case id: return instruction##name##8(__VA_ARGS__);
      #define x(name) &Cpu65816::algorithm##name##8
      #include "instruction.hpp"
      #undef opX
      #undef x
    } else {
      #define opX(id, name, ...) case id: return instruction##name##16(__VA_ARGS__);
      #define x(name) &Cpu65816::algorithm##name##16
      #include "instruction.hpp"
      #undef opX
      #undef x
    }
    #undef opM
    #undef m
  } else {
    #define opM(id, name, ...) case id: return instruction##name##16(__VA_ARGS__);
    #define m(name) &Cpu65816::algorithm##name##16
    if(XF) {
      #define opX(id, name, ...) case id: return instruction##name##8(__VA_ARGS__);
      #define x(name) &Cpu65816::algorithm##name##8
      #include "instruction.hpp"
      #undef opX
      #undef x
    } else {
      #define opX(id, name, ...) case id: return instruction##name##16(__VA_ARGS__);
      #define x(name) &Cpu65816::algorithm##name##16
      #include "instruction.hpp"
      #undef opX
      #undef x
    }
    #undef opM
    #undef m
  }
  #undef opA
}

}  // namespace snes