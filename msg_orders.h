#include "fix_builder.h"
#include "fixed.h"

enum class OrderType {
    Market=1,
    Limit=2
};

enum class OrderSide {
    Buy=1,
    Sell=2
};

enum class OrderStatus {
    New=0,
    PartiallyFilled=1,
    Filled=2,
    Canceled=4,
    Rejected=8
};

enum class ExecType {
    New=0,
    PartiallyFilled=1,
    Filled=2,
    Canceled=4,
    Rejected=8
};

struct NewOrderSingle {
    constexpr const static char * msgType = "D";
    template <int nPlaces=7> static void build(FixBuilder& fix,const std::string_view& symbol,const OrderType& orderType,const OrderSide& side, Fixed<nPlaces> price,Fixed<nPlaces> quantity,const std::string_view orderId) {
        fix.addField(55,symbol);
        fix.addField(11,orderId);
        fix.addField(38,quantity);
        fix.addField(44,price);
        fix.addField(54,int(side));
        fix.addField(40,int(orderType));
    }
};

struct OrderCancelRequest {
    constexpr const static char * msgType = "F";
    template <int nPlaces=7> static void build(FixBuilder& fix,const std::string_view& symbol,const OrderType& orderType,const OrderSide& side, Fixed<nPlaces> price,Fixed<nPlaces> quantity,const std::string_view orderId) {
        fix.addField(55,symbol);
        fix.addField(11,orderId);
        fix.addField(41,orderId);
        fix.addField(54,int(side));
    }
};

struct OrderCancelReject {
    constexpr const static char * msgType = "9";
    template <int nPlaces=7> static void build(FixBuilder& fix,const long exchangeId,const std::string_view orderId,const OrderStatus& status) {
        fix.addField(37,exchangeId);
        fix.addField(11,orderId);
        fix.addField(41,orderId);
        fix.addField(434,1);
        fix.addField(39,int(status));
    }
};

struct ExecutionReport {
    constexpr const static char * msgType = "b";
    template <int nPlaces=7> static void build(FixBuilder& fix,const std::string_view& orderId,const std::string_view& symbol, const OrderSide& side, Fixed<nPlaces> lastPrice,Fixed<nPlaces> lastQty,Fixed<nPlaces> cumQty,Fixed<nPlaces> avgPrice, Fixed<nPlaces> remaining,const long exchangeId,const ExecType& execType,const std::string_view& execId,const OrderStatus& status) {
        fix.addField(37,exchangeId);
        fix.addField(11,orderId);
        fix.addField(17,execId);
        fix.addField(20,int(execType));
        fix.addField(39,int(status));
        fix.addField(55,symbol);
        fix.addField(54,int(side));
        fix.addField(31, lastPrice);
        fix.addField(32, lastQty);
        fix.addField(151, remaining);
        fix.addField(14, cumQty);
        fix.addField(6, avgPrice);
    }
};
