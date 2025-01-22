#include "fix_builder.h"
#include "fixed.h"

struct MassQuote {
    constexpr const static char * msgType = "i";
    template <int nPlaces=7> static void build(FixBuilder& fix,const std::string_view& symbol,Fixed<nPlaces> bidPrice,Fixed<nPlaces> bidQty,Fixed<nPlaces> offerPrice,Fixed<nPlaces> offerQty) {
        fix.addField(117,"MyQuoteID");
        fix.addField(301,2); // request ack on each quote
        fix.addField(296,1);// 1 QuoteSet
        fix.addField(302,"Set1");// QuoteSet ID
        fix.addField(304,1);// total number of quote entries in the set across all messages
        fix.addField(295,1);// number of quote entries in this set
        fix.addField(299,"EntryID1");// number of quote entries in this set
        fix.addField(55,symbol);
        fix.addField(132,bidPrice);
        fix.addField(133,offerPrice);
        fix.addField(134,bidQty);
        fix.addField(135,offerQty);
    }
};

struct MassQuoteAck {
    constexpr const static char * msgType = "b";
    static void build(FixBuilder& fix,const std::string_view& quoteId, int quoteStatus) {
        fix.addField(117,quoteId);
        fix.addField(297,quoteStatus);
    }
};
