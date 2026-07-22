#include "Application.h"

#include "ProductInfo.h"

namespace padflow {
const juce::String Application::getApplicationName() {
    return juce::String{product::name.data()};
}

const juce::String Application::getApplicationVersion() {
    return juce::String{product::version.data()};
}

bool Application::moreThanOneInstanceAllowed() {
    return true;
}

void Application::initialise(const juce::String& commandLine) {
    juce::ignoreUnused(commandLine);
    controller_ = std::make_unique<ApplicationController>();
    mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
}

void Application::shutdown() {
    mainWindow_.reset();
    controller_.reset();
}

void Application::systemRequestedQuit() {
    quit();
}

void Application::anotherInstanceStarted(const juce::String& commandLine) {
    juce::ignoreUnused(commandLine);
}
} // namespace padflow
