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


#ifndef MATRIX_FD_CANARY_FD_COMMON_INFO_H
#define MATRIX_FD_CANARY_FD_COMMON_INFO_H



namespace fdcanary {

    typedef enum {

        kFD_IFIFO = 1,//pipe
        kFD_IFCHR = 2, //inputchannel（ashmem）
        kFD_IFDIR = 3,
        kFD_IFBLK = 4,
        kFD_IFREG = 5,//io
        kFD_IFLNK = 6,
        kFD_IFSOCK = 7,//socket

        kFD_ANON_EVENT_FD = 10,
        kFD_ANON_EVENTPOLL = 11,
        kFD_ANON_SYNC_FENCE = 12,
        kFD_ANON_DMABUF = 13,
        kFD_ANON_INFINIBANDEVENT = 14,
        kFD_ANON_VFIO_DEVICE = 15,
        kFD_ANON_PERF_EVENT = 16,

        kFD_UN_KNOW = 20,

    } FDType;


    
}


#endif //MATRIX_FD_CANARY_FD_COMMON_INFO_H
