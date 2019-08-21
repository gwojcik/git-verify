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

#include "messages.h"
#include "gitWrapper.h"
#include <git2.h>

namespace {
    struct FileCbPayload {
        Messages & msgs;
        std::vector<std::string> & newFiles;
    };
    struct LogDiffCbPayload {
        std::vector<int> & addedLines;
    };
    int file_cb(const git_diff_delta * delta, float progress, void * payloadVoid) {
        (void) progress;
        if (delta->new_file.mode == GIT_FILEMODE_COMMIT) {
            // skip submodule changes
            return 0;
        }
        auto * payload = static_cast<FileCbPayload*>(payloadVoid);
        payload->msgs.push_back({MessageType::NORMAL, delta->new_file.path});
        payload->newFiles.push_back(delta->new_file.path);
        return 0;
    }
    int logDiff_line_cb(const git_diff_delta * delta, const git_diff_hunk *hunk, const git_diff_line *line, void * payloadVoid) {
        (void) delta;
        (void) hunk;
        auto * payload = static_cast<LogDiffCbPayload*>(payloadVoid);
        if (line->old_lineno == -1) {
            payload->addedLines.push_back(line->new_lineno);
        }
        return 0;
    }
    inline void ok(int isNotOk, const char * msg) {
        if (isNotOk) {
            const git_error * error = giterr_last();     // NOTE: for libgit2 0.28 git_error_last
            const char * errMsg = error ? error->message : "null";
            throw std::runtime_error(std::to_string(isNotOk) + " " + msg + "\nlibgit2 msg: " + errMsg);
        }
    }
    
    constexpr git_diff_options getDiffOptsIgnoreWhiteSpace() {
        git_diff_options diffopts = GIT_DIFF_OPTIONS_INIT;
        diffopts.flags = GIT_DIFF_NORMAL | GIT_DIFF_IGNORE_WHITESPACE;
        return diffopts;
    }

    struct getAddedLines_payload {
        const std::string & fileName;
        std::string & result;
    };
    
    int getAddedLines_line_cb(const git_diff_delta * delta, const git_diff_hunk * unusedHunk, const git_diff_line * line, void * payloadRaw) {
        (void) unusedHunk;
        getAddedLines_payload * payload = (getAddedLines_payload*)(payloadRaw);
        if (delta->new_file.path == payload->fileName && line->origin == '+') {
            payload->result += std::string(line->content, line->content_len);
        }
        return 0;
    }
}


GitWrapper::GitWrapper(const std::string& repoPath) {
    git_libgit2_init();
    ok(git_repository_open(&repo, repoPath.c_str()), "open repo");
}

GitWrapper::~GitWrapper() {
    git_repository_free(repo);
    git_libgit2_shutdown();
}

ChangesData GitWrapper::getChangedFiles(const std::string& newCommitShaStr, const std::string& oldCommitShaStr) {
    git_object * newObj = nullptr;
    git_object * oldObj = nullptr;
    ok(git_revparse_single(&newObj, repo, newCommitShaStr.c_str()), "commit spec revparse - new");
    const git_oid * newCommitId = git_object_id(newObj);
    ok(git_revparse_single(&oldObj, repo, oldCommitShaStr.c_str()), "commit spec revparse - old");
    const git_oid * oldCommitId = git_object_id(oldObj);
    git_commit * oldCommit = nullptr;
    git_commit * newCommit = nullptr;
    ok(git_commit_lookup(&oldCommit, repo, oldCommitId), "old commit");
    ok(git_commit_lookup(&newCommit, repo, newCommitId), "new commit");
    git_tree * oldTree = nullptr;
    git_tree * newTree = nullptr;
    ok(git_tree_lookup(&oldTree, repo, git_commit_tree_id(oldCommit)), "old tree");
    ok(git_tree_lookup(&newTree, repo, git_commit_tree_id(newCommit)), "new tree");
    git_commit_free(oldCommit);
    git_commit_free(newCommit);
    git_object_free(newObj);
    git_object_free(oldObj);
    ChangesData ret = getChangedFiles(oldTree, newTree);
    git_tree_free(oldTree);
    git_tree_free(newTree);
    return ret;
}

