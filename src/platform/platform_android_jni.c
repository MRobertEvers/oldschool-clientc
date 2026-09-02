/*
 * platform_android_jni.c -- the Java side of the Android lane, and the only
 * file in this tree that knows what a jobject is.
 *
 * Its whole job is to turn an Android activity's lifecycle into the three
 * things the client actually needs -- a surface, a stream of input, and a
 * thread to run the frame loop on -- and then get out of the way. It draws
 * nothing and decides nothing about a frame; platform_android.c does that, and
 * the two meet at platform_android.h.
 *
 * WHY A THREAD
 *
 * main.c's loop is `while( frame_loop_step() )`: a blocking loop that owns the
 * stack it runs on, and the shape every native lane uses. Android's UI thread
 * cannot host it -- a blocked UI thread is an ANR in five seconds. The web lane
 * had to invert its loop for the same reason (it hands control to
 * requestAnimationFrame and never returns), and doing that a second time would
 * put two different loop shapes in one file for two different reasons. A thread
 * lets Android run the loop exactly the way Linux and Windows do.
 *
 * So: the UI thread receives callbacks and appends to queues; the frame thread
 * calls main(). Everything they share is behind the one mutex in
 * platform_android.c.
 *
 * WHERE stdout AND stderr GO
 *
 * Nowhere, by default -- Android discards both, which would silently swallow
 * every TORIRS_REPORT, every boot diagnostic, and every assert message this
 * client emits. They are re-pointed at logcat below, so `adb logcat -s torirs`
 * shows what a terminal would show on any other host. Without this, a failed
 * boot on a device is a black screen with no explanation anywhere.
 */

#include "platform/platform_android.h"

#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>

#include <fcntl.h>
#include <pthread.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ANDROID_LOG_TAG "torirs"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, ANDROID_LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, ANDROID_LOG_TAG, __VA_ARGS__)

/** main() lives in src/main.c and is linked into this shared library. */
extern int
main(int argc, char** argv);

/* ---- argv ----------------------------------------------------------------
 *
 * Built on the UI thread from what the boot screen chose, and read by the frame
 * thread. Copied rather than referenced because the Java strings it came from
 * are released the moment the JNI call returns.
 *
 * The client's own argument parser is what consumes these -- `--manifest
 * <path>` and everything else -- so the Android app configures the client the
 * same way the desktop command line does. There is no Android-specific
 * configuration path, which is what keeps a manifest meaning the same thing on
 * a phone as it does on a desktop.
 */
#define ANDROID_ARGV_MAX 32
static char* g_argv[ANDROID_ARGV_MAX];
static int g_argc;

static void
argv_clear(void)
{
    for( int i = 0; i < g_argc; i++ )
        free(g_argv[i]);
    g_argc = 0;
}

static void
argv_push(char const* value)
{
    char* copy;

    if( g_argc >= ANDROID_ARGV_MAX )
    {
        LOGE("argv full, dropping '%s'", value);
        return;
    }
    copy = strdup(value);
    /* An allocation failure here is an assertion, not a case to handle: an argv
     * missing its manifest boots a client that silently loads something else. */
    if( !copy )
        abort();
    g_argv[g_argc++] = copy;
}

static JavaVM* g_vm;
static jobject g_activity;      /* global ref, held for the app's life */
static jmethodID g_show_keyboard;
static jmethodID g_boot_failed;
/**
 * The frame thread has been attached to the JVM.
 *
 * It MUST be detached before the thread exits. ART does not treat that as a
 * leak to be tidied up later -- it aborts the process with "Native thread
 * exited without calling DetachCurrentThread", which surfaces as a SIGSEGV in
 * a thread with no relation to the code that attached. Tracked here because
 * the attach happens on demand (the first soft-keyboard call) and the detach
 * happens somewhere else entirely (the end of frame_thread).
 */
static int g_frame_thread_attached;

/* ---- the frame thread ---------------------------------------------------- */

static pthread_t g_thread;
static int g_thread_started;

