/*
    Nudelta Console
    Copyright (C) 2022 Mohamed Gaber

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/
#include "access.hpp"
#include "air60.hpp"

#include <fstream>
#include <hidapi.h>
#include <iostream>
#include <scope_guard.hpp>
#include <ssco.hpp>

#define MAX_STR 255
#ifndef NUDELTA_VERSION
    #define NUDELTA_VERSION "UNKNOWN"
#endif

Air60 getKeyboard() {
    auto air60Optional = Air60::find();
    if (!air60Optional.has_value()) {
        throw std::runtime_error(
            "Couldn't find a NuPhy Air60 connected to this device. Make sure it's plugged in via USB."
        );
    }

    auto air60 = air60Optional.value();

    p("Found NuPhy Air60 at path {} (Firmware {:04x})\n",
      air60.path,
      air60.firmware);

    return air60;
}

SSCO_Fn(printVersion) {
    p("Nudelta Utility v{}\n", NUDELTA_VERSION);
    p("Copyright (c) Mohamed Gaber 2022\n");
    p(R"(
Licensed under the GNU General Public License, version 3, or at your option,
any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
)");
}

SSCO_Fn(printFirmware) {
    auto air60 = getKeyboard();
}

SSCO_Fn(resetKeymap) {
    auto mac = opts.options.find("mac") != opts.options.end();

    auto air60 = getKeyboard();
    air60.resetKeymap(mac);
    p("Wrote default keymap config to keyboard.\n");
}

SSCO_Fn(dumpKeymap) {
    auto mac = opts.options.find("mac") != opts.options.end();

    auto air60 = getKeyboard();
    auto keys = air60.getKeymap(mac);
    auto file = opts.options.find("dump-keys")->second;
    auto filePtr = fopen(file.c_str(), "wb");

    if (!filePtr) {
        throw std::runtime_error(
            fmt::format("Failed to open '{}' for writing", file)
        );
    }

    SCOPE_EXIT {
        fclose(filePtr);
    };

    // ALERT: Endianness-defined Behavior
    auto seeker = (char *)&*keys.begin();
    auto end = (char *)(&*keys.begin() + keys.size());
    fwrite(seeker, 1, end - seeker, filePtr);

    p("Wrote current {} keymap to '{}'.\n", mac ? "Mac" : "Windows", file);

    auto hexFileIterator = opts.options.find("dump-hex-to");
    if (hexFileIterator != opts.options.end()) {
        auto hexFile = hexFileIterator->second;
        auto hexFilePtr = fopen(hexFile.c_str(), "w");
        if (!hexFilePtr) {
            throw std::runtime_error(
                fmt::format("Failed to open '{}' for writing", hexFile)
            );
        }
        SCOPE_EXIT {
            fclose(hexFilePtr);
        };

        prettyPrintBinary(
            std::vector< uint8_t >(
                (uint8_t *)&*keys.begin(),
                (uint8_t *)(&*keys.begin() + keys.size())
            ),
            hexFilePtr
        );

        p("Wrote current keymap in hex format to '{}'.\n", hexFile);
    }
}

SSCO_Fn(loadKeymap) {
    auto mac = opts.options.find("mac") != opts.options.end();

    auto air60 = getKeyboard();
    auto keys = air60.getKeymap(mac);
    auto file = opts.options.find("load-keys")->second;
    auto filePtr = fopen(file.c_str(), "rb");
    if (!filePtr) {
        throw std::runtime_error(
            fmt::format("Failed to open '{}' for reading", file)
        );
    }
    SCOPE_EXIT {
        fclose(filePtr);
    };

    uint8_t readBuffer[1024];
    fread(readBuffer, 1, sizeof readBuffer, filePtr);

    // ALERT: Endianness-defined Behavior
    auto keymap = std::vector< uint32_t >(
        (uint32_t *)readBuffer,
        (uint32_t *)(readBuffer + 1024)
    );

    air60.setKeymap(keymap, mac);

    p("Wrote keymap '{}' to the keyboard's {} mode.\n",
      file,
      mac ? "Mac" : "Windows");
}

SSCO_Fn(loadYAML) {
    auto mac = opts.options.find("mac") != opts.options.end();

    auto air60 = getKeyboard();
    auto configPath = opts.options.find("load-profile")->second;

    std::string configStr;
    std::getline(std::ifstream(configPath), configStr, '\0');
    air60.setKeymapFromYAML(configStr, mac);

    p("Wrote keymap '{}' to the keyboard's {} mode.\n",
      configPath,
      mac ? "Mac" : "Windows");
}

int main(int argc, char *argv[]) {
    auto hidAccess = checkHIDAccess();
    if (!hidAccess.has_value()) {
        hidAccess = requestHIDAccess();
    }
    if (!hidAccess.value()) {
        p(stderr, "{}\n", hidAccessFailureMessage);
        return -1;
    }
    using Opt = SSCO::Option;

    SSCO::Options options(
        {Opt{"version",
             'V',
             "Show the current version of this app and exit.",
             false,
             printVersion},
         Opt{"firmware",
             'f',
             "Print the Air60 keyboard's firmware and exit.",
             false,
             printFirmware},
         Opt{"mac",
             'M',
             "Perform operations on the Mac mode of the keyboard instead of the windows one.",
             false},
         Opt{"reset-keys",
             'r',
             "Restore the original keymap.",
             false,
             resetKeymap},
         Opt{"dump-keys",
             'D',
             "Dump the keymap to a binary file.",
             true,
             dumpKeymap},
         Opt{"dump-hex-to",
             'H',
             "When the keymap is dumped to a binary file, also dump the keymap in a hex format to a text file.",
             true},
         Opt{"load-keys",
             'L',
             "Load the keymap from a binary file.",
             true,
             loadKeymap},
         Opt{"load-profile", 'l', "Load YAML keymap", true, loadYAML}}
    );

    try {
        auto opts = options.process(argc, argv);

        if (opts.has_value()) {
            if (!opts.value().options.size()) {
                options.printHelp(std::cout);
            }
            return 0;
        } else {
            options.printHelp(std::cout);
            return 1;
        }
    } catch (std::runtime_error &e) {
        p(stderr, "[ERROR] {}\n", e.what());
        return -1;
    }

    return 0;
}