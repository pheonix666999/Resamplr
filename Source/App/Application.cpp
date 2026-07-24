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
    jobs_ = std::make_unique<BackgroundJobSystem>();
    assets_ = std::make_unique<SampleAssetRegistry>();
    runtime_ = std::make_unique<AudioRuntime>();
    publisher_ = std::make_unique<PlaybackStatePublisher>(runtime_->engine(), *assets_);
    input_ = std::make_unique<InputRouter>(*controller_, runtime_->engine());
    preview_ = std::make_unique<SamplePreviewController>(*controller_, runtime_->preview());
    const auto audioResult = runtime_->initialise(controller_->project().state().audio);
    juce::ignoreUnused(audioResult);
    mainWindow_ = std::make_unique<MainWindow>(getApplicationName(), *controller_, *jobs_, *assets_,
                                               *runtime_, *publisher_, *input_, *preview_);
}

void Application::shutdown() {
    mainWindow_.reset();
    if (jobs_ != nullptr)
        jobs_->shutdown();
    if (runtime_ != nullptr)
        runtime_->close();
    if (publisher_ != nullptr)
        publisher_->clearWhenAudioIsStopped();
    preview_.reset();
    input_.reset();
    runtime_.reset();
    publisher_.reset();
    if (assets_ != nullptr)
        assets_->clear();
    assets_.reset();
    jobs_.reset();
    controller_.reset();
}

void Application::systemRequestedQuit() {
    quit();
}

void Application::anotherInstanceStarted(const juce::String& commandLine) {
    juce::ignoreUnused(commandLine);
}
} // namespace padflow
