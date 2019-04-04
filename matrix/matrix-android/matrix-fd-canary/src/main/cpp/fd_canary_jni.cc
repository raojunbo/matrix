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

//
// jni interfaces
// posix fd hook
//
#include <jni.h>
#include <cstddef>
#include <android/log.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include "elf_hook.h"
#include "core/fd_dump.h"
#include "core/fd_canary.h"
#include "comm/fd_canary_utils.h"
namespace fdcanary
{

static const char *const kTag = "FDCanary.JNI";

static int (*original_open)(const char *pathname, int flags, mode_t mode);
static int (*original_open64)(const char *pathname, int flags, mode_t mode);
static int (*original_close)(int fd);

static int (*original_ashmem_create_region) (const char *name, size_t size);

static int (*original_epoll_create) (int size);

static int (*original_socket) (int domain,int type,int protocol);
static int (*original_shutdown) (int s,int how);

static int (*original_pipe) (int filedes[2]);
static FILE* (*original_popen) (const char * command,const char * type);

static bool kInitSuc = false;
static JavaVM *kJvm;

static jclass kJavaBridgeClass;
static jmethodID kMethodIDOnIssuePublish;

static jclass kJavaContextClass;
static jmethodID kMethodIDGetJavaContext;
static jfieldID kFieldIDStack;
static jfieldID kFieldIDThreadName;

static jclass kIssueClass;
static jmethodID kMethodIDIssueConstruct;
static jmethodID kMethodIDIssueConstruct2;

static jclass kListClass;
static jmethodID kMethodIDListConstruct;
static jmethodID kMethodIDListAdd;

const static char *TARGET_MODULES_IO[] = {
    "libopenjdkjvm.so", //io相关
    "libjavacore.so",
    "libopenjdk.so",
    "libandroid_runtime.so",
    "libandroidfw.so",
    "libart.so", //inputchannel相关，thread相关，looper相关,ashmem相关
    //"libbinder.so",
    "libcutils.so"
};
const static size_t TARGET_MODULE_COUNT_IO = sizeof(TARGET_MODULES_IO) / sizeof(char *);

const static char *TARGET_MODULES_ASHMEM[] = {
    "libandroid_runtime.so",
    "libandroidfw.so",
    "libart.so",
    //"libbinder.so",
    "libcutils.so",
};
const static size_t TARGET_MODULE_COUNT_ASHMEM = sizeof(TARGET_MODULES_ASHMEM) / sizeof(char *);

const static char *TARGET_MODULES_EPOLL[] = {
    //"libandroid_servers.so",
    "libutils.so",
    "libc.so",
    "libopenjdk.so",

    //"libbinder.so",
};
const static size_t TARGET_MODULE_COUNT_EPOLL = sizeof(TARGET_MODULES_EPOLL) / sizeof(char *);

const static char *TARGET_MODULES_SOCKET[] = {
    "libc.so",
    "libopenjdk.so",
    "libjavacore.so",
    "libandroid_runtime.so",
    "libart.so",

    //"libbinder.so",
};
const static size_t TARGET_MODULE_COUNT_SOCKET = sizeof(TARGET_MODULES_SOCKET) / sizeof(char *);

const static char *TARGET_MODULES_PIPE[] = {
    
    "libandroid_runtime.so",
    "libart.so",
    "libc.so",
    "libinputflinger.so",
    "libjavacrypto.so",
    "libopenjdk.so",
    "libstagefright_foundation.so",

    "libcutils.so",
};
const static size_t TARGET_MODULE_COUNT_PIPE = sizeof(TARGET_MODULES_PIPE) / sizeof(char *);
extern "C"
{
    /**
         * tool method to get JNIEnv;
         * ensure called after JNI_OnLoad
         * @return nullable
         */
    static JNIEnv *getJNIEnv()
    {
        //ensure kJvm init
        assert(kJvm != NULL);

        JNIEnv *env = NULL;
        int ret = kJvm->GetEnv((void **)&env, JNI_VERSION_1_6);
        if (ret != JNI_OK)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "getJNIEnv !JNI_OK: %d", ret);
        }

