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

#include "taskBase.h"
#include "pstream/pstream.h"
#include "log.h"

std::pair<int, Messages> callProcess(const std::string & name, const std::vector<std::string> & args) {
    redi::ipstream process(name, args, redi::pstreams::pstdout|redi::pstreams::pstderr);

    std::string line;
    Messages msg;
    while (std::getline(process,line)) {
        msg.push_back({MessageType::NORMAL, line});
    }
    process.clear();
    process.err();
    while (std::getline(process,line)) {
        msg.push_back({MessageType::ERR, line});
    }
    process.close();
    return {process.rdbuf()->status(), msg};
}

std::pair<int, Messages> callProcess(const std::string & name, const std::vector<std::string> & args, const std::string & input) {
    redi::pstream process(name, args, redi::pstreams::pstderr|redi::pstreams::pstdin|redi::pstreams::pstdout);

    process << input << std::flush << redi::peof;
    enum streamEnd {
        STDERR = 1,
        STDOUT = 2,
        ALL = STDERR | STDOUT
    };
    int end = 0;
    constexpr int size = 1024;
    std::string buf;
    buf.resize(size);
    std::string unfinishedOut;
    std::string unfinishedErr;
    Messages msg;
    auto reader = [&buf, &msg](auto & stream, MessageType msgType, std::string & unfinished){
        int n;
        while ((n = stream.readsome(buf.data(), size))) {
            buf.resize(n);
            buf = unfinished + buf;
            int beginPos = 0;
            auto newLinePos = buf.find('\n', beginPos);
            while (newLinePos != std::string::npos) {
                msg.push_back({msgType, buf.substr(beginPos, newLinePos - beginPos)});
                beginPos = newLinePos + 1;
                newLinePos = buf.find('\n', beginPos);
            }
            unfinished = buf.substr(beginPos, newLinePos - beginPos);
            buf.resize(size);
        };
    };
    
    while (end != ALL) {
        if (!process.out().eof()) {
            reader(process.out(), MessageType::NORMAL, unfinishedOut);
        } else {
            end |= STDOUT;
            msg.push_back({MessageType::NORMAL, unfinishedOut});
        }
        if (!process.err().eof()) {
            reader(process.err(), MessageType::ERR, unfinishedErr);
        } else {
            end |= STDERR;
            msg.push_back({MessageType::ERR, unfinishedErr});
        }
    }

    process.close();
    return std::make_pair(process.rdbuf()->status(), msg);
}
