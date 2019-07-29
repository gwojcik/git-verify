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
#include "configLoader.h"
#include "log.h"
#include "taskBase.h"
#include "gitWrapper.h"
#include "common.h"

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <iostream>
#include <atomic>
#include <thread>

std::atomic<unsigned> taskID;

using Tasks = std::vector<TaskPtr>;

namespace {
    struct TaskResult {
        Messages msgs;
        TaskRunDescription descr;
        int status = -1;
    };

    void runTask(const Tasks & tasks, std::vector<TaskResult> & result, unsigned id) {
        auto maxId = tasks.size();
        while (id < maxId) {
            result[id].msgs = tasks[id]->run();
            result[id].status = tasks[id]->getStatus();
            result[id].descr = tasks[id]->getDescr();
            id = taskID.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

int main(int argNum, char ** args) {
    enum class Mode {
        NONE,
        PRE_PUSH,
        PRE_COMMIT,
        TEST_1,
        TEST_2,
        HELP,
    };
    auto exeFullName = std::string(args[0]);
    auto exeName = lastPart(exeFullName, '/');

    Mode mode = Mode::NONE;
    if (exeName == "pre-push") {
        mode = Mode::PRE_PUSH;
    } else if (exeName == "pre-commit") {
        mode = Mode::PRE_COMMIT;
    } else {
        if (argNum == 3) {
            mode = Mode::TEST_2;
        } else if (argNum == 2) {
            if (args[1] == std::string("--help") || args[1] == std::string("-h")) {
                mode = Mode::HELP;
            } else {
                mode = Mode::TEST_1;
            }
        } else {
            mode = Mode::HELP;
        }
    }

    CreatorConfig config;
    switch (mode) {
        case (Mode::HELP):
        LogInfo(R"(
Usage:
1) pre-push
2) pre-commit
3) git-verify <rev>
4) git-verify <rev1> <rev2>
1 - as pre-push, see `git help hooks`
2 - as pre-commit, see `git help hooks`
3,4 - for testing in range <rev>..HEAD or <rev1>..<rev2>
)");
        std::exit(0);
        break;
        case (Mode::PRE_PUSH): {
            std::string remote = args[1];
            std::string url = args[2];
            std::string localRef, localSha, remoteRef, remoteSha;
            int branchCount = 0;
            while (std::cin >> localRef >> localSha >> remoteRef >> remoteSha) {
                config = CreatorConfig{
                    .remote = remote,
                    .url = url,
                    .localRef = localRef,
                    .localSha = localSha,
                    .remoteRef = remoteRef,
                    .remoteSha = remoteSha,
                };
                branchCount++;
                LogInfo("test with: git-verify --test ", localSha, " ", remoteSha);
            }
            bool addFailForMultiBranchPush = branchCount > 1 ? true : false;
            if (addFailForMultiBranchPush) {
                LogErr("Unsupported pushing to multiple branches");
                std::exit(1);
            }
        }
        break;
        case (Mode::TEST_2): {
            std::string localSha = args[1];
            std::string remoteSha = args[2];
            config = CreatorConfig{
                .remote = "",
                .url = "",
                .localRef = "",
                .localSha = localSha,
                .remoteRef = "",
                .remoteSha = remoteSha,
            };
        }
        break;
        case (Mode::TEST_1): {
            std::string localSha = "HEAD";
            std::string remoteSha = args[1];
            config = CreatorConfig{
                .remote = "",
                .url = "",
                .localRef = "",
                .localSha = localSha,
                .remoteRef = "",
                .remoteSha = remoteSha,
            };
        }
        break;
        case (Mode::PRE_COMMIT):
            LogErr("unsupported mode - pre-commit");
            // TODO add support for pre-commit
            std::exit(1);
            break;
        case (Mode::NONE):
            LogErr("unknown mode");
            std::exit(1);
        break;
    }

    auto progressFct = [](const std::vector<TaskResult> & results){
        while(true) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(100ms);
            bool allDone = true;
            std::cout << "[";
            for (auto && r : results) {
                if (r.status != -1) {
                    std::cout << (r.status == 0 ? '+' : 'F');
                } else {
                    allDone = false;
                    std::cout << ".";
                }
            }
            std::cout << "]\n";
            if (allDone) {
                break;
            }
        }
    };
    auto runTasks = [&progressFct](const Tasks & tasks, std::vector<TaskResult> & results, int forceThreadNum = 0){
        std::vector<std::thread> threads;
        int threadNum = forceThreadNum ? forceThreadNum : std::thread::hardware_concurrency();
        threadNum = static_cast<int>(tasks.size()) > threadNum ? threadNum : tasks.size();
        LogInfo("Tasks to run: ", tasks.size());
        
        results.resize(tasks.size());
        taskID = threadNum;
        auto progress = std::thread(progressFct, std::cref(results));
        if (threadNum == 1) {
            runTask(tasks, results, 0);
        } else {
            for (int i = 0; i<threadNum; i++) {
                threads.push_back(std::thread(runTask, std::cref(tasks), std::ref(results), i));
            }
            for (int i = 0; i<threadNum; i++) {
                threads[i].join();
            }
        }
        progress.join();
    };

    GitWrapper git = GitWrapper(".");
    auto crateor = TasksCreator(config, &git);
    TaskPhases phases = crateor.create();
    int resultStatus = 0;
    
    auto runBuild = [&phases, &runTasks, &resultStatus]() {
        if (phases.build.size()) {
            std::vector<TaskResult> resultsForBuild;
            runTasks(phases.build, resultsForBuild, 1);
            int i = 0;
            for (auto && result : resultsForBuild) {
                auto & processing = phases.processingBuild[i];
                Messages msgs = processing->process(result.msgs, result.status);
                int status = processing->getStatus();
                resultStatus |= status;
                for (auto && msg : msgs) {
                    print_msg(msg);
                }
                i++;
            }
        }
    };
    
    HeadData headData = git.getHeadSha();
    if (phases.forOld.size()) {
        if (git.canCheckout(config.remoteSha)) {
            LogInfo("chackout ",config.remoteSha);
            git.doCheckout(config.remoteSha);
            std::vector<TaskResult> resultsForOld;
            runBuild();
            runTasks(phases.forOld, resultsForOld);
            LogInfo("checkout HEAD ", headData.refName, "(", headData.sha, ")");
            int i = 0;
            for (auto && result : resultsForOld) {
                auto & processing = phases.processingForOld[i];
                processing->process(result.msgs, result.status);
                i++;
            }
            git.doCheckoutHead(headData);
        } else {
            LogErr("Checkout failed");
            resultStatus = 1;
        }
    }
    
    runBuild();
    
    std::vector<TaskResult> results;
    runTasks(phases.forNew, results);

    {
        int i = 0;
        for(auto && result : results) {
            auto & processing = phases.processingForNew[i];
            Messages msgs = processing->process(result.msgs, result.status);
            int status = processing->getStatus();
            LogInfo("STATUS: ", status);
            LogInfo(result.descr.taskTypeName, ": \"", result.descr.fileName, "\"");
            resultStatus |= status;
            for (auto && msg : msgs) {
                print_msg(msg);
            }
            i++;
        }
    }

    return resultStatus ? 1 : 0;
}
