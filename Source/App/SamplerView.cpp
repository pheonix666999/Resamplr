#include "SamplerView.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace padflow {
namespace {
constexpr auto backgroundColour = 0xff15191fU;
constexpr auto panelColour = 0xff222a33U;
constexpr auto raisedColour = 0xff2b3540U;
constexpr auto borderColour = 0xff3b4957U;
constexpr auto textColour = 0xffd8e1e8U;
constexpr auto mutedTextColour = 0xff91a0abU;
constexpr auto tealColour = 0xff50c8bbU;
constexpr auto amberColour = 0xffe1aa55U;
constexpr auto coralColour = 0xffe27868U;

juce::String bankName(const std::size_t index) {
    return juce::String::charToString(static_cast<juce::juce_wchar>('A' + static_cast<int>(index)));
}

void styleButton(juce::Button& button) {
    button.setColour(juce::TextButton::buttonColourId, juce::Colour{raisedColour});
    button.setColour(juce::TextButton::buttonOnColourId, juce::Colour{tealColour}.darker(0.35F));
    button.setColour(juce::TextButton::textColourOffId, juce::Colour{textColour});
    button.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
}

void styleLabel(juce::Label& label, const juce::Justification justification) {
    label.setColour(juce::Label::textColourId, juce::Colour{textColour});
    label.setJustificationType(justification);
}
} // namespace

void SamplerView::PadButton::mouseDown(const juce::MouseEvent& event) {
    if (onPadMouseDown)
        onPadMouseDown(event);
    juce::TextButton::mouseDown(event);
}

void SamplerView::PadButton::mouseUp(const juce::MouseEvent& event) {
    if (onPadMouseUp)
        onPadMouseUp(event);
    juce::TextButton::mouseUp(event);
}

SamplerView::SamplerView(ApplicationController& controller, BackgroundJobSystem& jobs,
                         SampleAssetRegistry& assets, AudioRuntime& runtime,
                         PlaybackStatePublisher& publisher, InputRouter& input,
                         SamplePreviewController& preview)
    : controller_(controller), jobs_(jobs), assets_(assets), runtime_(runtime),
      publisher_(publisher), input_(input), preview_(preview) {
    setTitle("PadFlow playable sampler");
    setDescription("Four banks of sixteen playable sample pads and a selected-pad editor");
    setWantsKeyboardFocus(true);
    addKeyListener(this);
    configureControls();
    publisher_.publish(controller_.project().state());
    lastSeenRevision_ = controller_.project().revision();
    refreshAll();
    startTimerHz(20);
}

SamplerView::~SamplerView() {
    stopTimer();
    removeKeyListener(this);
    if (fileChooser_ != nullptr)
        fileChooser_.reset();
    input_.panic();
    juce::ignoreUnused(preview_.stop());
}

