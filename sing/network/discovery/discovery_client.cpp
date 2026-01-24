// MIT License
// Copyright (c) 2026 dbjwhs

// Discovery Client - Demonstrates mDNS service discovery
// Usage: discovery_client [service_type]
// Default service_type: "testcalc" (matches discovery_service)

#include <song/song.hpp>
#include "calculator.hpp"
#include <iostream>
#include <cstdlib>

using namespace song;
using namespace song::calculator;

int main(int argc, char* argv[]) {
    std::string service_type = "testcalc";

    if (argc > 1) service_type = argv[1];

    std::cout << "Discovering services of type: " << service_type << "\n";
    std::cout << "(mDNS type: " << Discovery::make_service_type(service_type) << ")\n\n";

    try {
        auto discovery = create_discovery();
        if (!discovery) {
            std::cerr << "Error: Discovery not supported on this platform\n";
            return 1;
        }

        if (!discovery->is_available()) {
            std::cerr << "Error: mDNS service not available\n";
            return 1;
        }

        std::cout << "Searching for services (5 second timeout)...\n";

        auto services = discovery->discover(service_type, std::chrono::seconds(5));

        if (services.empty()) {
            std::cout << "No services found.\n";
            return 0;
        }

        std::cout << "\nFound " << services.size() << " service(s):\n\n";

        for (const auto& svc : services) {
            std::cout << "  Name: " << svc.name << "\n";
            std::cout << "  Host: " << svc.host << ":" << svc.port << "\n";
            std::cout << "\n";
        }

        // Connect to the first discovered service
        const auto& first = services[0];
        std::cout << "Connecting to " << first.name << " at " << first.host << ":" << first.port << "...\n";

        ServiceManager mgr;
        mgr.register_remote_service("discovered", first.host, first.port, kService_Calculator);
        auto conn = mgr.connect("discovered");

        CalculatorProxy calc(conn);

        std::cout << "\n=== Calculator via Discovery ===\n\n";

        std::cout << "add(100, 200) = " << calc.add(100, 200) << "\n";
        std::cout << "multiply(12, 12) = " << calc.multiply(12, 12) << "\n";
        std::cout << "factorial(7) = " << calc.factorial(7) << "\n";

        std::cout << "\nSuccess!\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
