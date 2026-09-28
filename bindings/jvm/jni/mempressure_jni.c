#include <mempressure.h>

#include <errno.h>
#include <jni.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static JavaVM *g_vm;

jint JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)reserved;
    g_vm = vm;
    return JNI_VERSION_1_6;
}

typedef struct {
    int handle;
    jobject cb_ref;
    jmethodID mid;
} jsub_t;

static struct {
    pthread_mutex_t lock;
    jsub_t **items;
    size_t count;
    size_t capacity;
} g_registry = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
};

static int registry_add(jsub_t *js) {
    pthread_mutex_lock(&g_registry.lock);
    if (g_registry.count == g_registry.capacity) {
        if (g_registry.capacity > ((size_t)-1) / 2) {
            pthread_mutex_unlock(&g_registry.lock);
            return -ENOMEM;
        }
        size_t next = g_registry.capacity ? g_registry.capacity * 2 : 8;
        jsub_t **grown = realloc(g_registry.items, next * sizeof *grown);
        if (!grown) {
            pthread_mutex_unlock(&g_registry.lock);
            return -ENOMEM;
        }
        g_registry.items = grown;
        g_registry.capacity = next;
    }
    g_registry.items[g_registry.count++] = js;
    pthread_mutex_unlock(&g_registry.lock);
    return 0;
}

static jsub_t *registry_take(int handle) {
    pthread_mutex_lock(&g_registry.lock);
    for (size_t i = 0; i < g_registry.count; i++) {
        if (g_registry.items[i]->handle == handle) {
            jsub_t *found = g_registry.items[i];
            g_registry.items[i] = g_registry.items[g_registry.count - 1];
            g_registry.count--;
            pthread_mutex_unlock(&g_registry.lock);
            return found;
        }
    }
    pthread_mutex_unlock(&g_registry.lock);
    return NULL;
}

static void jni_trampoline(mp_level_t level, void *userdata) {
    jsub_t *js = userdata;
    JNIEnv *env = NULL;
    int attached = 0;
    if ((*g_vm)->GetEnv((void *)g_vm, (void **)&env, JNI_VERSION_1_6) != JNI_OK) {
        if ((*g_vm)->AttachCurrentThreadAsDaemon((void *)g_vm, (void **)&env, NULL) != JNI_OK) {
            return;
        }
        attached = 1;
    }
    (*env)->CallVoidMethod(env, js->cb_ref, js->mid, (jint)level);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }
    (void)attached;  // stay attached as a daemon thread for the process lifetime
}

JNIEXPORT void JNICALL Java_io_kutu_mempressure_MemPressure_start(
    JNIEnv *env, jclass cls, jobject jcfg) {
    (void)cls;
    mp_config_t cfg = {0};
    cfg.low_threshold = 5.0;
    cfg.moderate_threshold = 15.0;
    cfg.critical_threshold = 40.0;
    cfg.poll_interval_sec = 0.5;
    cfg.hysteresis = 2;
    cfg.psi_path = NULL;

    jclass c = (*env)->GetObjectClass(env, jcfg);
    jfieldID f_low = (*env)->GetFieldID(env, c, "low", "D");
    jfieldID f_moderate = (*env)->GetFieldID(env, c, "moderate", "D");
    jfieldID f_critical = (*env)->GetFieldID(env, c, "critical", "D");
    jfieldID f_interval = (*env)->GetFieldID(env, c, "intervalSec", "D");
    jfieldID f_hysteresis = (*env)->GetFieldID(env, c, "hysteresis", "I");
    jfieldID f_path = (*env)->GetFieldID(env, c, "psiPath", "Ljava/lang/String;");
    if (!f_low || !f_moderate || !f_critical || !f_interval || !f_hysteresis || !f_path) {
        return;
    }
    cfg.low_threshold = (*env)->GetDoubleField(env, jcfg, f_low);
    cfg.moderate_threshold = (*env)->GetDoubleField(env, jcfg, f_moderate);
    cfg.critical_threshold = (*env)->GetDoubleField(env, jcfg, f_critical);
    cfg.poll_interval_sec = (*env)->GetDoubleField(env, jcfg, f_interval);
    cfg.hysteresis = (*env)->GetIntField(env, jcfg, f_hysteresis);
    jstring jpath = (*env)->GetObjectField(env, jcfg, f_path);
    char *path_copy = NULL;
    if (jpath) {
        const char *path = (*env)->GetStringUTFChars(env, jpath, NULL);
        if (path) {
            path_copy = strdup(path);
            cfg.psi_path = path_copy;
            (*env)->ReleaseStringUTFChars(env, jpath, path);
        }
    }
    int rc = mp_init(&cfg);
    free(path_copy);
    if (rc != 0 && rc != -EALREADY) {
        (*env)->ThrowNew(
            env, (*env)->FindClass(env, "io/kutu/mempressure/MemPressure$MemPressureException"),
            "mp_init failed");
    }
}