void SamplerView::configureControls() {
    productLabel_.setText("PadFlow", juce::dontSendNotification);
    productLabel_.setFont(juce::FontOptions{27.0F, juce::Font::bold});
    productLabel_.setColour(juce::Label::textColourId, juce::Colour{tealColour});
    productLabel_.setTitle("PadFlow product name");
    addAndMakeVisible(productLabel_);

    for (auto* label : {&projectLabel_, &modifiedLabel_, &cpuLabel_, &audioStateLabel_,
                        &selectedPadLabel_, &sampleNameLabel_, &deviceStatusLabel_,
                        &midiStatusLabel_, &memoryStatusLabel_, &operationStatusLabel_}) {
        styleLabel(*label, juce::Justification::centredLeft);
        addAndMakeVisible(*label);
    }
    projectLabel_.setTitle("Current project name");
    modifiedLabel_.setColour(juce::Label::textColourId, juce::Colour{amberColour});
    cpuLabel_.setTitle("Audio CPU usage");
    audioStateLabel_.setTitle("Audio device status");
    operationStatusLabel_.setTitle("Last operation result");

    for (auto* button :
         {&newButton_, &openButton_, &saveButton_, &undoButton_, &redoButton_, &audioButton_,
          &midiButton_, &importButton_, &clearLayerButton_, &clearPadButton_, &auditionButton_}) {
        styleButton(*button);
        addAndMakeVisible(*button);
        button->setTitle(button->getButtonText());
    }
    newButton_.setComponentID("new-project");
    openButton_.setComponentID("open-project");
    saveButton_.setComponentID("save-project");
    audioButton_.setComponentID("audio-settings");
    midiButton_.setComponentID("midi-settings");

    newButton_.onClick = [this] {
        input_.panic();
        assets_.clear();
        controller_.createEmptyProject();
        modified_ = false;
        publishModel(false);
        setOperationMessage("Created a new project", false);
    };
    openButton_.onClick = [this] { showOpenChooser(); };
    saveButton_.onClick = [this] { showSaveChooser(); };
    undoButton_.onClick = [this] {
        if (controller_.undo()) {
            publishModel(true);
            setOperationMessage("Undo", false);
        }
    };
    redoButton_.onClick = [this] {
        if (controller_.redo()) {
            publishModel(true);
            setOperationMessage("Redo", false);
        }
    };
    audioButton_.onClick = [this] { showAudioSettings(); };
    midiButton_.onClick = [this] { showMidiSettings(); };

    for (std::size_t bank = 0; bank < bankButtons_.size(); ++bank) {
        auto& button = bankButtons_[bank];
        button.setButtonText("Bank " + bankName(bank));
        button.setClickingTogglesState(false);
        button.setComponentID("bank-" + juce::String{static_cast<int>(bank)});
        button.setTitle("Select bank " + bankName(bank));
        styleButton(button);
        button.onClick = [this, bank] { juce::ignoreUnused(selectBank(bank)); };
        addAndMakeVisible(button);
    }

    for (std::size_t pad = 0; pad < padButtons_.size(); ++pad) {
        auto& button = padButtons_[pad];
        button.setComponentID("pad-" + juce::String{static_cast<int>(pad)});
        button.setTitle("Playable pad " + juce::String{static_cast<int>(pad + 1U)});
        styleButton(button);
        button.onPadMouseDown = [this, pad](const juce::MouseEvent& event) {
            if (event.mods.isPopupMenu()) {
                showPadMenu(selectedGlobalPad() / padsPerBank * padsPerBank + pad);
                return;
            }
            juce::ignoreUnused(selectPad(selectedGlobalPad() / padsPerBank * padsPerBank + pad));
            juce::ignoreUnused(input_.mouseDown(pad));
        };
        button.onPadMouseUp = [this, pad](const juce::MouseEvent& event) {
            if (!event.mods.isPopupMenu())
                juce::ignoreUnused(input_.mouseUp(pad));
        };
        addAndMakeVisible(button);
    }

    padNameEditor_.setComponentID("pad-name");
    padNameEditor_.setTitle("Selected pad name");
    padNameEditor_.setSelectAllWhenFocused(true);
    padNameEditor_.addListener(this);
    keyboardEditor_.setComponentID("keyboard-mapping");
    keyboardEditor_.setTitle("Computer keyboard assignment");
    keyboardEditor_.setInputRestrictions(1);
    keyboardEditor_.addListener(this);
    for (auto* editor : {&padNameEditor_, &keyboardEditor_}) {
        editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour{raisedColour});
        editor->setColour(juce::TextEditor::textColourId, juce::Colour{textColour});
        editor->setColour(juce::TextEditor::outlineColourId, juce::Colour{borderColour});
        addAndMakeVisible(*editor);
    }

    layerSelector_.setComponentID("layer-selector");
    layerSelector_.setTitle("Selected sample layer");
    for (int layer = 1; layer <= static_cast<int>(minimumLayersPerPad); ++layer)
        layerSelector_.addItem("Layer " + juce::String{layer}, layer);
    layerSelector_.setSelectedId(1, juce::dontSendNotification);
    layerSelector_.onChange = [this] { refreshEditor(); };
    addAndMakeVisible(layerSelector_);

    configureEditorControl(gainSlider_, "Gain decibels", -60.0, 12.0, 0.1);
    configureEditorControl(panSlider_, "Pan", -1.0, 1.0, 0.01);
    configureEditorControl(coarseSlider_, "Coarse pitch semitones", -48.0, 48.0, 1.0);
    configureEditorControl(fineSlider_, "Fine pitch cents", -100.0, 100.0, 0.1);
    configureEditorControl(attackSlider_, "Attack seconds", 0.0, 10.0, 0.001);
    configureEditorControl(decaySlider_, "Decay seconds", 0.0, 10.0, 0.001);
    configureEditorControl(sustainSlider_, "Sustain level", 0.0, 1.0, 0.01);
    configureEditorControl(releaseSlider_, "Release seconds", 0.0, 10.0, 0.001);
    configureEditorControl(chokeSlider_, "Choke group", 0.0, 16.0, 1.0);
    configureEditorControl(voicesSlider_, "Maximum local voices", 1.0, 128.0, 1.0);
    configureEditorControl(midiNoteSlider_, "MIDI note", 0.0, 127.0, 1.0);

    for (auto* slider :
         {&gainSlider_, &panSlider_, &coarseSlider_, &fineSlider_, &attackSlider_, &decaySlider_,
          &sustainSlider_, &releaseSlider_, &chokeSlider_, &voicesSlider_})
        slider->onDragEnd = [this] { applyParameterControls(); };
    midiNoteSlider_.onDragEnd = [this] { applyMappings(); };

    playbackModeBox_.setComponentID("playback-mode");
    playbackModeBox_.setTitle("Playback mode");
    playbackModeBox_.addItem("One-shot", 1);
    playbackModeBox_.addItem("Gate", 2);
    playbackModeBox_.addItem("Toggle", 3);
    playbackModeBox_.onChange = [this] { applyParameterControls(); };
    polyphonyBox_.setComponentID("polyphony-mode");
    polyphonyBox_.setTitle("Polyphony mode");
    polyphonyBox_.addItem("Poly", 1);
    polyphonyBox_.addItem("Mono", 2);
    polyphonyBox_.onChange = [this] { applyParameterControls(); };
    addAndMakeVisible(playbackModeBox_);
    addAndMakeVisible(polyphonyBox_);

    importButton_.onClick = [this] { showImportChooser(selectedGlobalPad()); };
    clearLayerButton_.onClick = [this] { clearSelectedLayer(); };
    clearPadButton_.onClick = [this] {
        const auto result = controller_.clearPad(selectedGlobalPad());
        if (result.wasOk()) {
            publishModel(true);
            setOperationMessage("Cleared selected pad", false);
        } else {
            setOperationMessage(result.getErrorMessage(), true);
        }
    };
    auditionButton_.onClick = [this] { auditionSelectedLayer(); };
}

void SamplerView::configureEditorControl(juce::Slider& slider, juce::String name,
                                         const double minimum, const double maximum,
                                         const double interval) {
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
    slider.setRange(minimum, maximum, interval);
    slider.setTitle(name);
    slider.setName(name);
    slider.setColour(juce::Slider::trackColourId, juce::Colour{tealColour});
    slider.setColour(juce::Slider::backgroundColourId, juce::Colour{raisedColour});
    slider.setColour(juce::Slider::textBoxTextColourId, juce::Colour{textColour});
    addAndMakeVisible(slider);
}

