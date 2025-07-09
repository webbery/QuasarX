#pragma once
#include <string>

#define CONTRACT_NAME   "Name"
#define CONTRACT_CODE   "Code"
#define CONTRACT_SHARP  "Sharp Ratio"
#define CONTRACT_RETURN "Return"
#define CONTRACT_STD    "Sigma"
#define CONTRACT_Delta  "Delta"

class Contract {
public:
  virtual ~Contract() {}

  virtual bool Init(const std::string& path, int prepare_count) = 0;
};


//class Option :public Contract {
//public:
//  virtual bool Init(const std::string& path) = 0;
//};
//
//class Future :public Contract {
//public:
//  virtual bool Init(const std::string& path) = 0;
//};