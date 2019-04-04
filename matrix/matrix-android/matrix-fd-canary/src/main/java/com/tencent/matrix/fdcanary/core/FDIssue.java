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

package com.tencent.matrix.fdcanary.core;

public class FDIssue {
    public int type;
    public String path;
    public long fileSize;
    public int opCnt;
    public long bufferSize;
    public long opCostTime;
    public int opType;
    public long opSize;
    public String threadName;
    public String stack;

    public int repeatReadCnt;

    public FDIssue(int type, String stack) {
        this.type = type;
        this.stack = stack;
    }

    public FDIssue(int type, String path, long fileSize, int opCnt, long bufferSize, long opCostTime, int opType,
            long opSize, String threadName, String stack, int repeatReadCnt) {
        this.type = type;
        this.path = path;
        this.fileSize = fileSize;
        this.opCnt = opCnt;
        this.bufferSize = bufferSize;
        this.opCostTime = opCostTime;
        this.opType = opType;
        this.opSize = opSize;
        this.threadName = threadName;
        this.stack = stack;
        this.repeatReadCnt = repeatReadCnt;
    }
}
