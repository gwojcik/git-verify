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

#include <vector>
#include <string>

struct git_repository;
struct git_tree;

/// structure of arrays
struct ChangesData {
    std::vector<std::string> newFiles;
    std::vector<std::string> newFileContent;
    std::vector<std::string> oldFileContent;
};

struct HeadData {
    std::string sha;
    std::string refName;
};

class GitWrapper {
    git_repository * repo = nullptr;
public:
    explicit GitWrapper(const std::string & repoPath);
    ~GitWrapper();
    ChangesData getChangedFiles(const std::string & newCommitShaStr, const std::string & oldCommitShaStr);
    std::string getJoinedCommitMsg(const std::string & newCommitShaStr, const std::string & oldCommitShaStr);
    std::string getAddedLines(const std::string & newCommitShaStr, const std::string & oldCommitShaStr, const std::string & fileName);
    bool canCheckout(const std::string & targetRevSpec);
    void doCheckout(const std::string & targetRevSpec);
    void doCheckoutHead(const HeadData & headData);
    HeadData getHeadSha();
    static std::vector<int> compareLogs(std::string oldLog, std::string newLog);
private:
    ChangesData getChangedFiles(git_tree * oldTree, git_tree * newTree);
};