void SamplerView::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour{backgroundColour});
    auto bounds = getLocalBounds().reduced(12);
    bounds.removeFromTop(58);
    bounds.removeFromBottom(42);
    auto editor = bounds.removeFromLeft(std::min(390, bounds.getWidth() / 3));
    bounds.removeFromLeft(12);
    graphics.setColour(juce::Colour{panelColour});
    graphics.fillRoundedRectangle(editor.toFloat(), 10.0F);
    graphics.fillRoundedRectangle(bounds.toFloat(), 10.0F);
    graphics.setColour(juce::Colour{borderColour});
    graphics.drawRoundedRectangle(editor.toFloat(), 10.0F, 1.0F);
    graphics.drawRoundedRectangle(bounds.toFloat(), 10.0F, 1.0F);
}

void SamplerView::resized() {
    auto bounds = getLocalBounds().reduced(12);
    auto top = bounds.removeFromTop(50);
    productLabel_.setBounds(top.removeFromLeft(118));
    projectLabel_.setBounds(top.removeFromLeft(150));
    modifiedLabel_.setBounds(top.removeFromLeft(18));
    top.removeFromLeft(8);
    for (auto* button : {&newButton_, &openButton_, &saveButton_, &undoButton_, &redoButton_}) {
        button->setBounds(top.removeFromLeft(62).reduced(2, 7));
    }
    audioButton_.setBounds(top.removeFromLeft(112).reduced(2, 7));
    midiButton_.setBounds(top.removeFromLeft(104).reduced(2, 7));
    cpuLabel_.setBounds(top.removeFromLeft(80));
    audioStateLabel_.setBounds(top);

    auto status = bounds.removeFromBottom(34);
    deviceStatusLabel_.setBounds(status.removeFromLeft(status.getWidth() / 4));
    midiStatusLabel_.setBounds(status.removeFromLeft(status.getWidth() / 3));
    memoryStatusLabel_.setBounds(status.removeFromLeft(status.getWidth() / 2));
    operationStatusLabel_.setBounds(status);
    bounds.removeFromBottom(8);

    auto editor = bounds.removeFromLeft(std::min(390, bounds.getWidth() / 3)).reduced(14);
    bounds.removeFromLeft(12);
    auto grid = bounds.reduced(14);

    selectedPadLabel_.setBounds(editor.removeFromTop(28));
    padNameEditor_.setBounds(editor.removeFromTop(30));
    editor.removeFromTop(6);
    layerSelector_.setBounds(editor.removeFromTop(28));
    sampleNameLabel_.setBounds(editor.removeFromTop(28));
    auto actions = editor.removeFromTop(32);
    importButton_.setBounds(actions.removeFromLeft(130).reduced(1));
    clearLayerButton_.setBounds(actions.removeFromLeft(104).reduced(1));
    auditionButton_.setBounds(actions.removeFromLeft(82).reduced(1));
    clearPadButton_.setBounds(editor.removeFromTop(28).removeFromLeft(100).reduced(1));
    editor.removeFromTop(6);

    for (auto* slider : {&gainSlider_, &panSlider_, &coarseSlider_, &fineSlider_, &attackSlider_,
                         &decaySlider_, &sustainSlider_, &releaseSlider_})
        slider->setBounds(editor.removeFromTop(28));
    auto modes = editor.removeFromTop(30);
    playbackModeBox_.setBounds(modes.removeFromLeft(modes.getWidth() / 2).reduced(1));
    polyphonyBox_.setBounds(modes.reduced(1));
    chokeSlider_.setBounds(editor.removeFromTop(28));
    voicesSlider_.setBounds(editor.removeFromTop(28));
    midiNoteSlider_.setBounds(editor.removeFromTop(28));
    keyboardEditor_.setBounds(editor.removeFromTop(28).removeFromLeft(100));

    auto banks = grid.removeFromTop(38);
    const auto bankWidth = banks.getWidth() / static_cast<int>(bankButtons_.size());
    for (auto& button : bankButtons_)
        button.setBounds(banks.removeFromLeft(bankWidth).reduced(3));
    grid.removeFromTop(8);
    const auto padWidth = grid.getWidth() / 4;
    const auto padHeight = grid.getHeight() / 4;
    for (std::size_t index = 0; index < padButtons_.size(); ++index) {
        const auto column = static_cast<int>(index % 4U);
        const auto row = static_cast<int>(index / 4U);
        padButtons_[index].setBounds(grid.getX() + column * padWidth + 4,
                                     grid.getY() + row * padHeight + 4, padWidth - 8,
                                     padHeight - 8);
    }
}

bool SamplerView::selectBank(const std::size_t bankIndex) {
    if (bankIndex >= padBankCount)
        return false;
    auto state = controller_.project().state().ui;
    state.selectedBank = static_cast<std::uint8_t>(bankIndex);
    if (controller_.setUiState(state).failed())
        return false;
    input_.refreshFromProject();
    lastSeenRevision_ = controller_.project().revision();
    refreshAll();
    grabKeyboardFocus();
    return true;
}

bool SamplerView::selectPad(const std::size_t globalPadIndex) {
    if (globalPadIndex >= totalPadCount)
        return false;
    auto state = controller_.project().state().ui;
    state.selectedBank = static_cast<std::uint8_t>(globalPadIndex / padsPerBank);
    state.selectedPad = static_cast<std::uint8_t>(globalPadIndex % padsPerBank);
    if (controller_.setUiState(state).failed())
        return false;
    input_.refreshFromProject();
    lastSeenRevision_ = controller_.project().revision();
    refreshAll();
    return true;
}