static void*
frame_thread(void* unused)
{
    int rc;

    (void)unused;
    LOGI("frame thread: entering main() with %d args", g_argc);
    for( int i = 0; i < g_argc; i++ )
        LOGI("  argv[%d] = %s", i, g_argv[i]);

    rc = main(g_argc, g_argv);

    /*
     * Detach BEFORE returning. A thread that attached to the JVM and then exits
     * without detaching aborts the whole process -- ART checks this on thread
     * exit and calls it fatal, and the crash it produces names a thread id with
     * nothing to connect it back to the attach.
     */
    if( g_frame_thread_attached && g_vm )
    {
        (*g_vm)->DetachCurrentThread(g_vm);
        g_frame_thread_attached = 0;
    }

    LOGI("frame thread: main() returned %d", rc);
    return NULL;
}

/* ---- stdout/stderr -> logcat --------------------------------------------
 *
 * A pipe stands in for the two descriptors and a pump thread copies whatever
 * lands in it to the log, line by line. This is the standard Android trick and
 * it is the only way to see a printf from native code.
 */
static int g_log_pipe[2];
static pthread_t g_log_thread;

static void*
log_pump(void* unused)
{
    char line[512];
    ssize_t used = 0;

    (void)unused;
    for( ;; )
    {
        ssize_t n = read(g_log_pipe[0], line + used, sizeof(line) - 1 - (size_t)used);
        if( n <= 0 )
            break;
        used += n;
        line[used] = '\0';

        /* Emit whole lines; hold a partial one until its newline arrives, so a
         * printf split across two writes is not logged as two lines. */
        for( ;; )
        {
            char* nl = strchr(line, '\n');
            if( !nl )
                break;
            *nl = '\0';
            __android_log_write(ANDROID_LOG_INFO, ANDROID_LOG_TAG, line);
            used -= (nl + 1 - line);
            memmove(line, nl + 1, (size_t)used + 1);
        }
        /* A line longer than the buffer is flushed rather than dropped. */
        if( used >= (ssize_t)sizeof(line) - 1 )
        {
            __android_log_write(ANDROID_LOG_INFO, ANDROID_LOG_TAG, line);
            used = 0;
        }
    }
    return NULL;
}

/*
 * exit() ends the process while the pump thread may still be between the
 * pipe and logcat -- and the line it is holding is the one that says why the
 * client exited (every die()/exit(1) path prints first). Drain whatever is
 * still in the pipe, synchronously, from the exiting thread. The pipe end is
 * switched to non-blocking so a drain with nothing to read returns at once.
 */
static void
drain_log_pipe_at_exit(void)
{
    char line[512];
    int flags;
    ssize_t n;

    if( g_log_pipe[0] <= 0 )
        return;
    flags = fcntl(g_log_pipe[0], F_GETFL, 0);
    if( flags >= 0 )
        fcntl(g_log_pipe[0], F_SETFL, flags | O_NONBLOCK);
    while( (n = read(g_log_pipe[0], line, sizeof(line) - 1)) > 0 )
    {
        line[n] = '\0';
        __android_log_write(ANDROID_LOG_INFO, ANDROID_LOG_TAG, line);
    }
    __android_log_write(ANDROID_LOG_INFO, ANDROID_LOG_TAG, "process exiting (log pipe drained)");
}

