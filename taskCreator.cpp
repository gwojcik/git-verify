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

#include "taskCreator.h"

#include "gitWrapper.h"

#include <filesystem>

namespace {
    auto prepareArgs(const TaskType::Process & process, const std::string & fileName) {
        auto args = std::vector<std::string>();
        args.push_back(process.executable);     // first param is always process name
        std::transform(process.params.begin(), process.params.end(), std::back_inserter(args),
            [&fileName](const std::variant<std::string, TaskType::Process::Special> & param) -> auto {
                if (param.index() == 1) {
                    return fileName;    // TaskType::Process::Special::FILENAME
                } else {
                    return std::get<std::string>(param);
                }
            }
        );
        return args;
    };
    
    bool testFile(const TaskType::File &taskFileConfig, const std::filesystem::path & filePath) {
        namespace fs = std::filesystem;
        for (auto && exceptionTest : taskFileConfig.exceptions) {
            auto exceptionBegin = fs::relative(filePath, exceptionTest).begin();
            if ((*exceptionBegin) != "..") {
                return false;
            }
        }
        if (taskFileConfig.files.size() == 0) {
            return true;
        }
        for (auto && fileTest : taskFileConfig.files) {
            if (fs::is_regular_file(fileTest)) {
                if (fileTest == filePath) {
                    return true;
                }
            } else if (fs::is_directory(fileTest)) {
                auto beginOfRelative = fs::relative(filePath, fileTest).begin();
                if ((*beginOfRelative) != "..") {
                    return true;
                }
            }
        }
        return false;
    };
}

