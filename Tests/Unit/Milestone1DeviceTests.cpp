#include "Audio/AudioRuntime.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace padflow {
class Milestone1DeviceTests final : public juce::UnitTest {
  public:
    Milestone1DeviceTests() : juce::UnitTest("Milestone 1 device runtime", "PadFlow") {}

    void runTest() override {
        AudioRuntime runtime;
        runtime.engine().prepare(48000.0);

        beginTest("DEVICE-M1-002 no output device path is safe silence");
        runtime.audioDeviceIOCallbackWithContext(nullptr, 0, nullptr, 0, 64, {});
        expectEquals(static_cast<int>(runtime.engine().activeVoiceCount()), 0);

        beginTest("REGRESSION-M1-002 device errors defer panic to the callback");
        std::array<float, 256U> pcm{};
        pcm.fill(0.5F);
        PlaybackSnapshot snapshot;
        snapshot.pads[0].layers[0].enabled = true;
        snapshot.pads[0].layers[0].asset = {pcm.data(), static_cast<std::uint64_t>(pcm.size()), 1U,
                                            48000.0};
        snapshot.pads[0].playbackMode = PlaybackMode::gate;
        runtime.engine().publishSnapshot(&snapshot);
        expect(runtime.engine().enqueue(
            {AudioCommandType::triggerPad, 0U, 1U, static_cast<float>(100U)}));
        std::array<float, 8U> primingLeft{};
        std::array<float, 8U> primingRight{};
        runtime.engine().processBlock(primingLeft.data(), primingRight.data(), primingLeft.size());
        expect(runtime.engine().activeVoiceCount() > 0U);
        runtime.audioDeviceError("synthetic device failure");
        expect(runtime.engine().activeVoiceCount() > 0U);
        runtime.audioDeviceIOCallbackWithContext(nullptr, 0, nullptr, 0, 64, {});
        expectEquals(static_cast<int>(runtime.engine().activeVoiceCount()), 0);

        beginTest("DEVICE-M1-003 bounded finite test tone needs no input");
        std::array<float, 257U> left{};
        std::array<float, 257U> right{};
        std::array<float*, 2U> outputs{left.data(), right.data()};
        runtime.setTestToneEnabled(true);
        runtime.audioDeviceIOCallbackWithContext(nullptr, 0, outputs.data(), 2,
                                                 static_cast<int>(left.size()), {});
        bool nonSilent = false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            expect(std::isfinite(left[index]) && std::isfinite(right[index]));
            expect(std::abs(left[index]) <= 0.1001F && std::abs(right[index]) <= 0.1001F);
            nonSilent = nonSilent || std::abs(left[index]) > 0.00001F;
        }
        expect(nonSilent);
        runtime.setTestToneEnabled(false);
        expect(!runtime.isTestToneEnabled());
        runtime.audioDeviceIOCallbackWithContext(nullptr, 0, outputs.data(), 2,
                                                 static_cast<int>(left.size()), {});
        for (std::size_t index = 0; index < left.size(); ++index)
            expect(left[index] == 0.0F && right[index] == 0.0F);

        beginTest("DEVICE-M1-004 CPU/dropout snapshots and reset are safe without hardware");
        const auto beforeReset = runtime.status();
        expect(std::isfinite(beforeReset.cpuUsage));
        runtime.resetDropoutCount();
        const auto afterReset = runtime.status();
        expectEquals(static_cast<juce::int64>(afterReset.dropoutCount), juce::int64{0});

        beginTest("AUDIO-M1-025 callback chunks oversized buffers without allocation");
        std::array<float, 16384U> largeLeft{};
        std::array<float, 16384U> largeRight{};
        std::array<float*, 2U> largeOutputs{largeLeft.data(), largeRight.data()};
        runtime.setTestToneEnabled(true);
        runtime.audioDeviceIOCallbackWithContext(nullptr, 0, largeOutputs.data(), 2,
                                                 static_cast<int>(largeLeft.size()), {});
        for (std::size_t index = 0; index < largeLeft.size(); ++index)
            expect(std::isfinite(largeLeft[index]) && std::isfinite(largeRight[index]));
        runtime.close();
    }
};

static Milestone1DeviceTests milestone1DeviceTests;
} // namespace padflow