HeadData GitWrapper::getHeadSha(){
    git_reference *refefence = nullptr;
    ok(git_repository_head(&refefence, repo), "repository head");
    std::string refName = git_reference_name(refefence);
    git_annotated_commit *annotatedCommit = nullptr;
    ok(git_annotated_commit_from_ref(&annotatedCommit, repo, refefence), "head commit");
    const git_oid * oid = git_annotated_commit_id(annotatedCommit);
    char sha[128];
    git_oid_tostr(sha, 128, oid);
    return HeadData{
        .sha = sha,
        .refName = refName,
    };
}

bool GitWrapper::canCheckout(const std::string & targetRevSpec) {
    git_annotated_commit * target = nullptr;
    git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
    checkout_opts.checkout_strategy = GIT_CHECKOUT_NONE;
    bool success = true;
    try {
        ok(git_annotated_commit_from_revspec(&target, repo, targetRevSpec.c_str()), "test checkout - target commit");
        git_commit * commit;
        ok(git_commit_lookup(&commit, repo, git_annotated_commit_id(target)), "test checkout - commit lookup");
        ok(git_checkout_tree(repo, (const git_object *)commit, &checkout_opts), "test checkout - checkout tree");
    } catch (std::runtime_error & e) {
        LogErr(e.what());
        success = false;
    }
    git_annotated_commit_free(target);
    LogDev("OK - checkout test");
    return success;
}

void GitWrapper::doCheckout(const std::string & targetRevSpec) {
    git_annotated_commit * target = nullptr;
    git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
    checkout_opts.checkout_strategy = GIT_CHECKOUT_SAFE;
    
    ok(git_annotated_commit_from_revspec(&target, repo, targetRevSpec.c_str()), "checkout - target commit");
    git_commit * commit;
    ok(git_commit_lookup(&commit, repo, git_annotated_commit_id(target)), "checkout - commit lookup");
    ok(git_checkout_tree(repo, (const git_object *)commit, &checkout_opts), "checkout - checkout tree");
    ok(git_repository_set_head_detached(repo, git_annotated_commit_id(target)), "checkout - detach dead");
    git_annotated_commit_free(target);
    LogDev("OK - checkout");
}

void GitWrapper::doCheckoutHead(const HeadData & headData) {
    doCheckout(headData.sha);
    ok(git_repository_set_head(repo, headData.refName.c_str()), "set head");
}

std::vector<int> GitWrapper::compareLogs(std::string oldLog, std::string newLog) {
    auto addedLines = std::vector<int>();
    auto payload = LogDiffCbPayload{
        .addedLines = addedLines
    };
    ok(git_diff_buffers(
        oldLog.c_str(), oldLog.size(), "a",
        newLog.c_str(), newLog.size(), "b",
        nullptr, nullptr, nullptr, nullptr, logDiff_line_cb, &payload
    ),"compareLogs - git_diff_buffers");
    return addedLines;
}

