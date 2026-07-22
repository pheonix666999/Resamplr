#include "SmokeScenario.h"

#include <iostream>

int main() {
    const auto result = padflow::runSmokeScenario();
    std::cout << result.diagnostics << '\n';
    return result.succeeded ? 0 : 1;
}
