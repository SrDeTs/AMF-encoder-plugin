#include "av1_container_fix.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

namespace {

bool ReadFile(const std::filesystem::path& path, std::vector<std::uint8_t>& data) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    data.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return input.good() || input.eof();
}

bool WriteAtomically(const std::filesystem::path& output,
                     const std::vector<std::uint8_t>& data) {
    std::filesystem::path temporary = output;
    temporary += ".tmp";

    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            return false;
        }
        stream.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
        if (!stream) {
            std::error_code error;
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    std::error_code error;
    std::filesystem::rename(temporary, output, error);
    if (!error) {
        return true;
    }
    std::filesystem::remove(temporary, error);
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: amf_av1_crop_fix <input.mkv> <output.mkv>\n";
        return 2;
    }

    const std::filesystem::path input = argv[1];
    const std::filesystem::path output = argv[2];
    std::vector<std::uint8_t> data;
    if (!ReadFile(input, data)) {
        std::cerr << "Could not read input file: " << input << '\n';
        return 1;
    }

    const auto result = amf_plugin::FixAv1Mkv1080(data);
    if (result.status == amf_plugin::Av1ContainerFixStatus::Invalid ||
        result.status == amf_plugin::Av1ContainerFixStatus::Unsupported) {
        std::cerr << "File not changed: " << result.message << '\n';
        return 1;
    }
    if (result.status == amf_plugin::Av1ContainerFixStatus::AlreadyFixed) {
        std::cout << result.message << '\n';
        return 0;
    }

    if (!WriteAtomically(output, data)) {
        std::cerr << "Could not write output file: " << output << '\n';
        return 1;
    }

    std::cout << "Fixed: " << result.message << '\n';
    return 0;
}