ChangesData GitWrapper::getChangedFiles(git_tree * oldTree, git_tree * newTree) {
    git_diff * diff = nullptr;
    git_diff_options diffopts = getDiffOptsIgnoreWhiteSpace();
    ok(git_diff_tree_to_tree(&diff, repo, oldTree, newTree, &diffopts), "tree diff");
    ChangesData result;
    Messages msgs;
    FileCbPayload payload = {.msgs = msgs, .newFiles = result.newFiles};
    ok(git_diff_foreach(diff, file_cb, nullptr, nullptr, nullptr, &payload), "diff foreach");
    for (auto && file : payload.newFiles) {
        struct LoopItem {
            git_tree * tree;
            decltype(result.newFileContent) & contentDst;
        };
        for ( auto && loopItem : ( LoopItem[] ) {{.tree = newTree, .contentDst = result.newFileContent }, {.tree = oldTree, .contentDst = result.oldFileContent}} ) {
            git_tree_entry * entry = nullptr;
            auto entry_status = git_tree_entry_bypath(&entry, loopItem.tree, file.c_str());
            bool fileExist = false;
            if (entry && entry_status == 0) {
                git_object * object = nullptr;
                ok(git_tree_entry_to_object(&object, repo, entry), "git tree entry to object");
                git_otype objectType = git_object_type(object);
                if (objectType == GIT_OBJ_BLOB) {
                    const git_oid * blobId = git_object_id(object);
                    git_blob * blob = nullptr;
                    git_blob_lookup(&blob, repo, blobId);
                    size_t size = git_blob_rawsize(blob);
                    std::string data((const char*)git_blob_rawcontent(blob), size);
                    git_blob_free(blob);
                    fileExist = true;
                    loopItem.contentDst.push_back(data);
                }
                git_object_free(object);
                git_tree_entry_free(entry);
            }
            if (!fileExist ) {
                loopItem.contentDst.push_back(std::string());
            }
        }
    }
    git_diff_free(diff);
    return result;
}

std::string GitWrapper::getAddedLines(const std::string& newCommitShaStr, const std::string& oldCommitShaStr, const std::string& fileName) {
    git_object * newObj = nullptr;
    git_object * oldObj = nullptr;
    ok(git_revparse_single(&newObj, repo, newCommitShaStr.c_str()), "commit spec revparse - new");
    ok(git_revparse_single(&oldObj, repo, oldCommitShaStr.c_str()), "commit spec revparse - old");

    const git_oid * newCommitId = git_object_id(newObj);
    const git_oid * oldCommitId = git_object_id(oldObj);
    
    git_commit * newCommit = nullptr;
    git_commit * oldCommit = nullptr;
    ok(git_commit_lookup(&newCommit, repo, newCommitId), "new commit lookup");
    ok(git_commit_lookup(&oldCommit, repo, oldCommitId), "old commit lookup");

    git_tree * newTree = nullptr;
    git_tree * oldTree = nullptr;
    ok(git_commit_tree(&newTree, newCommit), "new commit tree");
    ok(git_commit_tree(&oldTree, oldCommit), "old commit tree");

    git_diff_options diffopts = getDiffOptsIgnoreWhiteSpace();
    
    git_diff * diff = nullptr;
    git_diff_tree_to_tree(&diff, repo, oldTree, newTree, &diffopts);
    
    std::string result;
    getAddedLines_payload payload = {
        .fileName = fileName,
        .result = result,
    };
    
    git_diff_foreach(diff, nullptr, nullptr, nullptr, getAddedLines_line_cb, (void*)(&payload));

    git_diff_free(diff);
    git_tree_free(newTree);
    git_commit_free(newCommit);
    git_commit_free(oldCommit);
    git_object_free(newObj);
    git_object_free(oldObj);
    
    return result;
}

std::string GitWrapper::getJoinedCommitMsg(const std::string& newCommitShaStr, const std::string& oldCommitShaStr){
    
    auto range = oldCommitShaStr + ".." + newCommitShaStr;
    
    git_revwalk *walk = nullptr;
    ok(git_revwalk_new(&walk, repo), "revwalk");
    git_revwalk_sorting(walk, GIT_SORT_REVERSE);
    ok(git_revwalk_push_range(walk, range.c_str()), "revwalk push range");
    std::string result;
    git_oid oid;
    while ((git_revwalk_next(&oid, walk)) == 0) {
        git_commit * commit = nullptr;
        ok(git_commit_lookup(&commit, repo, &oid), "commit lookup");
        result += git_commit_message(commit);
        git_commit_free(commit);
    }
    return result;
}
