#include "cpp_fix_codec/fix_builder.h"

struct Logon {
    constexpr const static char * msgType = "A";
    static void build(FixBuilder& fix) {
        fix.addField(98,0);
        fix.addField(108,60);
    }
};