// MIT License
// Copyright (c) 2026 dbjwhs

// TCP Calculator Client - Demonstrates basic TCP RPC
// Usage: tcp_calculator_client [host] [port]

#include <song/song.hpp>
#include "calculator.hpp"
#include <iostream>
#include <cstdlib>

using namespace song;
using namespace song::calculator;

int main(int argc, char* argv[]) {
    const char* host = "127.0.0.1";
    u16 port = 12345;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = static_cast<u16>(std::atoi(argv[2]));

    std::cout << "Connecting to " << host << ":" << port << "...\n";

    try {
        ServiceManager mgr;
        mgr.register_remote_service("calc", host, port, kService_Calculator);
        auto conn = mgr.connect("calc");

        CalculatorProxy calc(conn);

        std::cout << "\n=== TCP Calculator Demo ===\n\n";

        std::cout << "add(10, 20) = " << calc.add(10, 20) << "\n";
        std::cout << "subtract(100, 42) = " << calc.subtract(100, 42) << "\n";
        std::cout << "multiply(6, 7) = " << calc.multiply(6, 7) << "\n";

        auto div = calc.divide(17, 5);
        std::cout << "divide(17, 5) = " << div.quotient << " remainder " << div.remainder << "\n";

        std::cout << "factorial(10) = " << calc.factorial(10) << "\n";
        std::cout << "sum([1,2,3,4,5]) = " << calc.sum({1, 2, 3, 4, 5}) << "\n";

        std::cout << "\nSuccess!\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