static void
redirect_stdio_to_log(void)
{
    static int done;

    if( done )
        return;
    done = 1;

    /* Unbuffered, or a crash loses everything printed since the last flush --
     * which is exactly the output that explains the crash. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if( pipe(g_log_pipe) != 0 )
        return;
    dup2(g_log_pipe[1], STDOUT_FILENO);
    dup2(g_log_pipe[1], STDERR_FILENO);
    pthread_create(&g_log_thread, NULL, log_pump, NULL);
    pthread_detach(g_log_thread);
    atexit(drain_log_pipe_at_exit);
}

/* ---- the soft keyboard ---------------------------------------------------
 *
 * PlatformWindow_SetTextInput's implementation. InputMethodManager is Java-only,
 * so the actual show/hide is a method on the activity and this reaches back up
 * to call it.
 */


void
PlatformAndroidJni_SetSoftKeyboard(int on)
{
    JNIEnv* env = NULL;
    int attached = 0;

    if( !g_vm || !g_activity || !g_show_keyboard )
        return;

    /*
     * This runs on the FRAME thread, which the JVM has never seen. A thread
     * that calls into Java must be attached first, and detached before it
     * exits -- but the frame thread outlives every one of these calls, so it is
     * attached once here and left attached; DetachCurrentThread on a thread
     * that will call again would just make the next call re-attach.
     */
    if( (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK )
    {
        if( (*g_vm)->AttachCurrentThread(g_vm, &env, NULL) != JNI_OK )
            return;
        attached = 1;
    }
    /*
     * Attached and LEFT attached, deliberately: this is called once per focus
     * change and re-attaching each time would be pure overhead. The debt is
     * settled at the end of frame_thread, which is the only place that knows
     * the thread is about to stop existing.
     */
    if( attached )
        g_frame_thread_attached = 1;
    (*env)->CallVoidMethod(env, g_activity, g_show_keyboard, (jboolean)(on != 0));
    if( (*env)->ExceptionCheck(env) )
        (*env)->ExceptionClear(env);
}

/* ---- a boot that cannot proceed ------------------------------------------
 *
 * @see PlatformAndroid_BootFailed for why this is not exit().
 */

void
PlatformAndroid_BootFailed(char const* message)
{
    JNIEnv* env = NULL;
    int attached = 0;

    assert(message);
    LOGE("%s", message);

    if( g_vm && g_activity && g_boot_failed )
    {
        if( (*g_vm)->GetEnv(g_vm, (void**)&env, JNI_VERSION_1_6) != JNI_OK )
        {
            if( (*g_vm)->AttachCurrentThread(g_vm, &env, NULL) == JNI_OK )
                attached = 1;
            else
                env = NULL;
        }
        if( attached )
            g_frame_thread_attached = 1;
        if( env )
        {
            jstring text = (*env)->NewStringUTF(env, message);

            /* The call POSTS to the UI thread and returns; it does not wait for
             * the activity to go away. It must not -- the activity's teardown
             * joins this thread, and waiting for it here would be each thread
             * waiting on the other. */
            (*env)->CallVoidMethod(env, g_activity, g_boot_failed, text);
            if( (*env)->ExceptionCheck(env) )
                (*env)->ExceptionClear(env);
            if( text )
                (*env)->DeleteLocalRef(env, text);
        }
    }

    /*
     * Detach before this thread stops existing. ART treats a thread that exits
     * while still attached as fatal, and the abort it raises would be exactly
     * the crash this function exists to prevent -- with a message naming a
     * thread id and nothing else. frame_thread settles the same debt at its own
     * end; this path never reaches it.
     */
    if( g_frame_thread_attached && g_vm )
    {
        (*g_vm)->DetachCurrentThread(g_vm);
        g_frame_thread_attached = 0;
    }

    /*
     * The client ends; the PROCESS does not. nativeStop's join returns at once
     * for a thread that has already exited, so the activity's teardown is
     * unchanged, and the next nativeStart begins a run in a process that is
     * still alive.
     */
    pthread_exit(NULL);
}

/* ---- JNI entry points ----------------------------------------------------
 *
 * Names are the fully-qualified Java ones; they must match
 * com.torirs.client.ClientActivity exactly. RegisterNatives is deliberately not
 * used: a name mismatch then fails at the call rather than at load, and this way
 * `nm -D libtorirs.so | grep Java_` is enough to check the binding.
 */

/**
 * Put the process where the client expects to be run from.
 *
 * Two things depend on this and neither is an argument the client accepts:
 *
 *   the WORKING DIRECTORY   game/rs_prefs.c opens "preferences.ini" by that
 *                           relative name, and a saved setting has to land
 *                           somewhere writable. An Android process starts with
 *                           its cwd at "/", which is read-only, so without this
 *                           every preference write fails silently.
 *   $HOME                   bootmanifest.c derives the default streamed-cache
 *                           location from it (<home>/torirs_cache/...). Android
 *                           sets no HOME at all, and the manifest loader says
 *                           so out loud when it cannot find one.
 *
 * Both are set here rather than passed as flags because that is what they are
 * on every other host: ambient process state the client reads, not
 * configuration it parses. Adding `--home` would mean adding a concept to the
 * client that only one platform uses.
 */
/*
 * env.txt, one KEY=VALUE per line, applied before main().
 *
 * The renderer's A/B and kernel-selection knobs -- TORIDRAW_FRAME_AB,
 * TORIDRAW_RASTER_BATCH, TORIDRAW_FACE_SORT -- are read with getenv(), which
 * on every other host is set by the shell that launches the client. An Android
 * app has no shell and inherits no environment, so on the one platform whose
 * performance is most worth measuring, every one of those knobs was
 * unreachable and the arm being measured was whatever the build defaulted to.
 *
 * This is the same shape as extra_args.txt next to it and for the same reason:
 * a profile can be tried a different way without rebuilding the APK. It is
 * deliberately not a manifest key -- these are host-level debug knobs, not
 * properties of a world, and putting them in a manifest would ship them.
 */
static void
apply_env_file(char const* data_root)
{
    char path[1024];
    char line[512];
    FILE* f;

    assert(data_root);

    snprintf(path, sizeof(path), "%s/env.txt", data_root);
    f = fopen(path, "r");
    if( !f )
        return; /* optional, and its absence is the normal case */

    while( fgets(line, sizeof(line), f) )
    {
        char* eq;
        char* p = line;
        size_t n;

        while( *p == ' ' || *p == '\t' )
            p++;
        if( *p == '#' || *p == '\n' || *p == '\r' || *p == '\0' )
            continue;
        n = strlen(p);
        while( n > 0 && ( p[n - 1] == '\n' || p[n - 1] == '\r' ) )
            p[--n] = '\0';
        eq = strchr(p, '=');
        if( !eq )
            continue;
        *eq = '\0';
        setenv(p, eq + 1, 1);
        LOGI("env: %s=%s", p, eq + 1);
    }
    fclose(f);
}

static void
place_process(char const* data_root)
{
    if( !data_root || !data_root[0] )
        return;
    if( chdir(data_root) != 0 )
        LOGE("could not chdir to %s -- preferences will not persist", data_root);
    setenv("HOME", data_root, 1);
    LOGI("data root: %s", data_root);
    apply_env_file(data_root);
}

JNIEXPORT void JNICALL
Java_com_torirs_client_ClientActivity_nativeStart(
    JNIEnv* env, jobject thiz, jobjectArray args, jstring data_root)
{
    jsize const count = args ? (*env)->GetArrayLength(env, args) : 0;

    redirect_stdio_to_log();

    if( data_root )
    {
        char const* utf = (*env)->GetStringUTFChars(env, data_root, NULL);
        if( utf )
        {
            place_process(utf);
            (*env)->ReleaseStringUTFChars(env, data_root, utf);
        }
    }

    if( g_thread_started )
    {
        LOGI("nativeStart: already running");
        return;
    }

    /*
     * The process outlives the client, so the previous run's quit latch is
     * still set. Clearing it here -- at the one point that knows a new run is
     * beginning -- is what lets the client be started again in the same
     * process. Without it main() returns before its first frame, and the app
     * boots to a black screen for the rest of the process's life.
     */
    PlatformAndroid_ResetForStart();

    (*env)->GetJavaVM(env, &g_vm);
    g_activity = (*env)->NewGlobalRef(env, thiz);
    {
        jclass cls = (*env)->GetObjectClass(env, thiz);
        g_show_keyboard = (*env)->GetMethodID(env, cls, "showSoftKeyboard", "(Z)V");
        if( (*env)->ExceptionCheck(env) )
            (*env)->ExceptionClear(env);
        g_boot_failed = (*env)->GetMethodID(env, cls, "bootFailed", "(Ljava/lang/String;)V");
        if( (*env)->ExceptionCheck(env) )
            (*env)->ExceptionClear(env);
    }

    argv_clear();
    /* argv[0] is the program name the client prints in its usage line. */
    argv_push("torirs");
    for( jsize i = 0; i < count; i++ )
    {
        jstring js = (jstring)(*env)->GetObjectArrayElement(env, args, i);
        char const* utf;

        if( !js )
            continue;
        utf = (*env)->GetStringUTFChars(env, js, NULL);
        if( utf )
        {
            argv_push(utf);
            (*env)->ReleaseStringUTFChars(env, js, utf);
        }
        (*env)->DeleteLocalRef(env, js);
    }

    if( pthread_create(&g_thread, NULL, frame_thread, NULL) != 0 )
    {
        LOGE("could not start the frame thread");
        return;
    }
    g_thread_started = 1;
}

JNIEXPORT void JNICALL
Java_com_torirs_client_ClientActivity_nativeSurfaceChanged(
    JNIEnv* env, jobject thiz, jobject surface, jint width, jint height)
{
    static ANativeWindow* held;

    (void)thiz;

    /*
     * One reference at a time. ANativeWindow_fromSurface takes one and the
     * caller owns it until it releases it; leaking these is what makes an
     * activity that has been rotated a few times run out of graphics buffers.
     *
     * The new window is acquired BEFORE the old one is released, and published
     * in between, so the frame thread never sees a moment with no window on a
     * plain resize.
     */
    ANativeWindow* next = surface ? ANativeWindow_fromSurface(env, surface) : NULL;

    PlatformAndroid_SetWindow(next, (int)width, (int)height);
    if( held )
        ANativeWindow_release(held);
    held = next;

    LOGI("surface %s: %dx%d", surface ? "changed" : "destroyed", (int)width, (int)height);
}

JNIEXPORT void JNICALL
Java_com_torirs_client_ClientActivity_nativeSetDensity(
    JNIEnv* env, jobject thiz, jint density)
{
    (void)env;
    (void)thiz;
    PlatformAndroid_SetDensity((int)density);
}

JNIEXPORT void JNICALL
Java_com_torirs_client_ClientActivity_nativeKeyboardInset(
    JNIEnv* env, jobject thiz, jint bottom_px)
{
    (void)env;
    (void)thiz;
    PlatformAndroid_SetKeyboardInset((int)bottom_px);
}

JNIEXPORT void JNICALL
Java_com_torirs_client_ClientActivity_nativeTouch(
    JNIEnv* env, jobject thiz, jint action, jint pointer_id, jint x, jint y)
{
    (void)env;
    (void)thiz;
    PlatformAndroid_PostTouch((int)action, (int32_t)pointer_id, (int)x, (int)y);
}

JNIEXPORT void JNICALL
Java_com_torirs_client_ClientActivity_nativeKey(
    JNIEnv* env, jobject thiz, jint keycode, jint down, jint unicode)
{
    (void)env;
    (void)thiz;
    PlatformAndroid_PostKey((int)keycode, (int)down, (int)unicode);
}

JNIEXPORT void JNICALL
Java_com_torirs_client_ClientActivity_nativeStop(JNIEnv* env, jobject thiz)
{
    (void)env;
    (void)thiz;

    /*
     * Ask, then WAIT. The frame loop finishes its frame, App_Shutdown runs, and
     * the thread exits; joining is what makes that ordering real rather than a
     * hope. The alternative -- letting the process die with the preference file
     * or the incremental cache mid-write -- is how a save gets corrupted.
     */
    PlatformAndroid_RequestQuit();
    if( g_thread_started )
    {
        pthread_join(g_thread, NULL);
        g_thread_started = 0;
    }
    argv_clear();
    LOGI("frame thread joined");
}