std::size_t SamplerView::selectedGlobalPad() const noexcept {
    const auto& state = controller_.project().state().ui;
    return static_cast<std::size_t>(state.selectedBank) * padsPerBank + state.selectedPad;
}

std::size_t SamplerView::visiblePadCount() const noexcept {
    return padButtons_.size();
}

void SamplerView::refreshAll() {
    refreshing_ = true;
    projectLabel_.setText(controller_.project().name(), juce::dontSendNotification);
    modifiedLabel_.setText(modified_ ? "*" : "", juce::dontSendNotification);
    undoButton_.setEnabled(controller_.canUndo());
    redoButton_.setEnabled(controller_.canRedo());
    refreshPads();
    refreshEditor();
    refreshStatus();
    refreshing_ = false;
    repaint();
}

void SamplerView::refreshPads() {
    const auto selectedBank = controller_.project().state().ui.selectedBank;
    const auto selectedPad = controller_.project().state().ui.selectedPad;
    for (std::size_t bank = 0; bank < bankButtons_.size(); ++bank)
        bankButtons_[bank].setToggleState(bank == selectedBank, juce::dontSendNotification);
    for (std::size_t local = 0; local < padButtons_.size(); ++local) {
        const auto global = static_cast<std::size_t>(selectedBank) * padsPerBank + local;
        const auto& pad = controller_.project().pad(global);
        int layers = 0;
        bool missing = false;
        for (const auto& layer : pad.layers)
            if (layer.enabled && layer.assetUuid.isNotEmpty()) {
                ++layers;
                const auto asset =
                    std::find_if(controller_.project().state().assets.begin(),
                                 controller_.project().state().assets.end(),
                                 [&](const auto& entry) { return entry.uuid == layer.assetUuid; });
                missing = missing ||
                          (asset != controller_.project().state().assets.end() && asset->missing);
            }
        auto text = juce::String{static_cast<int>(local + 1U)} + "\n" + pad.name + "\n";
        text += layers > 0 ? juce::String{layers} + " layer" + (layers == 1 ? "" : "s") : "Empty";
        padButtons_[local].setButtonText(text);
        auto colour = juce::Colour{pad.colourArgb};
        if (missing)
            colour = juce::Colour{coralColour}.darker(0.2F);
        if (local == selectedPad)
            colour = colour.brighter(0.32F);
        padButtons_[local].setColour(juce::TextButton::buttonColourId, colour.darker(0.32F));
        padButtons_[local].setToggleState(local == selectedPad, juce::dontSendNotification);
        padButtons_[local].setTooltip("MIDI " + juce::String{static_cast<int>(pad.midiNote)} +
                                      " / Key " + pad.keyboardKey);
    }
}

void SamplerView::refreshEditor() {
    const auto global = selectedGlobalPad();
    const auto& pad = controller_.project().pad(global);
    const auto bank = global / padsPerBank;
    const auto local = global % padsPerBank;
    selectedPadLabel_.setText("Bank " + bankName(bank) + " / Pad " +
                                  juce::String{static_cast<int>(local + 1U)},
                              juce::dontSendNotification);
    padNameEditor_.setText(pad.name, false);
    sampleNameLabel_.setText(selectedSampleName(), juce::dontSendNotification);
    const auto& parameters = pad.parameters;
    gainSlider_.setValue(parameters.gainDecibels, juce::dontSendNotification);
    panSlider_.setValue(parameters.pan, juce::dontSendNotification);
    coarseSlider_.setValue(parameters.coarseSemitones, juce::dontSendNotification);
    fineSlider_.setValue(parameters.fineCents, juce::dontSendNotification);
    attackSlider_.setValue(parameters.envelope.attackSeconds, juce::dontSendNotification);
    decaySlider_.setValue(parameters.envelope.decaySeconds, juce::dontSendNotification);
    sustainSlider_.setValue(parameters.envelope.sustainLevel, juce::dontSendNotification);
    releaseSlider_.setValue(parameters.envelope.releaseSeconds, juce::dontSendNotification);
    chokeSlider_.setValue(parameters.chokeGroup, juce::dontSendNotification);
    voicesSlider_.setValue(parameters.maximumVoices, juce::dontSendNotification);
    midiNoteSlider_.setValue(pad.midiNote, juce::dontSendNotification);
    keyboardEditor_.setText(pad.keyboardKey, false);
    playbackModeBox_.setSelectedId(static_cast<int>(parameters.playbackMode) + 1,
                                   juce::dontSendNotification);
    polyphonyBox_.setSelectedId(static_cast<int>(parameters.polyphonyMode) + 1,
                                juce::dontSendNotification);
}

void SamplerView::refreshStatus() {
    const auto status = runtime_.status();
    cpuLabel_.setText(juce::String{status.cpuUsage * 100.0, 1} + "% CPU",
                      juce::dontSendNotification);
    audioStateLabel_.setText(status.deviceOpen ? "Audio online" : "Audio unavailable",
                             juce::dontSendNotification);
    audioStateLabel_.setColour(juce::Label::textColourId,
                               juce::Colour{status.deviceOpen ? tealColour : coralColour});
    deviceStatusLabel_.setText(
        "Audio: " + (status.deviceName.isNotEmpty() ? status.deviceName : "None") + "  " +
            juce::String{status.sampleRate, 0} + " Hz / " +
            juce::String{static_cast<int>(status.bufferSize)} + " samples",
        juce::dontSendNotification);
    const auto& midi = controller_.project().state().midi;
    midiStatusLabel_.setText("MIDI: " + (midi.preferredInputIdentifier.isNotEmpty()
                                             ? midi.preferredInputIdentifier
                                             : "None"),
                             juce::dontSendNotification);
    memoryStatusLabel_.setText(
        "RAM: " +
            juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(assets_.usedBytes())) +
            " / " +
            juce::File::descriptionOfSizeInBytes(static_cast<juce::int64>(assets_.budgetBytes())),
        juce::dontSendNotification);
    operationStatusLabel_.setText(operationMessage_, juce::dontSendNotification);
    operationStatusLabel_.setColour(
        juce::Label::textColourId,
        juce::Colour{lastOperationWasError_ ? coralColour : mutedTextColour});
}

