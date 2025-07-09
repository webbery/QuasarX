#include "Bridge/CTP/CTPSymbol.h"
#include "Util/string_algorithm.h"
#include "Util/log.h"

namespace {
    static Map<String, char> object_code{
        {"ap", 1}, {"al", 2},
        {"cf", 3}, {"cj", 4}, {"cy", 5},
        {"fg", 6}, {"ho", 7}, {"ic", 8},
        {"if", 9}, {"ih", 10}, {"im", 11},
        {"io", 12}, {"jr", 13}, {"lr", 14},
        {"ma", 15}, {"mo", 16}, {"oi", 17},
        {"pf", 18}, {"pk", 19}, {"pm", 20},
        {"pr", 21}, {"px", 22}, {"ri", 23},
        {"rm", 24}, {"rs", 25}, {"sa", 26},
        {"sf", 27}, {"sh", 28}, {"sm", 29},
        {"sr", 30}, {"t", 31}, {"ta", 32},
        {"tf", 33}, {"tl", 34}, {"ts", 35},
        {"ur", 36}, {"wh", 37}, {"zc", 38},
        {"a", 39}, {"ag", 40}, {"ao", 41},
        {"au", 42}, {"b", 43}, {"bb", 44},
        {"bc", 45}, {"br", 46}, {"bu", 47},
        {"c", 48}, {"cs", 49}, {"cu", 50},
        {"eb", 51}, {"ec", 51}, {"eg", 52},
        {"fb", 53}, {"fu", 54}, {"hc", 55},
        {"i", 56}, {"j", 57}, {"jd", 58},
        {"jm", 59}, {"l", 60}, {"lc", 61},
        {"lh", 62}, {"lu", 63}, {"m", 64},
        {"ni", 65}, {"nr", 66}, {"p", 67},
        {"pb", 68}, {"pg", 69}, {"pp", 70},
        {"rb", 71}, {"rr", 72}, {"zn", 73},
        {"si", 74}, {"ru", 75}, {"ss", 76},
        {"sp", 77}, {"v", 78},  { "y", 79 },
        {"wr", 80}, {"sn", 81}, {"sc", 82},
        {"ps", 83}, {"lg", 84}
    };

    static Map<String, ExchangeName> exchange_map{
        {"ap", MT_Zhengzhou}, {"al", MT_ShanghaiFuture},
        {"cf", MT_Zhengzhou}, {"cj", MT_Zhengzhou}, {"cy", MT_Zhengzhou},
        {"fg", MT_Zhengzhou}, {"ho", MT_Zhongjin}, {"ic", MT_Zhongjin},
        {"if", MT_Zhongjin}, {"ih", MT_Zhongjin}, {"im", MT_Zhongjin},
        {"io", MT_Zhongjin}, {"jr", MT_Zhengzhou}, {"lr", MT_Zhengzhou},
        {"ma", MT_Zhengzhou}, {"mo", MT_Zhongjin}, {"oi", MT_Zhengzhou},
        {"pf", MT_Zhengzhou}, {"pk", MT_Zhengzhou}, {"pm", MT_Zhengzhou},
        {"pr", MT_Zhengzhou}, {"px", MT_Zhengzhou}, {"ri", MT_Zhengzhou},
        {"rm", MT_Zhengzhou}, {"rs", MT_Zhengzhou}, {"sa", MT_Zhengzhou},
        {"sf", MT_Zhengzhou}, {"sh", MT_Zhengzhou}, {"sm", MT_Zhengzhou},
        {"sr", MT_Zhengzhou}, {"t", MT_Zhongjin}, {"ta", MT_Zhengzhou},
        {"tf", MT_Zhongjin}, {"tl", MT_Zhongjin}, {"ts", MT_Zhongjin},
        {"ur", MT_Zhengzhou}, {"wh", MT_Zhengzhou}, {"zc", MT_Zhengzhou},
        {"a", MT_Dalian}, {"ag", MT_ShanghaiFuture}, {"ao", MT_ShanghaiFuture},
        {"au", MT_ShanghaiFuture}, {"b", MT_Dalian}, {"bb", MT_Dalian},
        {"bc", MT_ShanghaiEng}, {"br", MT_ShanghaiFuture}, {"bu", MT_ShanghaiFuture},
        {"c", MT_Dalian}, {"cs", MT_Dalian}, {"cu", MT_ShanghaiFuture},
        {"eb", MT_Dalian}, {"ec", MT_ShanghaiEng}, {"eg", MT_Dalian},
        {"fb", MT_Dalian}, {"fu", MT_ShanghaiFuture}, {"hc", MT_ShanghaiFuture},
        {"i", MT_Dalian}, {"j", MT_Dalian}, {"jd", MT_Dalian},
        {"jm", MT_Dalian}, {"l", MT_Dalian}, {"lc", MT_Guangzhou},
        {"lh", MT_Dalian}, {"lu", MT_ShanghaiEng}, {"m", MT_Dalian},
        {"ni", MT_ShanghaiFuture}, {"nr", MT_ShanghaiFuture}, {"p", MT_Dalian},
        {"pb", MT_ShanghaiFuture}, {"pg", MT_Dalian}, {"pp", MT_Dalian},
        {"rb", MT_ShanghaiFuture}, {"rr", MT_Dalian},{"zn", MT_ShanghaiFuture},
        {"si", MT_Guangzhou}, {"ru", MT_ShanghaiFuture}, { "ss", MT_ShanghaiFuture},
        {"sp", MT_ShanghaiFuture}, {"v", MT_Dalian}, {"y", MT_Dalian}, {"wr", MT_ShanghaiFuture},
        {"sn", MT_ShanghaiFuture}, {"sc", MT_ShanghaiEng}, 
        {"ps", MT_Guangzhou}, {"lg", MT_Dalian}
    };
}
CTPSymbol::CTPSymbol(const String& symbol):_symbol(symbol) {

}

