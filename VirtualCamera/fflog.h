#ifndef __FF_LOG_H__
#define __FF_LOG_H__

#include <stdio.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/time.h>
#include <assert.h>

#define DEBUG 1

#ifndef CAPI
#ifdef __cplusplus
#define CAPI extern "C"
#else
#define CAPI
#endif
#endif

static unsigned long long getTimestampMS()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long long)tv.tv_usec/1000 + (unsigned long long)tv.tv_sec*1000;
}

//static unsigned long getTimestampUS()
//{
//    struct timeval tv;
//    gettimeofday(&tv, NULL);
//    return (unsigned long)(tv.tv_usec*1e-3 + tv.tv_sec*1000);
//}

static unsigned long getTimestamp()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)(tv.tv_usec/1000000 + tv.tv_sec);
}

#ifdef __ANDROID__

#include <android/log.h>
#include <jni.h>

#ifndef TAG
#define TAG "jrtplib"
#endif

#if DEBUG

    #define LOGV(...) __android_log_print(ANDROID_LOG_VERBOSE, TAG, __VA_ARGS__)
    #define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG ,  TAG, __VA_ARGS__)
    #define LOGI(...) __android_log_print(ANDROID_LOG_INFO ,   TAG, __VA_ARGS__)
    #define LOGW(...) __android_log_print(ANDROID_LOG_WARN ,   TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR ,  TAG, __VA_ARGS__)

    #define LOGFV(x, ...) LOGV("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)
    #define LOGFD(x, ...) LOGD("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)
    #define LOGFI(x, ...) LOGI("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)
    #define LOGFW(x, ...) LOGW("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)
    #define LOGFE(x, ...) LOGE("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)

    #define printf(x, ...) LOGD("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)

#else

    #define LOGV(...)       NULL
    #define LOGD(...)       NULL
    #define LOGI(...)       NULL
    #define LOGW(...)       NULL
    #define LOGE(...)       NULL

    #define LOGFV(x, ...)   NULL
    #define LOGFD(x, ...)   NULL
    #define LOGFI(x, ...)   NULL
    #define LOGFW(x, ...)   NULL
    #define LOGFE(x, ...)   NULL

#endif //endif Debug




#ifdef __cplusplus
    static int jniThrowException(JNIEnv *env, const char *className, const char *msg) {
        if (env->ExceptionCheck()) {
            /* consider creating the new exception with this as "cause" */
            env->ExceptionOccurred();
            env->ExceptionClear();
        }

        jclass jcls = env->FindClass(className);
        if (jcls == NULL) {
            LOGE("Unable to find exception class %s", className);
            /* ClassNotFoundException now pending */
            return -1;
        }

        if (env->ThrowNew(jcls, msg) != JNI_OK) {
            LOGE("Failed throwing '%s' '%s'", className, msg);
            /* an exception, most likely OOM, will now be pending */
            return -1;
        }
        return 0;
    }
#else
    static int jniThrowException(JNIEnv *env, const char *className, const char *msg) {
        if ((*env)->ExceptionCheck(env)) {
            /* consider creating the new exception with this as "cause" */
            (*env)->ExceptionOccurred(env);
            (*env)->ExceptionClear(env);
        }

        jclass jcls = (*env)->FindClass(env, className);
        if (jcls == NULL) {
            LOGE("Unable to find exception class %s", className);
            /* ClassNotFoundException now pending */
            return -1;
        }

        if ((*env)->ThrowNew(env, jcls, msg) != JNI_OK) {
            LOGE("Failed throwing '%s' '%s'", className, msg);
            /* an exception, most likely OOM, will now be pending */
            return -1;
        }
        return 0;
    }
#endif




#else //非Android平台

#define LOG(format, ...) \
do { \
    fprintf(stderr, "[%s|%s|%d]:" format "\n", basename(__FILE__), __FUNCTION__, __LINE__, ##__VA_ARGS__ ); \
} while (0)

#define LOGV(x,...) printf(x"\n",##__VA_ARGS__)
#define LOGD(x,...) printf(x"\n",##__VA_ARGS__)
#define LOGI(x,...) printf(x"\n",##__VA_ARGS__)
#define LOGW(x,...) printf(x"\n",##__VA_ARGS__)
#define LOGE(x,...) printf(x"\n",##__VA_ARGS__)

#define LOGFV LOG
#define LOGFD LOG
#define LOGFI LOG
#define LOGFW LOG
#define LOGFE LOG

#endif  //end of else ********

#define CHECK(CONDITION) { if(CONDITION) { assert(0); } }
#define CHECK_EQ(X, Y) { if(X != Y) assert(0); }
#define CHECK_NE(X, Y) { if(X == Y) assert(0); }
#define CHECK_GE(X, Y) { if(X < Y ) assert(0); }
#define CHECK_GT(X, Y) { if(X <= Y) assert(0); }
#define CHECK_LE(X, Y) { if(X > Y ) assert(0); }
#define CHECK_LT(X, Y) { if(X >= Y) assert(0); }

#define BEGIN LOGFD(" + ");
#define END   LOGFD(" - ");

//检查空指针,无返回值.
#define CHECK_NULL(p)  \
{ \
    if(p == NULL) { \
        LOGFD("error : null-pointer!"); \
        return; \
    } \
}

#define CHECK_NULL_ASSERT(p)  \
{ \
    if(p == NULL) { \
        LOGFD("error : null-pointer!"); \
        assert(0); \
    } \
}

//检查空指针,返回r值.
#define CHECK_NULL_R(p, r)  \
{ \
    if(p == NULL) { \
        LOGFD("error : null-pointer!"); \
        return r; \
    } \
}

#define CHECK_NULL_INFO(p, info)  \
{ \
    if(p == NULL) { \
        LOGFD("error : null-pointer! info:%s", info); \
        return; \
    } \
}

#define CHECK_NULL_INFO_R(p, info, r)  \
{ \
    if(p == NULL) { \
        LOGFD("error : null-pointer! info:%s", info); \
        return r; \
    } \
}

#endif