void SamplerView::publishModel(const bool markModified) {
    modified_ = modified_ || markModified;
    publisher_.publish(controller_.project().state());
    input_.refreshFromProject();
    lastSeenRevision_ = controller_.project().revision();
    refreshAll();
}

void SamplerView::applyParameterControls() {
    if (refreshing_)
        return;
    auto parameters = controller_.project().pad(selectedGlobalPad()).parameters;
    parameters.gainDecibels = static_cast<float>(gainSlider_.getValue());
    parameters.pan = static_cast<float>(panSlider_.getValue());
    parameters.coarseSemitones = static_cast<std::int16_t>(coarseSlider_.getValue());
    parameters.fineCents = static_cast<float>(fineSlider_.getValue());
    parameters.envelope.attackSeconds = static_cast<float>(attackSlider_.getValue());
    parameters.envelope.decaySeconds = static_cast<float>(decaySlider_.getValue());
    parameters.envelope.sustainLevel = static_cast<float>(sustainSlider_.getValue());
    parameters.envelope.releaseSeconds = static_cast<float>(releaseSlider_.getValue());
    parameters.chokeGroup = static_cast<std::uint8_t>(chokeSlider_.getValue());
    parameters.maximumVoices = static_cast<std::uint16_t>(voicesSlider_.getValue());
    parameters.playbackMode =
        static_cast<PlaybackMode>(std::max(0, playbackModeBox_.getSelectedId() - 1));
    parameters.polyphonyMode =
        static_cast<PolyphonyMode>(std::max(0, polyphonyBox_.getSelectedId() - 1));
    const auto result = controller_.setPadParameters(selectedGlobalPad(), parameters);
    if (result.wasOk()) {
        publishModel(true);
        setOperationMessage("Updated pad parameters", false);
    } else {
        setOperationMessage(result.getErrorMessage(), true);
    }
}

void SamplerView::applyMappings() {
    if (refreshing_)
        return;
    const auto result = controller_.setPadMappings(
        selectedGlobalPad(), static_cast<std::uint8_t>(midiNoteSlider_.getValue()),
        keyboardEditor_.getText());
    if (result.wasOk()) {
        publishModel(true);
        setOperationMessage("Updated pad mappings", false);
    } else {
        setOperationMessage(result.getErrorMessage(), true);
    }
}

void SamplerView::textEditorReturnKeyPressed(juce::TextEditor& editor) {
    if (&editor == &padNameEditor_) {
        const auto result = controller_.renamePad(selectedGlobalPad(), editor.getText());
        if (result.wasOk()) {
            publishModel(true);
            setOperationMessage("Renamed pad", false);
        } else {
            setOperationMessage(result.getErrorMessage(), true);
            refreshEditor();
        }
    } else if (&editor == &keyboardEditor_) {
        applyMappings();
    }
    grabKeyboardFocus();
}

void SamplerView::textEditorFocusLost(juce::TextEditor& editor) {
    textEditorReturnKeyPressed(editor);
}

bool SamplerView::keyPressed(const juce::KeyPress& key,
                             juce::Component* const originatingComponent) {
    const auto code = key.getKeyCode();
    const auto textEntry = dynamic_cast<juce::TextEditor*>(originatingComponent) != nullptr;
    if (code >= 0 && code < static_cast<int>(heldUiKeys_.size()) &&
        input_.keyDown(code, textEntry)) {
        heldUiKeys_[static_cast<std::size_t>(code)] = true;
        return true;
    }
    return false;
}

bool SamplerView::keyStateChanged(const bool isKeyDown,
                                  juce::Component* const originatingComponent) {
    juce::ignoreUnused(isKeyDown, originatingComponent);
    bool released = false;
    for (std::size_t code = 0; code < heldUiKeys_.size(); ++code)
        if (heldUiKeys_[code] && !juce::KeyPress::isKeyCurrentlyDown(static_cast<int>(code))) {
            heldUiKeys_[code] = false;
            juce::ignoreUnused(input_.keyUp(static_cast<int>(code)));
            released = true;
        }
    return released;
}

bool SamplerView::isSupportedAudioFile(const juce::File& file) {
    const auto extension = file.getFileExtension().toLowerCase();
    return file.existsAsFile() && (extension == ".wav" || extension == ".aiff" ||
                                   extension == ".aif" || extension == ".flac");
}

bool SamplerView::isInterestedInFileDrag(const juce::StringArray& files) {
    return std::any_of(files.begin(), files.end(),
                       [](const auto& path) { return isSupportedAudioFile(juce::File{path}); });
}

int SamplerView::padAtPosition(const juce::Point<int> position) const noexcept {
    for (std::size_t index = 0; index < padButtons_.size(); ++index)
        if (padButtons_[index].getBounds().contains(position))
            return static_cast<int>(index);
    return -1;
}

void SamplerView::filesDropped(const juce::StringArray& files, const int x, const int y) {
    const auto local = padAtPosition({x, y});
    if (local < 0)
        return;
    const auto first =
        static_cast<std::size_t>(controller_.project().state().ui.selectedBank) * padsPerBank +
        static_cast<std::size_t>(local);
    confirmAndQueueFiles(files, first);
}

