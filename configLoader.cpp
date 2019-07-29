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

#include "configLoader.h"
#include "log.h"

#include <vector>
#include <optional>
#include <variant>
#include <yaml-cpp/yaml.h>
#include <filesystem>

namespace YAML {
    template <> struct convert <TaskType::TargetType> {
        static bool decode (const Node & node, TaskType::TargetType & targetType) {
            if ( !node.IsScalar() ) {
                LogErr(R"("targetType" node is not "Scalar".)");
                return false;
            }
            auto targetTypeName = node.as<std::string>();
            if ( targetTypeName == "FILE" ) {
                targetType = TaskType::TargetType::FILE;
            } else if ( targetTypeName == "COMMIT_TEXT" ) {
                targetType = TaskType::TargetType::COMMIT_TEXT;
            } else if ( targetTypeName == "FILE_NAME" ) {
                targetType = TaskType::TargetType::FILE_NAME;
            } else if ( targetTypeName == "ADDED_TEXT" ) {
                targetType = TaskType::TargetType::ADDED_TEXT;
            } else if ( targetTypeName == "BUILD" ) {
                targetType = TaskType::TargetType::BUILD;
            } else if (targetTypeName == "ANY_CHANGE" ) {
                targetType = TaskType::TargetType::ANY_CHANGE;
            } else {
                LogErr(R"("targetType" unknown value.)");
                return false;
            }
            return true;
        }
    };
    template <> struct convert <TestType> {
        static bool decode (const Node & node, TestType & type) {
            if ( !node.IsScalar() ) {
                LogErr(R"("testType" node is not "Scalar".)");
                return false;
            }
            auto typeName = node.as<std::string>();
            if ( typeName == "DIFF") {
                type = TestType::DIFF;
            } else if (typeName == "RETURN") {
                type = TestType::RETURN;
            } else if (typeName == "MATCH_SUCCESS") {
                type = TestType::MATCH_SUCCESS;
            } else if (typeName == "MATCH_FAIL") {
                type = TestType::MATCH_FAIL;
            } else if (typeName == "DIFF_WITH_CHECKOUT") {
                type = TestType::DIFF_WITH_CHECKOUT;
            } else {
                LogErr(R"("testType" unknown value.)");
                return false;
            }
            return true;
        }
    };
    template <> struct convert <TaskType::Process::Special> {
        static bool decode (const Node & node, TaskType::Process::Special & special) {
            if ( !node.IsScalar() ) {
                LogErr(R"("special" node is not "Scalar".)");
                return false;
            }
            auto specialName = node.as<std::string>();
            if (specialName == "FILENAME") {
                special = TaskType::Process::Special::FILENAME;
            } else {
                LogErr(R"("special" unknown value.)");
                return false;
            }
            return true;
        }
    };
    template <> struct convert <TaskType::File> {
        static bool decode (const Node & node, TaskType::File & file) {
            if (!node.IsMap()) {
                LogErr(R"("file" node is not "Map".)");
                return false;
            }
            if (!node["ext"]) {
                LogErr(R"("file" node require "ext" field.)");
                return false;
            }
            if (!node["ext"].IsSequence()) {
                LogErr(R"("file.ext" is not "Sequence")");
                return false;
            }
            if (node["files"]) {
                if (!node["files"].IsSequence()) {
                    LogErr(R"("file.files" is not "Sequence")");
                    return false;
                }
                file.files = node["files"].as<std::vector<std::string>>();
            }
            if (node["exceptions"]) {
                if (!node["exceptions"].IsSequence()) {
                    LogErr(R"("file.exceptions" is not "Sequence")");
                    return false;
                }
                file.exceptions = node["exceptions"].as<std::vector<std::string>>();
            }
            file.ext = node["ext"].as<std::vector<std::string>>();
            if (file.ext.size() == 0) {
                file.ext.push_back(""); // match all file extensions
            }
            return true;
        }
    };
    template <> struct convert <TaskType::Process> {
        static bool decode (const Node & node, TaskType::Process & process) {
            if (!node.IsMap()) {
                LogErr(R"("process" node is not "Map".)");
                return false;
            }
            if (!node["testType"]) {
                LogErr(R"("process" node require "testType" field.)");
                return false;
            }
            if (!node["useStdin"]) {
                LogErr(R"("process" node require "useStdin" field.)");
                return false;
            }
            if (!node["executable"]) {
                LogErr(R"("process" node require "executable" field.)");
                return false;
            }
            if (!node["params"]) {
                LogErr(R"("process" node require "params" field.)");
                return false;
            }
            if (!node["params"].IsSequence()) {
                LogErr(R"("process.params" is not "Sequence".)");
                return false;
            }
            for (auto && param : node["params"]) {
                if (param.IsScalar()) {
                    process.params.push_back(param.as<std::string>());
                } else if (param.IsMap()) {
                    if (param["special"]) {
                        process.params.push_back(param["special"].as<TaskType::Process::Special>());
                    } else {
                        LogErr(R"("process.params" unsupported element ("Map") in "Sequence".)");
                        return false;
                    }
                } else {
                    LogErr(R"("process.params" unsupported element in "Sequence".)");
                    return false;
                }
            }
            process.testType = node["testType"].as<TestType>();
            process.useStdin = node["useStdin"].as<bool>();
            LogDev("loader - useStdin ", process.useStdin ? 1: 0);
            process.skipOnEmptyFile = node["skipOnEmptyFile"].as<bool>(true);
            process.executable = node["executable"].as<std::string>();
            if (process.testType == TestType::DIFF) {
                if (!node["logDiffFilterRegex"]) {
                    LogErr(R"(process.testType = "DIFF" require "logDiffFilterRegex" field)");
                    return false;
                }
                process.logDiffFilterRegex = node["logDiffFilterRegex"].as<std::string>();
            }

            if (process.testType == TestType::MATCH_FAIL) {
                if (!node["matchForFail"]) {
                    LogErr(R"(process.testType = "MATCH_FAIL" require "matchForFail" field)");
                    return false;
                }
                process.matchForFail = node["matchForFail"].as<std::string>();
            }
            if (process.testType == TestType::MATCH_SUCCESS) {
                if (!node["matchForSuccess"]) {
                    LogErr(R"(process.testType = "MATCH_SUCCESS" require "matchForSuccess" field)");
                    return false;
                }
                process.matchForSuccess = node["matchForSuccess"].as<std::string>();
            }
            return true;
        }
    };
    template <> struct convert <TaskType> {
        static bool decode (const Node & node, TaskType & taskType) {
            using TargetType = TaskType::TargetType;
            if (!node.IsMap()) {
                LogErr(R"(Task definition is not "Map".)");
                return false;
            }
            if (! node["type"]) {
                LogErr(R"(Task definition require "type" field.)");
                return false;
            }
            if (! node["targetType"]) {
                LogErr(R"(Task definition require "targetType" field.)");
                return false;
            }
            if (node["description"]) {
                taskType.description = node["description"].as<std::string>();
            }
            taskType.targetType = node["targetType"].as<TargetType>();
            if (taskType.targetType != TargetType::COMMIT_TEXT && taskType.targetType != TargetType::BUILD && taskType.targetType != TargetType::ANY_CHANGE) {
                if (! node["file"]) {
                    LogErr(R"(Task definition of targetType not in ("COMMIT_TEXT", "BUILD", "ANY_CHANGE") require "file" field)");
                    return false;
                }
                taskType.file = node["file"].as<TaskType::File>();
            }
            if (! node["process"]) {
                LogErr(R"(Task definition of type = "PROCESS" require "process" field)");
                return false;
            }
            taskType.process = node["process"].as<TaskType::Process>();
            taskType.enabled = node["enabled"].as<bool>(true);
            return true;
        }
    };
}

