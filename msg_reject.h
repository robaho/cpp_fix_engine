#include "fix_builder.h"

struct Logout {
    constexpr const static char * msgType = "3";
    static void build(FixBuilder& fix,int refSeqNum,std::string text) {
        fix.addField(45,refSeqNum);
        fix.addField(58,text);
    }
};