void SamplerView::confirmAndQueueFiles(juce::StringArray files,
                                       const std::size_t firstGlobalPadIndex) {
    bool overwrites = false;
    for (int index = 0; index < files.size(); ++index) {
        const auto destination = firstGlobalPadIndex + static_cast<std::size_t>(index);
        if (destination >= totalPadCount)
            break;
        const auto& layer = controller_.project().pad(destination).layers[0];
        overwrites = overwrites || (layer.enabled && layer.assetUuid.isNotEmpty());
    }
    if (!overwrites) {
        juce::ignoreUnused(queueImportFiles(files, firstGlobalPadIndex, true));
        return;
    }

    juce::AlertWindow::showOkCancelBox(
        juce::MessageBoxIconType::WarningIcon, "Replace occupied pads?",
        "One or more destination pads already contain samples. Replace layer 1 sequentially?",
        "Replace", "Cancel", this,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<SamplerView>{this},
                                             files = std::move(files),
                                             firstGlobalPadIndex](const int result) {
            if (safe != nullptr && result != 0)
                juce::ignoreUnused(safe->queueImportFiles(files, firstGlobalPadIndex, true));
        }));
}

bool SamplerView::queueImportFiles(const juce::StringArray& files,
                                   const std::size_t firstGlobalPadIndex,
                                   const bool overwriteConfirmed) {
    if (!overwriteConfirmed || firstGlobalPadIndex >= totalPadCount)
        return false;
    std::size_t destination = firstGlobalPadIndex;
    for (const auto& path : files) {
        const juce::File file{path};
        if (destination >= totalPadCount)
            break;
        if (isSupportedAudioFile(file))
            importQueue_.push_back({file, destination++, 0U});
    }
    if (importQueue_.empty())
        return false;
    submitNextImport();
    return true;
}

void SamplerView::submitNextImport() {
    if (importActive_ || importQueue_.empty())
        return;
    const auto& queued = importQueue_.front();
    const auto& pad = controller_.project().pad(queued.globalPadIndex);
    SampleImportRequest request{
        JobSpec{controller_.project().uuid(), pad.uuid, controller_.project().revision(), 0},
        queued.file,
        juce::Uuid().toString(),
        queued.globalPadIndex,
        queued.layerIndex,
        assets_.budgetBytes(),
    };
    if (SampleImporter::submit(jobs_, std::move(request)).has_value()) {
        importActive_ = true;
        setOperationMessage("Importing " + queued.file.getFileName(), false);
    } else {
        setOperationMessage("Sample import queue is full", true);
        importQueue_.pop_front();
    }
}

void SamplerView::processPendingJobs() {
    for (;;) {
        const auto result = jobs_.tryPopCompleted();
        if (result == nullptr)
            break;
        handleCompletedJob(*result);
    }
}

void SamplerView::handleCompletedJob(const JobResult& result) {
    if (result.target.ownerUuid == "padflow-preview") {
        const auto previewResult = preview_.commit(result);
        setOperationMessage(previewResult.wasOk() ? "Preview started"
                                                  : previewResult.getErrorMessage(),
                            previewResult.failed());
        return;
    }
    if (result.target.ownerUuid == "padflow-resolve") {
        if (result.succeeded) {
            const auto* payload =
                static_cast<const SampleImportPayload*>(result.immutablePayload.get());
            if (payload != nullptr && payload->asset != nullptr)
                juce::ignoreUnused(assets_.publish(payload->asset));
        }
        publisher_.publish(controller_.project().state());
        return;
    }

    importActive_ = false;
    const auto commit = SampleImporter::commit(result, controller_, assets_);
    if (commit.wasOk()) {
        setOperationMessage("Imported " + importQueue_.front().file.getFileName(), false);
        if (!importQueue_.empty())
            importQueue_.pop_front();
        publishModel(true);
    } else {
        setOperationMessage(commit.getErrorMessage(), true);
        if (!importQueue_.empty())
            importQueue_.pop_front();
    }
    submitNextImport();
}

void SamplerView::timerCallback() {
    juce::ignoreUnused(input_.flushMidiCommands());
    processPendingJobs();
    juce::ignoreUnused(preview_.collectRetired());
    juce::ignoreUnused(publisher_.collectAcknowledged());
    if (controller_.project().revision() != lastSeenRevision_) {
        lastSeenRevision_ = controller_.project().revision();
        refreshAll();
    } else {
        refreshStatus();
        if (runtime_.engine().activeVoiceCount() > 0U)
            refreshPads();
    }
}

void SamplerView::showImportChooser(const std::size_t globalPadIndex) {
    fileChooser_ = std::make_unique<juce::FileChooser>("Choose a sample", juce::File{},
                                                       "*.wav;*.aif;*.aiff;*.flac", true);
    fileChooser_->launchAsync(juce::FileBrowserComponent::openMode |
                                  juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<SamplerView>{this},
                               globalPadIndex](const juce::FileChooser& chooser) {
                                  if (safe == nullptr)
                                      return;
                                  const auto file = chooser.getResult();
                                  if (isSupportedAudioFile(file)) {
                                      juce::StringArray files;
                                      files.add(file.getFullPathName());
                                      safe->confirmAndQueueFiles(std::move(files), globalPadIndex);
                                  }
                                  safe->fileChooser_.reset();
                              });
}

void SamplerView::showSaveChooser() {
    fileChooser_ = std::make_unique<juce::FileChooser>("Save PadFlow project", juce::File{},
                                                       "*.padflow", true);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
            juce::FileBrowserComponent::warnAboutOverwriting,
        [safe = juce::Component::SafePointer<SamplerView>{this}](const juce::FileChooser& chooser) {
            if (safe == nullptr)
                return;
            auto file = chooser.getResult();
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension(".padflow");
            if (file != juce::File{}) {
                const auto result = ProjectSerializer::save(safe->controller_.project(), file);
                safe->modified_ = !result.succeeded;
                safe->setOperationMessage(result.message, !result.succeeded);
                safe->refreshAll();
            }
            safe->fileChooser_.reset();
        });
}

