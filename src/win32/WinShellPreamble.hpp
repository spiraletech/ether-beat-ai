#pragma once

#include <filesystem>

// Shared preamble for the native Win32 shell translation unit. The UI can call
// playback helpers from version-navigation code before their implementation
// appears later in EtherBeatTabbedWin.cpp without moving the legacy media code.
namespace {
void playPath(const std::filesystem::path& path);
}
