#ifndef CALCULATOR_HPP
#define CALCULATOR_HPP

#include <string>
#include <variant>

struct CalcResult {
    bool success;
    double value;
    std::string error;
};

class Calculator {
public:
    static CalcResult evaluate(const std::string& expression);
};

#endif // CALCULATOR_HPP
