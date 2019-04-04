/*
 * Tencent is pleased to support the open source community by making wechat-matrix available.
 * Copyright (C) 2018 THL A29 Limited, a Tencent company. All rights reserved.
 * Licensed under the BSD 3-Clause License (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      https://opensource.org/licenses/BSD-3-Clause
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

package com.tencent.matrix.fdcanary.util;

import android.content.Context;

import com.tencent.matrix.fdcanary.config.SharePluginInfo;
import com.tencent.matrix.report.Issue;
import com.tencent.matrix.fdcanary.core.FDIssue;

import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.ListIterator;

//import com.tencent.matrix.util.DeviceUtil;

public final class FDCanaryUtil {
    private static final int DEFAULT_MAX_STACK_LAYER = 10;


    private static String sPackageName = null;

    public static void setPackageName(Context context) {
        if (sPackageName == null) {
            sPackageName = context.getPackageName();
        }
    }

    //todo
    public static String stackTraceToString(final StackTraceElement[] arr) {
        if (arr == null) {
            return "";
        }

        ArrayList<StackTraceElement> stacks = new ArrayList<>(arr.length);
        for (int i = 0; i < arr.length; i++) {
            String className = arr[i].getClassName();
            // remove unused stacks
            if (className.contains("libcore.io")
                || className.contains("com.tencent.matrix.iocanary")
                || className.contains("java.io")
                || className.contains("dalvik.system")
                || className.contains("android.os")) {
                continue;
            }

            stacks.add(arr[i]);
        }
        // stack still too large
        if (stacks.size() > DEFAULT_MAX_STACK_LAYER && sPackageName != null) {
            ListIterator<StackTraceElement> iterator = stacks.listIterator(stacks.size());
            // from backward to forward
            while (iterator.hasPrevious()) {
                StackTraceElement stack = iterator.previous();
                String className = stack.getClassName();
                if (!className.contains(sPackageName)) {
                    iterator.remove();
                }
                if (stacks.size() <= DEFAULT_MAX_STACK_LAYER) {
                    break;
                }
            }
        }
        StringBuffer sb = new StringBuffer(stacks.size());
        for (StackTraceElement stackTraceElement : stacks) {
            sb.append(stackTraceElement).append('\n');
        }
        return sb.toString();
    }

    public static String getThrowableStack(Throwable throwable) {
        if (throwable == null) {
            return "";
        }
        return FDCanaryUtil.stackTraceToString(throwable.getStackTrace());
    }


    public static Issue convertFDIssueToReportIssue(FDIssue fdIssue) {
        if (fdIssue == null) {
            return null;
        }

        Issue issue = new Issue(fdIssue.type);
        JSONObject content = new JSONObject();

        try {

//            content = DeviceUtil.getDeviceInfo(content)

            content.put(SharePluginInfo.ISSUE_FILE_PATH, fdIssue.path);
            content.put(SharePluginInfo.ISSUE_FILE_SIZE, fdIssue.fileSize);
            content.put(SharePluginInfo.ISSUE_FILE_OP_TIMES, fdIssue.opCnt);
            content.put(SharePluginInfo.ISSUE_FILE_BUFFER, fdIssue.bufferSize);
            content.put(SharePluginInfo.ISSUE_FILE_COST_TIME, fdIssue.opCostTime);
            content.put(SharePluginInfo.ISSUE_FILE_READ_WRITE_TYPE, fdIssue.opType);
            content.put(SharePluginInfo.ISSUE_FILE_OP_SIZE, fdIssue.opSize);
            content.put(SharePluginInfo.ISSUE_FILE_THREAD, fdIssue.threadName);
            content.put(SharePluginInfo.ISSUE_FILE_STACK, fdIssue.stack);
            content.put(SharePluginInfo.ISSUE_FILE_REPEAT_COUNT, fdIssue.repeatReadCnt);
        } catch (JSONException e) {
            e.printStackTrace();
        }

        issue.setContent(content);
        return issue;
    }
}