        return env;
    }

    char *jstringToChars(JNIEnv *env, jstring jstr)
    {
        if (jstr == nullptr)
        {
            return nullptr;
        }

        jboolean isCopy = JNI_FALSE;
        const char *str = env->GetStringUTFChars(jstr, &isCopy);
        char *ret = strdup(str);
        env->ReleaseStringUTFChars(jstr, str);
        return ret;
    }

    void OnIssuePublish(const std::vector<FDIssue> &published_issues)
        {
            if (!kInitSuc)
            {
                __android_log_print(ANDROID_LOG_ERROR, kTag, "OnIssuePublish kInitSuc false");
                return;
            }

            JNIEnv *env;
            bool attached = false;
            jint j_ret = kJvm->GetEnv((void **)&env, JNI_VERSION_1_6);
            if (j_ret == JNI_EDETACHED)
            {
                jint jAttachRet = kJvm->AttachCurrentThread(&env, nullptr);
                if (jAttachRet != JNI_OK)
                {
                    __android_log_print(ANDROID_LOG_ERROR, kTag, "onIssuePublish AttachCurrentThread !JNI_OK");
                    return;
                }
                else
                {
                    attached = true;
                }
            }
            else if (j_ret != JNI_OK || env == NULL)
            {
                return;
            }

            jthrowable exp = env->ExceptionOccurred();
            if (exp != NULL)
            {
                __android_log_print(ANDROID_LOG_INFO, kTag, "checkCanCallbackToJava ExceptionOccurred, return false");
                env->ExceptionDescribe();
                return;
            }
            __android_log_print(ANDROID_LOG_INFO, kTag, "start issue publish");
            jobject j_issues = env->NewObject(kListClass, kMethodIDListConstruct);

            for (const auto &issue : published_issues)
            {
                jint type = issue.fdinfo_.fd_type_;
                jstring stack = env->NewStringUTF(issue.fdinfo_.stack_.c_str());

                //jobject issue_obj = env->NewObject(kIssueClass, kMethodIDIssueConstruct, type, path, file_size, op_cnt, buffer_size, op_cost_time, op_type, op_size, thread_name, stack, repeat_read_cnt);
                jobject issue_obj = env->NewObject(kIssueClass, kMethodIDIssueConstruct2, type, stack);
                env->CallBooleanMethod(j_issues, kMethodIDListAdd, issue_obj);

                env->DeleteLocalRef(issue_obj);
                env->DeleteLocalRef(stack);
            }

            env->CallStaticVoidMethod(kJavaBridgeClass, kMethodIDOnIssuePublish, j_issues);

            env->DeleteLocalRef(j_issues);

            if (attached)
            {
                kJvm->DetachCurrentThread();
            }
        }

    static void DoProxyOpenLogic(const char *pathname, int flags, mode_t mode, int ret)
        {
            JNIEnv *env = NULL;
            kJvm->GetEnv((void **)&env, JNI_VERSION_1_6);
            if (env == NULL || !kInitSuc) {
                __android_log_print(ANDROID_LOG_ERROR, kTag, "ProxyOpen env null or kInitSuc:%d", kInitSuc);
            }
            else {
                //jni获取javabridge中的thread和堆栈信息
                jobject java_context_obj = env->CallStaticObjectMethod(kJavaBridgeClass, kMethodIDGetJavaContext);
                if (NULL == java_context_obj)
                {
                    return;
                }

                jstring j_stack = (jstring)env->GetObjectField(java_context_obj, kFieldIDStack);
                jstring j_thread_name = (jstring)env->GetObjectField(java_context_obj, kFieldIDThreadName);

                char *thread_name = jstringToChars(env, j_thread_name);
                char *stack = jstringToChars(env, j_stack);
                JavaContext java_context(GetCurrentThreadId(), thread_name == NULL ? "" : thread_name, stack == NULL ? "" : stack);
                free(stack);
                free(thread_name);
                fdcanary::FDCanary::Get().OnOpen(pathname, flags, mode, ret, java_context);
                env->DeleteLocalRef(java_context_obj);
                env->DeleteLocalRef(j_stack);
                env->DeleteLocalRef(j_thread_name);
            }
        }


    int ProxyOpen(const char *pathname, int flags, mode_t mode)
    {
         /*if (!IsMainThread()) {
             __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyOpen not main thread");
             return original_open(pathname, flags, mode);
          }*/

         int ret = original_open(pathname, flags, mode);
        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyOpen pathname:%s, flags:%d, ret:%d", pathname, flags, ret);
         if (ret != -1)
         {
             DoProxyOpenLogic(pathname, flags, mode, ret);
         }

         return ret;
        }

    int ProxyOpen64(const char *pathname, int flags, mode_t mode)
    {
        /*if (!IsMainThread()) {
            __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyOpen64 not main thread");
            return original_open64(pathname, flags, mode);
        }*/
        int ret = original_open64(pathname, flags, mode);

        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyOpen64 pathname:%s, flags:%d, ret:%d", pathname, flags, ret);
        if (ret != -1)
        {
            DoProxyOpenLogic(pathname, flags, mode, ret);
        }

        return ret;
    }


    /**
         *  Proxy for close: callback to the java layer
         */
    int ProxyClose(int fd)
    {
        /*if (!IsMainThread()) {
            __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyClose not main thread");
            return original_close(fd);
        }*/

        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyClose fd:%d", fd);
        fdcanary::FDCanary::Get().OnClose(fd);
        int ret = original_close(fd);

        //__android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyClose fd:%d ret:%d", fd, ret);
        
        return ret;
    }

    int ProxyAshMemCreateRegion(const char *name, size_t size) {
        
        int result = original_ashmem_create_region(name, size);

        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyAshMemCreateRegion， name:%s,size:%zu,result:%d", name, size, result);
        fdcanary::FDCanary::Get().AshmemCreateRegion(name, size, result);
        return result;
    }

    int ProxyEpollCreate(int size) {
        int epoll_fd = original_epoll_create(size);
        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyEpollCreate, epoll_fd:%d", epoll_fd);
        return epoll_fd;
    }

    int ProxySocket(int domain,int type,int protocol) {

        int ret = original_socket(domain, type, protocol);

        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxySocket domain:%d, type:%d, protocol:%d, ret:%d",domain, type, protocol, ret);
        fdcanary::FDCanary::Get().Socket(ret);

        return ret;
    }

    int ProxyShutDown(int s,int how) {

        int ret = original_shutdown(s, how);

        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyShutDown s:%d, how:%d, ret:%d", s, how, ret);
        fdcanary::FDCanary::Get().ShutDown(ret);
        return ret;
    }

    int ProxyPipe(int filedes[2]) {
        int ret = original_pipe(filedes);

        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyPipe filedes[0]:%d, filedes[1]:%d, ret:%d", filedes[0], filedes[1], ret);

        return ret;
    }

    FILE* ProxyPOpen(const char * command,const char * type) {
        FILE* file = popen(command, type);

        __android_log_print(ANDROID_LOG_DEBUG, kTag, "ProxyPOpen command:%s, type:%s", command, type);
        return file;
    }

    void dumpFdInfo()
    {
        time_t t1;
        time(&t1);
       
        QueryFD fd_info;
        std::vector<std::string> infos;
        fd_info.QueryFDInfo(1024, infos);
            __android_log_print(ANDROID_LOG_DEBUG, "FDCanary.JNI", "result size is [%zu]", infos.size());
        for(auto s : infos) {
            __android_log_print(ANDROID_LOG_DEBUG, "FDCanary.JNI", "result is %s", s.c_str());
        }
         
        time_t t2;
        time(&t2);
        __android_log_print(ANDROID_LOG_WARN, "FDCanary.JNI", "dumpFdInfo t1:[%ld], t2:[%ld], speed time:%ld",t1, t2, (t2-t1));
    }

    void HookIo() {
        for (int i = 0; i < TARGET_MODULE_COUNT_IO; i++)
        {
            const char *so_name = TARGET_MODULES_IO[i];
            __android_log_print(ANDROID_LOG_INFO, kTag, "try to hook function in %s.", so_name);

            loaded_soinfo *soinfo = elfhook_open(so_name);
            if (!soinfo)
            {
                __android_log_print(ANDROID_LOG_WARN, kTag, "Failure to open %s, try next.", so_name);
                continue;
            }

            int result1 = elfhook_replace(soinfo, "open", (void *)ProxyOpen, (void **)&original_open);
            int result2 = elfhook_replace(soinfo, "open64", (void *)ProxyOpen64, (void **)&original_open64);
            int result3 = elfhook_replace(soinfo, "close", (void*)ProxyClose, (void**)&original_close);
            __android_log_print(ANDROID_LOG_WARN, kTag, "doHook hook elfhook_replace, result1:%d, result2:%d, result3:%d",
                                result1, result2, result3);
        }
    }

    void HookAshmem() {
        for (int i = 0; i < TARGET_MODULE_COUNT_ASHMEM; i++)
        {
            const char *so_name = TARGET_MODULES_ASHMEM[i];
            __android_log_print(ANDROID_LOG_INFO, kTag, "proxyAshmem try to hook function in %s.", so_name);

            loaded_soinfo *soinfo = elfhook_open(so_name);
            if (!soinfo)
            {
                __android_log_print(ANDROID_LOG_WARN, kTag, "Failure to open %s, try next.", so_name);
                continue;
            }

            int result = elfhook_replace(soinfo, "ashmem_create_region", (void *)ProxyAshMemCreateRegion, (void **)&original_ashmem_create_region);
            __android_log_print(ANDROID_LOG_WARN, kTag, "doHook hook elfhook_replace, result:%d",
                                result);
        } 
    }

    //try to hook epoll fail
    void HookEpoll() {
        for (int i = 0; i < TARGET_MODULE_COUNT_EPOLL; i ++) {
            const char *so_name = TARGET_MODULES_EPOLL[i];
            __android_log_print(ANDROID_LOG_INFO, kTag, "HookEpoll try to hook function in %s.", so_name);

            loaded_soinfo *soinfo = elfhook_open(so_name);
            if (!soinfo)
            {
                __android_log_print(ANDROID_LOG_WARN, kTag, "Failure to open %s, try next.", so_name);
                continue;
            }

            int result = elfhook_replace(soinfo, "epoll_create", (void *)ProxyEpollCreate, (void **)&original_epoll_create);
            __android_log_print(ANDROID_LOG_WARN, kTag, "doHook hook elfhook_replace, result:%d", result);
        }
    }

    void HookSocket() {
        for (int i = 0; i < TARGET_MODULE_COUNT_SOCKET; i ++) {
            const char *so_name = TARGET_MODULES_SOCKET[i];
            __android_log_print(ANDROID_LOG_INFO, kTag, "HookSocket try to hook function in %s.", so_name);

            loaded_soinfo *soinfo = elfhook_open(so_name);
            if (!soinfo)
            {
                __android_log_print(ANDROID_LOG_WARN, kTag, "Failure to open %s, try next.", so_name);
                continue;
            }

            int result = elfhook_replace(soinfo, "socket", (void *)ProxySocket, (void **)&original_socket);
            int result1 = elfhook_replace(soinfo, "shutdown", (void *)ProxyShutDown, (void **)&original_shutdown);
            
            __android_log_print(ANDROID_LOG_WARN, kTag, "doHook hook elfhook_replace, result:%d，result1:%d"
            , result, result1);
        }
    }

    void HookPipe() {
        for (int i = 0; i < TARGET_MODULE_COUNT_PIPE; i ++) {
            const char *so_name = TARGET_MODULES_PIPE[i];
            __android_log_print(ANDROID_LOG_INFO, kTag, "HookPipe try to hook function in %s.", so_name);

            loaded_soinfo *soinfo = elfhook_open(so_name);
            if (!soinfo)
            {
                __android_log_print(ANDROID_LOG_WARN, kTag, "Failure to open %s, try next.", so_name);
                continue;
            }

            int result = elfhook_replace(soinfo, "pipe", (void *)ProxyPipe, (void **)&original_pipe);
            int result1 = elfhook_replace(soinfo, "popen", (void *)ProxyPOpen, (void **)&original_popen);
            
            __android_log_print(ANDROID_LOG_WARN, kTag, "doHook hook elfhook_replace, result:%d, result1:%d"
            , result,result1);
        }
    }

    JNIEXPORT jboolean JNICALL
    Java_com_tencent_matrix_fdcanary_core_FDCanaryJniBridge_doHook(JNIEnv *env, jclass type)
    {
        __android_log_print(ANDROID_LOG_INFO, kTag, "doHook");

        
        HookIo();
        HookAshmem();
        HookEpoll();
        HookSocket();
        HookPipe();
        return true;
    }

    JNIEXPORT jboolean JNICALL
    Java_com_tencent_matrix_fdcanary_core_FDCanaryJniBridge_doUnHook(JNIEnv *env, jclass type)
    {
        //todo close的选择TARGET_MODULE_COUNT_IO
        __android_log_print(ANDROID_LOG_INFO, kTag, "doUnHook");
        for (int i = 0; i < TARGET_MODULE_COUNT_IO; ++i)
        {
            const char *so_name = TARGET_MODULES_IO[i];
            loaded_soinfo *soinfo = elfhook_open(so_name);
            if (!soinfo)
            {
                continue;
            }
            elfhook_replace(soinfo, "open", (void *)original_open, nullptr);
            elfhook_replace(soinfo, "open64", (void *)original_open64, nullptr);
            elfhook_replace(soinfo, "close", (void *)original_close, nullptr);
            elfhook_replace(soinfo, "ashmem_create_region", (void *)original_ashmem_create_region, nullptr);
            elfhook_replace(soinfo, "epoll_create", (void *)original_epoll_create, nullptr);
            //elfhook_replace(soinfo, "socket", (void *)original_close, nullptr);
            //elfhook_replace(soinfo, "socket_close", (void *)original_close, nullptr);
            elfhook_close(soinfo);
        }
        return true;
    }

    static bool InitJniEnv(JavaVM *vm)
    {
        kJvm = vm;
        JNIEnv *env = NULL;
        if (kJvm->GetEnv((void **)&env, JNI_VERSION_1_6) != JNI_OK)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv GetEnv !JNI_OK");
            return false;
        }

        jclass temp_cls = env->FindClass("com/tencent/matrix/fdcanary/core/FDCanaryJniBridge");
        if (temp_cls == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv kJavaBridgeClass NULL");
            return false;
        }
        kJavaBridgeClass = reinterpret_cast<jclass>(env->NewGlobalRef(temp_cls));

        jclass temp_java_context_cls = env->FindClass("com/tencent/matrix/fdcanary/core/FDCanaryJniBridge$JavaContext");
        if (temp_java_context_cls == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv kJavaBridgeClass NULL");
            return false;
        }
        kJavaContextClass = reinterpret_cast<jclass>(env->NewGlobalRef(temp_java_context_cls));
        kFieldIDStack = env->GetFieldID(kJavaContextClass, "stack", "Ljava/lang/String;");
        kFieldIDThreadName = env->GetFieldID(kJavaContextClass, "threadName", "Ljava/lang/String;");
        if (kFieldIDStack == NULL || kFieldIDThreadName == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv kJavaContextClass field NULL");
            return false;
        }

        kMethodIDOnIssuePublish = env->GetStaticMethodID(kJavaBridgeClass, "onIssuePublish", "(Ljava/util/ArrayList;)V");
        if (kMethodIDOnIssuePublish == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv kMethodIDOnIssuePublish NULL");
            return false;
        }

        kMethodIDGetJavaContext = env->GetStaticMethodID(kJavaBridgeClass, "getJavaContext", "()Lcom/tencent/matrix/fdcanary/core/FDCanaryJniBridge$JavaContext;");
        if (kMethodIDGetJavaContext == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv kMethodIDGetJavaContext NULL");
            return false;
        }

        jclass temp_issue_cls = env->FindClass("com/tencent/matrix/fdcanary/core/FDIssue");
        if (temp_issue_cls == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv kIssueClass NULL");
            return false;
        }
        kIssueClass = reinterpret_cast<jclass>(env->NewGlobalRef(temp_issue_cls));

        kMethodIDIssueConstruct = env->GetMethodID(kIssueClass, "<init>", "(ILjava/lang/String;JIJJIJLjava/lang/String;Ljava/lang/String;I)V");
        if (kMethodIDIssueConstruct == NULL)
        {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv kMethodIDIssueConstruct NULL");
            return false;
        }
        kMethodIDIssueConstruct2 = env->GetMethodID(kIssueClass, "<init>", "(ILjava/lang/String;)V");
        if (kMethodIDIssueConstruct2 == NULL) {
            __android_log_print(ANDROID_LOG_ERROR, kTag, "InitJniEnv kMethodIDIssueConstruct2 NULL");
            return false;
        }

        jclass list_cls = env->FindClass("java/util/ArrayList");
        kListClass = reinterpret_cast<jclass>(env->NewGlobalRef(list_cls));
        kMethodIDListConstruct = env->GetMethodID(list_cls, "<init>", "()V");
        kMethodIDListAdd = env->GetMethodID(list_cls, "add", "(Ljava/lang/Object;)Z");

        return true;
    }

    JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved)
    {
        __android_log_print(ANDROID_LOG_DEBUG, kTag, "JNI_OnLoad");
        kInitSuc = false;

        if (!InitJniEnv(vm))
        {
            return -1;
        }

        fdcanary::FDCanary::Get().SetIssuedCallback(OnIssuePublish);
        kInitSuc = true;
        __android_log_print(ANDROID_LOG_DEBUG, kTag, "JNI_OnLoad done");
        return JNI_VERSION_1_6;
    }

    JNIEXPORT void JNICALL JNI_OnUnload(JavaVM *vm, void *reserved)
    {
        __android_log_print(ANDROID_LOG_DEBUG, kTag, "JNI_OnUnload done");
        JNIEnv *env;
        kJvm->GetEnv((void **)&env, JNI_VERSION_1_6);
        if (env != NULL)
        {
            if (kIssueClass)
            {
                env->DeleteGlobalRef(kIssueClass);
            }
            if (kJavaBridgeClass)
            {
                env->DeleteGlobalRef(kJavaBridgeClass);
            }
            if (kListClass)
            {
                env->DeleteGlobalRef(kListClass);
            }
        }
    }

    JNIEXPORT void JNICALL
    Java_com_tencent_matrix_fdcanary_core_FDCanaryJniBridge_dumpFdInfo(JNIEnv *env, jclass type)
    {
        dumpFdInfo();
    }

    //todo pipe not support
    JNIEXPORT void JNICALL
    Java_com_tencent_matrix_fdcanary_core_FDCanaryJniBridge_nativeTestPIPE(JNIEnv *env, jclass type) {

        int filedes[2];
        char buffer[80];
        int result = pipe(filedes);

        __android_log_print(ANDROID_LOG_DEBUG, kTag, "nativeTestPIPE result:[%d]", result);
        if (fork() > 0) {
            /* 父进程*/
            char s[ ] = "hello!\n";
            write(filedes[1],s,sizeof(s));
            __android_log_print(ANDROID_LOG_DEBUG, kTag, "nativeTestPIPE buffer:%s", buffer);
        } else {
            read(filedes[0], buffer, 80);
            __android_log_print(ANDROID_LOG_DEBUG, kTag, "nativeTestPIPE buffer:%s", buffer);
        }
        
    }

}
} // namespace fdcanary
