#pragma once
#include <span>
#include <string>
namespace lykoware::cli {
int scene_command(std::span<const std::string> arguments, bool screenshot);
}