void SamplerView::showOpenChooser() {
    fileChooser_ = std::make_unique<juce::FileChooser>("Open PadFlow project", juce::File{},
                                                       "*.padflow", true);
    fileChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<SamplerView>{this}](const juce::FileChooser& chooser) {
            if (safe == nullptr)
                return;
            const auto file = chooser.getResult();
            if (file != juce::File{}) {
                auto restored = Project::createEmpty();
                auto result = ProjectSerializer::load(file, restored);
                if (result.wasOk())
                    result = safe->controller_.restoreProject(std::move(restored));
                if (result.wasOk()) {
                    safe->input_.panic();
                    safe->assets_.clear();
                    safe->modified_ = false;
                    safe->publishModel(false);
                    safe->setOperationMessage("Opened " + file.getFileName(), false);
                } else {
                    safe->setOperationMessage(result.getErrorMessage(), true);
                }
            }
            safe->fileChooser_.reset();
        });
}

void SamplerView::showPadMenu(const std::size_t globalPadIndex) {
    juce::PopupMenu menu;
    menu.addItem(1, "Load / Replace sample");
    menu.addItem(2, "Clear pad");
    menu.addItem(3, "Rename");
    menu.addItem(4, "Change colour");
    menu.addSeparator();
    menu.addItem(5, "Copy");
    menu.addItem(6, "Paste", controller_.project().pad(globalPadIndex).uuid.isNotEmpty());
    menu.addItem(7, "Duplicate to next pad", globalPadIndex + 1U < totalPadCount);
    menu.showMenuAsync(
        juce::PopupMenu::Options{}.withTargetComponent(&padButtons_[globalPadIndex % padsPerBank]),
        [safe = juce::Component::SafePointer<SamplerView>{this}, globalPadIndex](const int choice) {
            if (safe == nullptr)
                return;
            juce::Result result = juce::Result::ok();
            switch (choice) {
            case 1:
                safe->showImportChooser(globalPadIndex);
                return;
            case 2:
                result = safe->controller_.clearPad(globalPadIndex);
                break;
            case 3:
                juce::ignoreUnused(safe->selectPad(globalPadIndex));
                safe->padNameEditor_.grabKeyboardFocus();
                safe->padNameEditor_.selectAll();
                return;
            case 4: {
                constexpr std::array<std::uint32_t, 4U> palette{0xff3f8f87U, 0xffd19a49U,
                                                                0xffd46f61U, 0xff687ca8U};
                const auto current = safe->controller_.project().pad(globalPadIndex).colourArgb;
                const auto found = std::find(palette.begin(), palette.end(), current);
                const auto next = found == palette.end() || std::next(found) == palette.end()
                                      ? palette.front()
                                      : *std::next(found);
                result = safe->controller_.recolourPad(globalPadIndex, next);
                break;
            }
            case 5:
                result = safe->controller_.copyPad(globalPadIndex);
                break;
            case 6:
                result = safe->controller_.pastePad(globalPadIndex);
                break;
            case 7:
                result = safe->controller_.duplicatePad(globalPadIndex, globalPadIndex + 1U);
                break;
            default:
                return;
            }
            if (result.wasOk()) {
                safe->publishModel(choice != 5);
                safe->setOperationMessage("Pad operation complete", false);
            } else {
                safe->setOperationMessage(result.getErrorMessage(), true);
            }
        });
}

void SamplerView::showAudioSettings() {
    auto* alert = new juce::AlertWindow{"Audio Settings", "Select the playback output and format.",
                                        juce::MessageBoxIconType::InfoIcon};
    juce::StringArray devices;
    for (const auto& device : runtime_.outputDevices())
        devices.add(device.name);
    if (devices.isEmpty())
        devices.add("System default");
    alert->addComboBox("device", devices, "Output device");
    alert->addTextEditor("rate", juce::String{runtime_.currentSettings().sampleRate, 0},
                         "Sample rate");
    alert->addTextEditor("buffer",
                         juce::String{static_cast<int>(runtime_.currentSettings().bufferSize)},
                         "Buffer size");
    alert->addButton("Apply", 1, juce::KeyPress{juce::KeyPress::returnKey});
    alert->addButton("Restart", 2);
    alert->addButton(runtime_.isTestToneEnabled() ? "Stop Test Tone" : "Start Test Tone", 3);
    alert->addButton("Cancel", 0, juce::KeyPress{juce::KeyPress::escapeKey});
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<SamplerView>{this},
                                             alert](const int result) {
            std::unique_ptr<juce::AlertWindow> ownedAlert{alert};
            if (safe == nullptr)
                return;
            if (result == 3) {
                safe->runtime_.setTestToneEnabled(!safe->runtime_.isTestToneEnabled());
                safe->setOperationMessage(safe->runtime_.isTestToneEnabled() ? "Test tone started"
                                                                             : "Test tone stopped",
                                          false);
                return;
            }
            if (result == 2) {
                const auto restart = safe->runtime_.restart();
                safe->setOperationMessage(restart.wasOk() ? "Audio restarted"
                                                          : restart.getErrorMessage(),
                                          restart.failed());
                return;
            }
            if (result != 1)
                return;
            auto settings = safe->runtime_.currentSettings();
            const auto* combo = ownedAlert->getComboBoxComponent("device");
            if (combo != nullptr && combo->getSelectedItemIndex() >= 0 &&
                combo->getText() != "System default")
                settings.outputDeviceIdentifier = combo->getText();
            settings.sampleRate = ownedAlert->getTextEditorContents("rate").getDoubleValue();
            settings.bufferSize = static_cast<std::uint32_t>(
                std::max(0, ownedAlert->getTextEditorContents("buffer").getIntValue()));
            auto applied = safe->runtime_.applySettings(settings);
            if (applied.wasOk())
                applied = safe->controller_.setAudioSettings(safe->runtime_.currentSettings());
            safe->setOperationMessage(applied.wasOk() ? "Audio settings applied"
                                                      : applied.getErrorMessage(),
                                      applied.failed());
            if (applied.wasOk())
                safe->modified_ = true;
        }),
        false);
}