CTPSymbol::operator symbol_t() const {
    symbol_t t{contract_type::stock};
    Vector<String> tokens;
    String last_token;
    char prev_is_word = 0;
    //printf("Symbol: %s\n", _symbol.c_str());
    for (auto itr = _symbol.begin(); itr != _symbol.end(); ++itr) {
        char c = *itr;
        if (c == '_' || c == '-') {
            tokens.push_back(std::move(last_token));
            prev_is_word = 0;
            continue;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
            if (prev_is_word == 1) {
                tokens.emplace_back(std::move(last_token));
            }
            prev_is_word = 2;
            last_token.push_back(c);
            continue;
        }
        if (prev_is_word == 2) {
            last_token = to_lower(last_token);
            tokens.emplace_back(std::move(last_token));
        }
        prev_is_word = 1;
        last_token.push_back(c);
    }
    if (!last_token.empty()) {
        tokens.emplace_back(std::move(last_token));
    }
    t._opt = object_code[tokens.front()];
    if (t._opt == 0) {
        WARN("Object {} is new item", tokens.front());
    }
    if (tokens.size() > 2) {
        String type = tokens[2];
        if (type == "c") {
            t._type = contract_type::call;
        }
        else if (type == "p") {
            t._type = contract_type::put;
        }
    }
    else {
        t._type = contract_type::future;
    }
    switch (t._type)
    {
    case contract_type::future:
        t._symbol = atol(tokens[1].c_str());
        break;
    case contract_type::put: // put
    case contract_type::call: // call
        t._year = atoi(tokens[1].substr(0, tokens[1].size() - 2).c_str());
        t._month = atoi(tokens[1].substr(tokens[1].size() - 2, 2).c_str());
        t._price = atoi(tokens[3].c_str())/100;
        break;
    default:
        break;
    }
    t._exchange = exchange_map[tokens.front()];

    return t;
}

String CTPObjectName(int type) {
    static bool initial = false;
    static Map<int, String> reverse_map;
    if (!initial) {
        initial = true;
        for (auto& item: object_code) {
            reverse_map[item.second] = item.first;
        }
    }
    return reverse_map[type];
}

ExchangeName GetExchangeName(const String& object) {
    return exchange_map[object];
}
