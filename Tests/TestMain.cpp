#include <juce_core/juce_core.h>

#include <iostream>

namespace {
class ConsoleTestRunner final : public juce::UnitTestRunner {
  public:
    void logMessage(const juce::String& message) override {
        std::cout << message << '\n';
    }
};
} // namespace

int main() {
    ConsoleTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.setPassesAreLogged(false);
    runner.runAllTests(0x504144464C4F57LL);

    int failureCount = 0;
    for (int index = 0; index < runner.getNumResults(); ++index)
        if (const auto* result = runner.getResult(index); result != nullptr)
            failureCount += result->failures;

    std::cout << "PadFlow tests: " << (failureCount == 0 ? "PASS" : "FAIL") << '\n';
    return failureCount == 0 ? 0 : 1;
}