void SamplerView::showMidiSettings() {
    auto* alert = new juce::AlertWindow{"MIDI Settings", "Select a MIDI input and channel.",
                                        juce::MessageBoxIconType::InfoIcon};
    juce::StringArray deviceNames;
    juce::StringArray identifiers;
    deviceNames.add("Disabled");
    identifiers.add(juce::String{});
    for (const auto& device : AudioRuntime::midiInputDevices()) {
        deviceNames.add(device.name);
        identifiers.add(device.identifier);
    }
    juce::StringArray channels;
    channels.add("Omni");
    for (int channel = 1; channel <= 16; ++channel)
        channels.add("Channel " + juce::String{channel});
    alert->addComboBox("device", deviceNames, "MIDI input");
    alert->addComboBox("channel", channels, "Channel filter");
    alert->addButton("Apply", 1, juce::KeyPress{juce::KeyPress::returnKey});
    alert->addButton("Panic", 2);
    alert->addButton("Cancel", 0, juce::KeyPress{juce::KeyPress::escapeKey});
    alert->enterModalState(
        true,
        juce::ModalCallbackFunction::create([safe = juce::Component::SafePointer<SamplerView>{this},
                                             alert, identifiers = std::move(identifiers)](
                                                const int result) {
            std::unique_ptr<juce::AlertWindow> ownedAlert{alert};
            if (safe == nullptr)
                return;
            if (result == 2) {
                safe->input_.panic();
                safe->setOperationMessage("All notes off", false);
                return;
            }
            if (result != 1)
                return;
            const auto* device = ownedAlert->getComboBoxComponent("device");
            const auto* channel = ownedAlert->getComboBoxComponent("channel");
            const auto deviceIndex = device != nullptr ? device->getSelectedItemIndex() : 0;
            MidiSettings settings;
            if (deviceIndex > 0 && deviceIndex < identifiers.size())
                settings.preferredInputIdentifier = identifiers[deviceIndex];
            settings.channelFilter = static_cast<std::uint8_t>(
                std::max(0, channel != nullptr ? channel->getSelectedItemIndex() : 0));
            const auto applied = safe->controller_.setMidiSettings(settings);
            if (applied.wasOk()) {
                safe->runtime_.setMidiInputEnabled(settings.preferredInputIdentifier,
                                                   settings.preferredInputIdentifier.isNotEmpty(),
                                                   &safe->input_);
                safe->input_.refreshFromProject();
                safe->modified_ = true;
            }
            safe->setOperationMessage(applied.wasOk() ? "MIDI settings applied"
                                                      : applied.getErrorMessage(),
                                      applied.failed());
        }),
        false);
}

void SamplerView::auditionSelectedLayer() {
    const auto& layer = controller_.project().pad(selectedGlobalPad()).layers[selectedLayerIndex()];
    if (layer.assetUuid.isEmpty()) {
        setOperationMessage("Selected layer has no sample", true);
        return;
    }
    const auto reference = std::find_if(
        controller_.project().state().assets.begin(), controller_.project().state().assets.end(),
        [&](const auto& entry) { return entry.uuid == layer.assetUuid; });
    if (reference == controller_.project().state().assets.end() ||
        !juce::File{reference->originalPath}.existsAsFile()) {
        setOperationMessage("Preview source is missing", true);
        return;
    }
    if (!preview_.begin(jobs_, juce::File{reference->originalPath}).has_value())
        setOperationMessage(preview_.lastError(), true);
    else
        setOperationMessage("Preparing preview", false);
}

void SamplerView::clearSelectedLayer() {
    auto layer = controller_.project().pad(selectedGlobalPad()).layers[selectedLayerIndex()];
    layer.assetUuid.clear();
    layer.enabled = false;
    const auto result =
        controller_.setLayer(selectedGlobalPad(), selectedLayerIndex(), std::move(layer));
    if (result.wasOk()) {
        publishModel(true);
        setOperationMessage("Cleared selected layer", false);
    } else {
        setOperationMessage(result.getErrorMessage(), true);
    }
}

juce::String SamplerView::selectedSampleName() const {
    const auto& layer = controller_.project().pad(selectedGlobalPad()).layers[selectedLayerIndex()];
    if (layer.assetUuid.isEmpty())
        return "No sample loaded";
    const auto reference = std::find_if(
        controller_.project().state().assets.begin(), controller_.project().state().assets.end(),
        [&](const auto& entry) { return entry.uuid == layer.assetUuid; });
    if (reference == controller_.project().state().assets.end())
        return "Missing asset record";
    return reference->originalName + (reference->missing ? " (missing)" : "");
}

std::size_t SamplerView::selectedLayerIndex() const noexcept {
    return static_cast<std::size_t>(std::max(1, layerSelector_.getSelectedId()) - 1);
}

void SamplerView::setOperationMessage(juce::String message, const bool isError) {
    operationMessage_ = std::move(message);
    lastOperationWasError_ = isError;
    refreshStatus();
}
} // namespace padflow
