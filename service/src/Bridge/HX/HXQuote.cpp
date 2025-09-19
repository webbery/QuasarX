#include "Bridge/HX/HXQuote.h"
#include "Util/system.h"
#include "Bridge/exchange.h"

using namespace TORALEV1API;

HXQuateSpi::HXQuateSpi(CTORATstpXMdApi* api) {
    
}

HXQuateSpi::~HXQuateSpi() {
    if (_sock.id != 0) {
        nng_close(_sock);
    }
}

bool HXQuateSpi::Init() {
  return Publish(URI_RAW_QUOTE, _sock);
}

void HXQuateSpi::OnRtnMarketData(TORALEV1API::CTORATstpMarketDataField *pMarketDataField) {
    auto name = pMarketDataField->SecurityName;
    auto symb = to_symbol(name);
    QuoteInfo& info = _tickers[symb];
    {
        std::unique_lock<std::mutex> lock(_mutex);
        // info._time = FromStr(std::to_string(pMarketDataField->data_time/1000), "%Y%m%d%H%M%S");
        info._symbol = symb;
        info._open = pMarketDataField->OpenPrice;
        info._close = pMarketDataField->PreClosePrice;
        info._volume = pMarketDataField->Volume;
        info._value = pMarketDataField->Turnover;
        info._high = pMarketDataField->HighestPrice;
        info._low = pMarketDataField->LowestPrice;

        info._bidPrice[0] = pMarketDataField->BidPrice1;
        info._bidPrice[1] = pMarketDataField->BidPrice2;
        info._bidPrice[2] = pMarketDataField->BidPrice3;
        info._bidPrice[3] = pMarketDataField->BidPrice4;
        info._bidPrice[4] = pMarketDataField->BidPrice5;
        info._askPrice[0] = pMarketDataField->AskPrice1;
        info._askPrice[1] = pMarketDataField->AskPrice2;
        info._askPrice[2] = pMarketDataField->AskPrice3;
        info._askPrice[3] = pMarketDataField->AskPrice4;
        info._askPrice[4] = pMarketDataField->AskPrice5;
        info._bidVolume[0] = pMarketDataField->BidVolume1;
        info._bidVolume[1] = pMarketDataField->BidVolume2;
        info._bidVolume[2] = pMarketDataField->BidVolume3;
        info._bidVolume[3] = pMarketDataField->BidVolume4;
        info._bidVolume[4] = pMarketDataField->BidVolume5;
        info._askVolume[0] = pMarketDataField->AskVolume1;
        info._askVolume[1] = pMarketDataField->AskVolume2;
        info._askVolume[2] = pMarketDataField->AskVolume3;
        info._askVolume[3] = pMarketDataField->AskVolume4;
        info._askVolume[4] = pMarketDataField->AskVolume5;
        // if (strcmp(market_data->ticker, "000001") == 0) {
        //   LOG("XTP update");
        // }
    }
    yas::shared_buffer buf = yas::save<flags>(info);
    if (0 != nng_send(_sock, buf.data.get(), buf.size, 0)) {
        printf("send quote message fail.\n");
        return;
    }
}