JNIEXPORT void JNICALL Java_io_kutu_mempressure_MemPressure_stop(JNIEnv *env, jclass cls) {
    (void)cls;
    mp_shutdown();
    pthread_mutex_lock(&g_registry.lock);
    for (size_t i = 0; i < g_registry.count; i++) {
        (*env)->DeleteGlobalRef(env, g_registry.items[i]->cb_ref);
        free(g_registry.items[i]);
    }
    g_registry.count = 0;
    pthread_mutex_unlock(&g_registry.lock);
}

JNIEXPORT jint JNICALL Java_io_kutu_mempressure_MemPressure_currentLevel(JNIEnv *env, jclass cls) {
    (void)env;
    (void)cls;
    return (jint)mp_current_level();
}

JNIEXPORT jdoubleArray JNICALL Java_io_kutu_mempressure_MemPressure_psi(JNIEnv *env, jclass cls) {
    (void)cls;
    mp_psi_t psi = {0};
    if (mp_psi(&psi) != 0) {
        jclass ex = (*env)->FindClass(env, "io/kutu/mempressure/MemPressure$MemPressureException");
        if (ex) {
            (*env)->ThrowNew(env, ex, "monitor not started");
        }
        return NULL;
    }
    jdoubleArray out = (*env)->NewDoubleArray(env, 6);
    if (!out) {
        return NULL;
    }
    double values[6] = {
        psi.some_avg10, psi.some_avg60, psi.some_avg300,
        psi.full_avg10, psi.full_avg60, psi.full_avg300,
    };
    (*env)->SetDoubleArrayRegion(env, out, 0, 6, values);
    return out;
}

JNIEXPORT jint JNICALL Java_io_kutu_mempressure_MemPressure_subscribe(
    JNIEnv *env, jclass cls, jobject cb) {
    (void)cls;
    jsub_t *js = malloc(sizeof *js);
    if (!js) {
        return -ENOMEM;
    }
    js->cb_ref = (*env)->NewGlobalRef(env, cb);
    if (!js->cb_ref) {
        free(js);
        return -ENOMEM;
    }
    jclass c = (*env)->GetObjectClass(env, cb);
    js->mid = (*env)->GetMethodID(env, c, "onPressure", "(I)V");
    int handle = mp_subscribe(jni_trampoline, js);
    if (handle < 0) {
        (*env)->DeleteGlobalRef(env, js->cb_ref);
        free(js);
        return handle;
    }
    js->handle = handle;
    if (registry_add(js) != 0) {
        mp_unsubscribe(handle);
        (*env)->DeleteGlobalRef(env, js->cb_ref);
        free(js);
        jclass oom = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
        if (oom) {
            (*env)->ThrowNew(env, oom, "subscription registry growth failed");
        }
        return -ENOMEM;
    }
    return handle;
}

JNIEXPORT void JNICALL Java_io_kutu_mempressure_MemPressure_unsubscribe(
    JNIEnv *env, jclass cls, jint handle) {
    (void)cls;
    jsub_t *js = registry_take(handle);
    if (js) {
        mp_unsubscribe(handle);
        (*env)->DeleteGlobalRef(env, js->cb_ref);
        free(js);
    }
}