TaskTypesMap loadTaskTypeConfig(const std::string & fileName) {
    YAML::Node configSrc = YAML::LoadFile(fileName);
    auto config = TaskTypesMap();
    for (auto && it : configSrc) {
        auto name = it.first.as<std::string>();
        config[name] = it.second.as<TaskType>();
        config[name].name = name;
    }
    return config;
}

TaskTypesMap loadTaskTypeConfig(){
    auto userConfigFile = std::string();
    {
        char * xdgConfigHome = std::getenv("XDG_CONFIG_HOME");
        if (xdgConfigHome) {
            userConfigFile = xdgConfigHome;
            userConfigFile += "/git-verify";
        } else {
            char * home = std::getenv("HOME");
            if (home == nullptr) {
                LogErr(R"(no "HOME" env variable)");
                std::exit(1);
            }
            userConfigFile = home;
            userConfigFile += "/.config/git-verify";
        }
        std::filesystem::create_directories(userConfigFile);
        userConfigFile += "/taskConfig.yml";
    }
    auto config = TaskTypesMap();
    if (std::filesystem::exists(userConfigFile)) {
        config = loadTaskTypeConfig(userConfigFile);
    }
    const char * repoConfigFileName = "./git-verify.yml";
    if (std::filesystem::exists(repoConfigFileName)){
        auto repoConfig = loadTaskTypeConfig(repoConfigFileName);
        for (auto && item : repoConfig) {
            config[item.first] = item.second;
        }
    }
    const char * repoUserConfigFileName = "./git-verify.user.yml";
    if (std::filesystem::exists(repoUserConfigFileName)){
        auto repoUserConfig = loadTaskTypeConfig(repoUserConfigFileName);
        for (auto && item : repoUserConfig) {
            config[item.first] = item.second;
        }
    }
    if (config.size() == 0) {
        LogErr(R"(no configuration found.)");
        std::exit(1);
    }
    return config;
}
