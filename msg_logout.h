#include "cpp_fix_codec/fix_builder.h"

struct Logout {
    constexpr const static char * msgType = "5";
    static void build(FixBuilder& fix,std::string text) {
        fix.addField(58,text);
    }
};