TaskPhases TasksCreator::create() {
    ChangesData changesData = git->getChangedFiles(config.localSha, config.remoteSha);
    TaskPhases phases;
    std::map<std::string, std::string> addedLinesCache;
    auto getAddedLines = [this, &addedLinesCache](std::string fileName) {
        if (!addedLinesCache.count(fileName)) {
            addedLinesCache[fileName] = git->getAddedLines(config.localSha, config.remoteSha, fileName);
        }
        return addedLinesCache[fileName];
    };
    auto changedByExt = std::map<std::string, std::vector<int>>();
    {
        int i = 0;
        for (auto && fileName : changesData.newFiles) {
            auto path = std::filesystem::path(fileName);
            if(path.has_extension()) {
                auto ext = lastPart(path.string(), '.');
                changedByExt[ext].push_back(i);
            }
            changedByExt[""].push_back(i);
            i++;
        }
    }
    changedByExt["___any_change___"].push_back(changesData.newFileContent.size());
    changesData.newFileContent.push_back("");
    changesData.newFiles.push_back("___any_change___");
    changesData.oldFileContent.push_back("");

    auto forEachFile = [&changedByExt, &changesData, &phases, &getAddedLines](const TaskType & taskType) -> void{
        namespace fs = std::filesystem;
        for (auto && ext : taskType.file.value().ext) {
            if (!changedByExt.count(ext)) {
                continue;
            }
            for (auto && fileId : changedByExt[ext]){
                auto fileName = changesData.newFiles[fileId];
                auto filePath = fs::path(fileName);
                if (!testFile(taskType.file.value(), filePath)) {
                    continue;
                }
                const auto & process = taskType.process;
                auto task = new TaskPstream();
                Processing * processing = nullptr;
                auto args = prepareArgs(process, fileName);

                task->setProgram(process.executable, args);
                task->setDesrc(TaskRunDescription{
                    .taskTypeName = taskType.name,
                    .fileName = fileName,
                });
                task->setUseStdIn(process.useStdin);
                if (taskType.targetType == TaskType::TargetType::ADDED_TEXT) {
                    task->setFileContent(getAddedLines(fileName));
                } else {
                    task->setFileContent(changesData.newFileContent[fileId]);
                }

                switch (process.testType) {
                    case TestType::DIFF_WITH_CHECKOUT: [[fallthrough]];
                    case TestType::DIFF:
                        if (!process.useStdin && process.testType == TestType::DIFF) {
                            LogErr("TestType DIFF require useStdin");
                            std::exit(1);
                        } else {
                            auto sharedDiffState = std::make_shared<SharedDiffState>();
                            processing = new ProcessingDiff(
                                ProcessingDiff::DiffPart::B, process.logDiffFilterRegex, sharedDiffState
                            );
                            Task * task2 = nullptr;
                            if (changesData.oldFileContent[fileId].size()) {
                                auto taskOldData = new TaskPstream();
                                taskOldData->setProgram(process.executable, args);
                                taskOldData->setDesrc(TaskRunDescription{
                                    .taskTypeName = taskType.name,
                                    .fileName = fileName,
                                });
                                taskOldData->setFileContent(changesData.oldFileContent[fileId]);
                                taskOldData->setUseStdIn(process.useStdin);
                                task2 = taskOldData;
                            } else {
                                auto taskNull = new TaskNull();
                                taskNull->setDesrc(TaskRunDescription{
                                    .taskTypeName = "empty_file",
                                    .fileName = fileName,
                                });
                                task2 = taskNull;
                            }
                            if (process.testType == TestType::DIFF_WITH_CHECKOUT) {
                                phases.forOld.push_back(TaskPtr(task2));
                                phases.processingForOld.push_back(std::unique_ptr<Processing>(new ProcessingDiff(
                                    ProcessingDiff::DiffPart::A, process.logDiffFilterRegex, sharedDiffState
                                )));
                            } else {
                                phases.forNew.push_back(TaskPtr(task2));
                                phases.processingForNew.push_back(std::unique_ptr<Processing>(new ProcessingDiff(
                                    ProcessingDiff::DiffPart::A, process.logDiffFilterRegex, sharedDiffState
                                )));
                            }
                        }
                        break;
                    case TestType::RETURN:
                        processing = new ProcessingReturnValue();
                        break;
                    case TestType::MATCH_FAIL: [[fallthrough]];
                    case TestType::MATCH_SUCCESS:
                        processing = new ProcessingMatch(
                            process.testType == TestType::MATCH_SUCCESS,
                            process.testType == TestType::MATCH_SUCCESS ? process.matchForSuccess : process.matchForFail
                        );
                    break;
                }
                phases.forNew.push_back(TaskPtr(task));
                phases.processingForNew.push_back(std::unique_ptr<Processing>(processing));
            }
        }
    };
    
    auto forBuild = [&phases](const TaskType & taskType) -> void {
        const auto & process = taskType.process;
        auto task = new TaskPstream();
        auto args = prepareArgs(process, "<no file name>");
        task->setProgram(process.executable, args);
        task->setDesrc(TaskRunDescription{
            .taskTypeName = taskType.name,
            .fileName = "<build>",
        });
        task->setUseStdIn(false);
        if (process.useStdin) {
            LogErr("Build cannot use stdin");
            std::exit(1);
        }
        phases.build.push_back(TaskPtr(task));
        auto * processing = new ProcessingReturnValue();
        phases.processingBuild.push_back(std::unique_ptr<Processing>(processing));
    };
    
    auto forCommitText = [&phases, this](const TaskType & taskType) -> void {
        const auto & process = taskType.process;
        auto task = new TaskPstream();
        auto args = prepareArgs(process, "<no file name>");
        task->setProgram(process.executable, args);
        task->setDesrc(TaskRunDescription{
            .taskTypeName = taskType.name,
            .fileName = "<build>",
        });
        task->setUseStdIn(true);
        std::string allCommitsText = git->getJoinedCommitMsg(config.localSha, config.remoteSha);
        LogDev("text: ", allCommitsText);
        task->setFileContent(allCommitsText);
        Processing * processing;
        switch (process.testType) {
            case TestType::DIFF: [[fallthrough]];
            case TestType::DIFF_WITH_CHECKOUT:
                LogErr("TestType DIFF unsupported for commit text");
                std::exit(1);
            break;
            case TestType::MATCH_FAIL: [[fallthrough]];
            case TestType::MATCH_SUCCESS:
            processing = new ProcessingMatch(
                process.testType == TestType::MATCH_SUCCESS,
                process.testType == TestType::MATCH_SUCCESS ? process.matchForSuccess : process.matchForFail
            );
            break;
            case TestType::RETURN:
            processing = new ProcessingReturnValue();
            break;
        }
        phases.forNew.push_back(TaskPtr(task));
        phases.processingForNew.push_back(std::unique_ptr<Processing>(processing));
    };

    for (auto && taskType : taskTypes) {
        if (!taskType.second.enabled) {
            continue;
        }
        switch (taskType.second.targetType) {
            case TaskType::TargetType::ANY_CHANGE:
            {
                taskType.second.file = TaskType::File();
                taskType.second.file->ext.push_back(std::string("___any_change___"));
            }
            [[fallthrough]];
            case TaskType::TargetType::FILE: [[fallthrough]];
            case TaskType::TargetType::ADDED_TEXT: [[fallthrough]];
            case TaskType::TargetType::FILE_NAME:
                forEachFile(taskType.second);
                break;
            case TaskType::TargetType::BUILD:
                forBuild(taskType.second);
                break;
            case TaskType::TargetType::COMMIT_TEXT:
                forCommitText(taskType.second);
                break;
        }
    }
    return phases;
}
