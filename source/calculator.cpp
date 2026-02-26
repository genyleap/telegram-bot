#include "calculator.hpp"
#include <cmath>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <cctype>

class Parser {
    std::string str;
    size_t pos = 0;

    char peek() { return pos < str.length() ? str[pos] : 0; }
    char get() { return pos < str.length() ? str[pos++] : 0; }

    void skipWhitespace() {
        while (std::isspace(peek())) pos++;
    }

    double number() {
        skipWhitespace();
        size_t start = pos;
        if (peek() == '-') get();
        while (std::isdigit(peek()) || peek() == '.') get();
        std::string s = str.substr(start, pos - start);
        if (s.empty() || s == "-") throw std::runtime_error("Expected number at position " + std::to_string(start));
        return std::stod(s);
    }

    double factor() {
        skipWhitespace();
        if (peek() == '(') {
            get();
            double res = expression();
            skipWhitespace();
            if (get() != ')') throw std::runtime_error("Expected ')'");
            return res;
        }
        return number();
    }

    double term() {
        double res = factor();
        while (true) {
            skipWhitespace();
            char c = peek();
            if (c == '*') { get(); res *= factor(); }
            else if (c == '/') {
                get();
                double den = factor();
                if (den == 0) throw std::runtime_error("Division by zero");
                res /= den;
            }
            else break;
        }
        return res;
    }

    double expression() {
        double res = term();
        while (true) {
            skipWhitespace();
            char c = peek();
            if (c == '+') { get(); res += term(); }
            else if (c == '-') { get(); res -= term(); }
            else break;
        }
        return res;
    }

public:
    double parse(const std::string& s) {
        str = s;
        pos = 0;
        double res = expression();
        skipWhitespace();
        if (pos < str.length()) throw std::runtime_error("Unexpected character at position " + std::to_string(pos));
        return res;
    }
};

CalcResult Calculator::evaluate(const std::string& expression) {
    try {
        Parser p;
        double result = p.parse(expression);

        if (std::isnan(result)) {
            return {false, 0.0, "Result is not a number"};
        }

        if (std::isinf(result)) {
            return {false, 0.0, "Result is infinite"};
        }

        return {true, result, ""};
    } catch (const std::exception& e) {
        return {false, 0.0, e.what()};
    }
}
