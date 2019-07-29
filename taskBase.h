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
#include "messages.h"
#include "log.h"

class Task;

using TaskPtr = std::unique_ptr<Task>;

struct TaskRunDescription {
    std::string taskTypeName;
    std::string fileName;
};

class Task {
protected:
    TaskRunDescription descr;
public:
    Task() = default;
    virtual Messages run() = 0;
    virtual int getStatus() = 0;
    TaskRunDescription getDescr() {
        return this->descr;
    }
    void setDesrc(const TaskRunDescription & descr) {
        this->descr = descr;
    }
};

class TaskNull : public Task {
public:
    TaskNull() = default;
    virtual Messages run() override { return {}; }
    virtual int getStatus() override { return 0; }
};

std::pair<int, Messages> callProcess(const std::string & name, const std::vector<std::string> & args);
std::pair<int, Messages> callProcess(const std::string & name, const std::vector<std::string> & args, const std::string & input);

class TaskPstream : public Task {
    std::string programName;
    std::vector<std::string> args;
    std::string fileContent;
    int status;
    bool useStdIn = true;
public:
    TaskPstream() = default;

    void setProgram(const std::string & programName, const std::vector<std::string> & args) {
        this->programName = programName;
        this->args = args;
    }

    void setFileContent (const std::string & fileContent) {
        this->fileContent = fileContent;
    }
    
    void setUseStdIn(bool useStdIn) {
        this->useStdIn = useStdIn;
    }

    int getStatus() override {
        return status;
    }
    
    virtual Messages run() override {
        LogDev("useStdIn", useStdIn ? 1 : 0);
        auto result = useStdIn ? callProcess(programName, args, fileContent ) : callProcess(programName, args);
        status = result.first;
        return result.second;
    }
};
