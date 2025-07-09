#include "Util/string_algorithm.h"
#include <algorithm>
#include <iostream>
#include <sstream>
#ifdef WIN32
#include <tchar.h>
#include <locale>
#include <codecvt>
#else
#include <iconv.h>
#endif
#include <fstream>
#include <strstream>

std::string to_lower(const std::string& str) {
    std::string ret(str);
    std::transform(str.begin(), str.end(), ret.begin(), ::tolower);
    return ret;
}

std::string to_upper(const std::string& str) {
  std::string ret(str);
  std::transform(str.begin(), str.end(), ret.begin(), ::toupper);
  return ret;
}

std::string trim(const std::string& s) {
  auto start = s.begin();
  auto end = s.end();
  while (
#ifdef WIN32
    _istspace(*start)
#else
    std::isspace(*start)
#endif
    && start != end) {
    start++;
  }
  do {
    end--;
  } while (
#ifdef WIN32
    _istspace(*start)
#else
    std::isspace(*end)
#endif
    && start > end);
  return std::string(start, end + 1);
}

std::string to_dot_string(double v, char dot) {
  auto str = std::to_string(v);
  auto index = str.find_first_of('.');
  if (index == std::string::npos) {
    index = str.size();
  }

  std::string finalResult;
  int count = 0;
  for (int i = index - 1; i >= 0; --i) {
    finalResult += str[i];
    ++count;
    if (count == 3 && i > 0) {
      finalResult += ',';
      count = 0;
    }
  }
  std::reverse(finalResult.begin(), finalResult.end());
  return finalResult + str.substr(index);
}

std::string format_symbol(const std::string& symbol) {
  if (symbol.size() < 6) {
    int i = 6 - symbol.size();
    std::string fill(i, '0');
    return fill + symbol;
  }
  return symbol;
}

std::string to_gbk(const std::string& str) {
#ifdef _WIN32
  std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  std::wstring tmp_wstr = conv.from_bytes(str);
  // GBK locale name in windows
  const char* GBK_LOCALE_NAME = ".936";
  std::wstring_convert<std::codecvt_byname<wchar_t, char, mbstate_t>> convert(new std::codecvt_byname<wchar_t, char, mbstate_t>(GBK_LOCALE_NAME));
  return convert.to_bytes(tmp_wstr);
#else
  // std::wstring_convert<std::codecvt_utf8<wchar_t>> conv;
  // std::wstring tmp_wstr = conv.from_bytes(str);
  // // GBK locale name in windows
  // const char* GBK_LOCALE_NAME = "zh_CN.GBK";
  // // const char* GBK_LOCALE_NAME = "zh_CN.gb18030";
  // std::wstringstream wss;
  // wss.imbue(std::locale(wss.getloc(),
  //             new std::codecvt_byname<wchar_t, char, std::mbstate_t>(GBK_LOCALE_NAME)));

  // wss << str.data();
  // std::wcout << wss.str();
    // iconv_t cd = iconv_open("GBK//IGNORE", "UTF-8");
    // if (cd == (iconv_t)-1) {
    //     throw std::runtime_error("Failed to initialize iconv");
    // }

    // size_t in_bytes_left = str.size();
    // char* in_buf = const_cast<char*>(str.data());

    // // 输出缓冲区初始大小为输入大小的 2 倍（GBK 最大字符占 2 字节）
    // std::vector<char> out_buf(str.size() * 2);
    // size_t out_bytes_left = out_buf.size();
    // char* out_ptr = out_buf.data();

    // if (iconv(cd, &in_buf, &in_bytes_left, &out_ptr, &out_bytes_left) == (size_t)-1) {
    //     iconv_close(cd);
    //     throw std::runtime_error("Conversion failed");
    // }

    // iconv_close(cd);
    // return std::string(out_buf.data(), out_ptr - out_buf.data());
    return str;
#endif
}

String to_base64(const String& bin) {
    static const std::string base64_chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    size_t in_len = bin.size();
    const char* bytes_to_encode = bin.data();
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];  // store 3 byte of bytes_to_encode
    unsigned char char_array_4[4];  // store encoded character to 4 bytes

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);  // get three bytes (24 bits)
        if (i == 3) {
            // eg. we have 3 bytes as ( 0100 1101, 0110 0001, 0110 1110) --> (010011, 010110, 000101, 101110)
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2; // get first 6 bits of first byte,
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4); // get last 2 bits of first byte and first 4 bit of second byte
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6); // get last 4 bits of second byte and first 2 bits of third byte
            char_array_4[3] = char_array_3[2] & 0x3f; // get last 6 bits of third byte

            for (i = 0; (i < 4); i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for (j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while ((i++ < 3))
            ret += '=';
    }

    return ret;

}
