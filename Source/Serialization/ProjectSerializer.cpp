#include "ProjectSerializer.h"

#include "App/ProductInfo.h"

#include <memory>

namespace padflow {
namespace {
juce::String quoted(const juce::String& value) {
    return juce::JSON::toString(juce::var{value}, true);
}

juce::File temporarySibling(const juce::File& destination) {
    return destination.getSiblingFile(destination.getFileName() + ".tmp");
}
} // namespace

juce::String ProjectSerializer::canonicalManifest(const Project& project) {
    juce::String manifest;
    manifest << "{\n"
             << "  \"applicationVersion\": " << quoted(juce::String{product::version.data()})
             << ",\n"
             << "  \"bundleIdentifier\": " << quoted(juce::String{product::bundleId.data()})
             << ",\n"
             << "  \"company\": " << quoted(juce::String{product::company.data()}) << ",\n"
             << "  \"format\": \"padflow-project\",\n"
             << "  \"product\": " << quoted(juce::String{product::name.data()}) << ",\n"
             << "  \"projectName\": " << quoted(project.name()) << ",\n"
             << "  \"projectUuid\": " << quoted(project.uuid()) << ",\n"
             << "  \"revision\": " << quoted(juce::String{project.revision()}) << ",\n"
             << "  \"schemaVersion\": " << project.schemaVersion() << "\n"
             << "}\n";
    return manifest;
}

SerializationResult ProjectSerializer::save(const Project& project, const juce::File& destination) {
    if (destination.getFileExtension().toLowerCase() !=
        juce::String{product::projectExtension.data()})
        return {false, "Project destination must use the .padflow extension"};

    const auto temporary = temporarySibling(destination);
    if (temporary.existsAsFile() && !temporary.deleteFile())
        return {false, "Could not remove a stale temporary project"};

    juce::ZipFile::Builder archive;
    const auto manifest = canonicalManifest(project);
    archive.addEntry(std::make_unique<juce::MemoryInputStream>(manifest.toRawUTF8(),
                                                               manifest.getNumBytesAsUTF8(), true),
                     9, "manifest.json", juce::Time{0});

    {
        juce::FileOutputStream output{temporary};
        if (!output.openedOk() || !archive.writeToStream(output, nullptr)) {
            temporary.deleteFile();
            return {false, "Could not write the temporary project archive"};
        }
        output.flush();
        if (output.getStatus().failed()) {
            temporary.deleteFile();
            return {false, "Could not flush the temporary project archive"};
        }
    }

    juce::ZipFile validationArchive{temporary};
    const auto manifestIndex = validationArchive.getIndexOfFileName("manifest.json");
    if (manifestIndex < 0) {
        temporary.deleteFile();
        return {false, "Temporary project archive has no manifest"};
    }

    std::unique_ptr<juce::InputStream> validationStream(
        validationArchive.createStreamForEntry(manifestIndex));
    const auto validationManifest =
        validationStream != nullptr
            ? juce::JSON::parse(validationStream->readEntireStreamAsString())
            : juce::var{};
    const auto* validationObject = validationManifest.getDynamicObject();
    if (validationObject == nullptr || static_cast<int>(validationObject->getProperty(
                                           "schemaVersion")) != product::schemaVersion) {
        temporary.deleteFile();
        return {false, "Temporary project manifest failed validation"};
    }

    const auto backup = destination.getSiblingFile(destination.getFileName() + ".bak");
    if (destination.existsAsFile()) {
        if (backup.existsAsFile() && !backup.deleteFile()) {
            temporary.deleteFile();
            return {false, "Could not remove the previous project backup"};
        }
        if (!destination.moveFileTo(backup)) {
            temporary.deleteFile();
            return {false, "Could not rotate the previous project backup"};
        }
    }

    if (!temporary.moveFileTo(destination)) {
        if (backup.existsAsFile())
            juce::ignoreUnused(backup.moveFileTo(destination));
        temporary.deleteFile();
        return {false, "Could not publish the validated project archive"};
    }

    return {true, {}};
}

juce::Result ProjectSerializer::load(const juce::File& source, Project& project) {
    if (!source.existsAsFile())
        return juce::Result::fail("Project file does not exist");

    juce::ZipFile archive{source};
    const auto manifestIndex = archive.getIndexOfFileName("manifest.json");
    if (manifestIndex < 0)
        return juce::Result::fail("Project archive has no manifest");

    std::unique_ptr<juce::InputStream> stream(archive.createStreamForEntry(manifestIndex));
    if (stream == nullptr)
        return juce::Result::fail("Project manifest could not be read");

    const auto parsed = juce::JSON::parse(stream->readEntireStreamAsString());
    const auto* object = parsed.getDynamicObject();
    if (object == nullptr)
        return juce::Result::fail("Project manifest is not a JSON object");

    const auto schema = static_cast<int>(object->getProperty("schemaVersion"));
    if (schema != product::schemaVersion)
        return juce::Result::fail("Unsupported project schema version");

    const auto uuid = object->getProperty("projectUuid").toString();
    const auto name = object->getProperty("projectName").toString();
    const auto revisionText = object->getProperty("revision").toString();
    if (uuid.isEmpty() || name.isEmpty() || revisionText.isEmpty() ||
        revisionText.startsWithChar('-'))
        return juce::Result::fail("Project manifest is missing required values");

    project = Project::createEmpty(name, uuid);
    project.restoreRevision(static_cast<std::uint64_t>(revisionText.getLargeIntValue()));
    return juce::Result::ok();
}
} // namespace padflow
