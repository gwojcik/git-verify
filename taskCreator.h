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

#include "configLoader.h"
#include "taskBase.h"

struct CreatorConfig {
    std::string remote;
    std::string url;
    std::string localRef;
    std::string localSha;
    std::string remoteRef;
    std::string remoteSha;
};

using Tasks = std::vector<TaskPtr>;

struct TaskPhases {
    Tasks forOld;
    std::vector<std::unique_ptr<Processing>> processingForOld;
    Tasks build;
    std::vector<std::unique_ptr<Processing>> processingBuild;
    Tasks forNew;
    std::vector<std::unique_ptr<Processing>> processingForNew;
};

class TasksCreator {
    TaskTypesMap taskTypes;
    CreatorConfig config;
    GitWrapper * git;
public:
    TasksCreator(const CreatorConfig & config, GitWrapper * git) : config(config) {
        taskTypes = loadTaskTypeConfig();
        this->git = git;
    };
    TaskPhases create();
};
