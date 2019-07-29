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

#include "log.h"
#include "gitWrapper.h"
#include <memory>
#include <vector>
#include <string>
#include <regex>

enum class MessageType {
    NORMAL,
    ERR
};
using Message = std::pair<MessageType, std::string>;
using Messages = std::vector<Message>;

inline void print_msg(Message msg) {
    if (msg.first == MessageType::NORMAL) {
        Log<1>(msg.second, '\n');
    } else {
        Log<1>(terminal::magenta, msg.second, terminal::reset, '\n');
    }
}

class Processing {
protected:
    int status;
public:
    Processing() = default;
    virtual Messages process(const Messages & messages, int status) = 0;
    int getStatus() {return status;}
};

class ProcessingNoop : public Processing {
public:
    ProcessingNoop() = default;
    Messages process(const Messages & messages, int status) override {
        this->status = status;
        return messages;
    }
};

class ProcessingReturnValue : public Processing {
public:
    ProcessingReturnValue() = default;
    Messages process(const Messages & messages, int status) override {
        this->status = status;
        if (status) {
            return messages;
        } else {
            return {};
        }
    }
};

class ProcessingMatch : public Processing {
public:
    ProcessingMatch(bool matchForSuccess, const std::string & match){
        this->matchForSuccess = matchForSuccess;
        this->match = std::regex(match);
    }
    Messages process(const Messages & messages, int status) override {
        (void) status;
        Messages result;
        for ( auto && msg : messages) {
            if (std::regex_match(msg.second,match) != matchForSuccess) {
                // matched and match for failure
                // or not matched and match for success
                result.push_back(std::make_pair(MessageType::ERR, msg.second));
                this->status = 1;
            }
        }
        return result;
    }
private:
    std::regex match;
    bool matchForSuccess;
};

struct SharedDiffState {
    int count = 0;
    Messages a;
    Messages b;
};

class ProcessingDiff : public Processing {
public:
    enum class DiffPart { A = 0, B = 1 };
    /** @param diffPartNum first
     */
    explicit ProcessingDiff(DiffPart diffPart, const std::string & logFilterRegexStr, std::shared_ptr<SharedDiffState> & diffState) {
        this->diffPart = diffPart;
        this->logFilterRegex = std::regex(logFilterRegexStr);
        this->diffState = diffState;
    }
    Messages process(const Messages & messages, int status) override {
        (void) status;
        if (diffPart == DiffPart::A) {
            diffState->a = messages;
            LogDev("process diff A");
        } else {
            diffState->b = messages;
            LogDev("process diff B");
        }
        if (diffState->count == 0) {
            this->status = 1;
            diffState->count = 1;
            return {};
        } else {
            Messages diffMesgs;
            auto logA = std::string();
            auto logB = std::string();
            for (auto && row : diffState->a) {
                auto line = std::regex_replace(row.second, logFilterRegex, "");
                logA.append(line);
                logA.append("\n");
            }
            for (auto && row : diffState->b) {
                auto line = std::regex_replace(row.second, logFilterRegex, "");
                logB.append(line);
                logB.append("\n");
            }
            int changedCount = 0;
            constexpr int contextSize = 3;
            bool realAdded = false;
            int lastAddedId = -1;
            Messages & msgB = diffState->b;
            for (auto && lineNo : GitWrapper::compareLogs(logA, logB)) {
                msgB[lineNo-1].first = MessageType::ERR;
                if (realAdded && lastAddedId < lineNo - 1 - contextSize) {
                    int contextBound = lastAddedId + contextSize;
                    while (lastAddedId < contextBound) {
                        lastAddedId++;
                        diffMesgs.push_back(msgB[lastAddedId]);
                    }
                }
                if (lastAddedId < lineNo - 1 - contextSize) {
                    lastAddedId = std::max(-1, lineNo - 1 - contextSize - 1);
                    if (lastAddedId >= 0) {
                        diffMesgs.push_back(std::make_pair(MessageType::NORMAL, "..."));
                    }
                }
                while (lastAddedId < lineNo - 1) {
                    lastAddedId++;
                    diffMesgs.push_back(msgB[lastAddedId]);
                }
                realAdded = true;
                changedCount++;
            }
            if (realAdded) {
                int contextBound = std::min(lastAddedId + contextSize, (int)msgB.size() - 1);
                while (lastAddedId < contextBound) {
                    lastAddedId++;
                    diffMesgs.push_back(msgB[lastAddedId]);
                }
            }
            this->status = changedCount ? 1 : 0;
            return diffMesgs;
        }
    }
private:
    std::shared_ptr<SharedDiffState> diffState;
    DiffPart diffPart;
    std::regex logFilterRegex;
};
