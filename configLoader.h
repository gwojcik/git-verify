/*
    This file is part of git-verify.
    Copyright (C) 2019  Grzegorz Wójcik

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

#pragma once

#include "common.h"

#include <vector>
#include <optional>
#include <variant>
#include <map>

struct TaskType {
    struct File {
        std::vector<std::string> ext;
        std::vector<std::string> files;
        std::vector<std::string> exceptions;
    };
    struct Process {
        enum class Special {
            FILENAME,
        };
        std::vector<std::variant<std::string, Special>> params;
        std::string executable;
        std::string logDiffFilterRegex;
        std::string matchForFail;
        std::string matchForSuccess;
        TestType testType;
        bool useStdin;
        bool skipOnEmptyFile;
    };
    enum class TargetType {
        FILE,
        COMMIT_TEXT,
        FILE_NAME,
        ADDED_TEXT,
        BUILD,
        ANY_CHANGE,
    };
    std::string name;
    std::string description;
    std::optional<File> file;
    Process process;
    TargetType targetType;
    bool enabled;
};

using TaskTypesMap = std::map<std::string, TaskType>;

TaskTypesMap loadTaskTypeConfig(const std::string & fileName);

TaskTypesMap loadTaskTypeConfig();
