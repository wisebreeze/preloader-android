#include "pl/runtime/JavaRuntime.h"

#include "pl/Logger.hpp"

#include <cstring>

namespace pl::runtime {
namespace {

JavaVM *g_vm = nullptr;
jobject g_activity = nullptr;

JNIEnv *AttachCurrentThread(bool &attached) {
    JNIEnv *env = nullptr;
    attached = false;
    const jint status =
        g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4);
    if (status == JNI_EDETACHED) {
        if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            return nullptr;
        }
        attached = true;
    } else if (status != JNI_OK) {
        return nullptr;
    }
    return env;
}

} // namespace

void SetJavaVm(JavaVM *vm) { g_vm = vm; }

JavaVM *GetJavaVm() { return g_vm; }

void SetActivity(JNIEnv *env, jobject activity) {
  ClearActivity(env);
  if (activity) {
    g_activity = env->NewGlobalRef(activity);
  }
}

void ClearActivity(JNIEnv *env) {
  if (g_activity) {
    env->DeleteGlobalRef(g_activity);
    g_activity = nullptr;
  }
}

void CallActivityVoidMethod(const char *methodName) {
  if (!g_vm || !g_activity) {
    return;
  }

  JNIEnv *env = nullptr;
  bool attached = false;
  const jint status =
      g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4);
  if (status == JNI_EDETACHED) {
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
      return;
    }
    attached = true;
  } else if (status != JNI_OK) {
    return;
  }

  jclass cls = env->GetObjectClass(g_activity);
  if (cls) {
    jmethodID mid = env->GetMethodID(cls, methodName, "()V");
    if (mid) {
      env->CallVoidMethod(g_activity, mid);
    }
    env->DeleteLocalRef(cls);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
  }

  if (attached) {
    g_vm->DetachCurrentThread();
  }
}

bool CallActivityStringMethod(const char *methodName, const std::string &value) {
  if (!g_vm || !g_activity) {
    return false;
  }

  JNIEnv *env = nullptr;
  bool attached = false;
  const jint status =
      g_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_4);
  if (status == JNI_EDETACHED) {
    if (g_vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
      return false;
    }
    attached = true;
  } else if (status != JNI_OK) {
    return false;
  }

  bool called = false;
  jclass cls = env->GetObjectClass(g_activity);
  if (cls) {
    jmethodID mid = env->GetMethodID(cls, methodName, "(Ljava/lang/String;)V");
    if (mid) {
      jstring argument = env->NewStringUTF(value.c_str());
      if (argument) {
        env->CallVoidMethod(g_activity, mid, argument);
        env->DeleteLocalRef(argument);
        called = !env->ExceptionCheck();
      }
    }
    env->DeleteLocalRef(cls);
  }
  if (env->ExceptionCheck()) {
    env->ExceptionClear();
    called = false;
  }

  if (attached) {
    g_vm->DetachCurrentThread();
  }
  return called;
}

pl::platform::HttpResponse HttpGetImpl(std::string_view url, int timeoutMs) {
  pl::platform::HttpResponse result;
  if (!g_vm || url.empty()) {
    return result;
  }

  bool attached = false;
  JNIEnv *env = AttachCurrentThread(attached);
  if (!env) {
    return result;
  }

  if (env->PushLocalFrame(32) < 0) {
    env->ExceptionClear();
    if (attached) g_vm->DetachCurrentThread();
    return result;
  }

  jobject connection = nullptr;
  jobject input = nullptr;

  jclass urlClass = env->FindClass("java/net/URL");
  jclass connectionClass = env->FindClass("java/net/HttpURLConnection");
  jclass inputClass = env->FindClass("java/io/InputStream");
  jclass outputClass = env->FindClass("java/io/ByteArrayOutputStream");
  std::string urlString(url);
  jstring jUrl = env->NewStringUTF(urlString.c_str());

  if (urlClass && connectionClass && inputClass && outputClass && jUrl) {
    jmethodID urlCtor = env->GetMethodID(urlClass, "<init>", "(Ljava/lang/String;)V");
    jobject urlObject = urlCtor ? env->NewObject(urlClass, urlCtor, jUrl) : nullptr;

    if (urlObject) {
      jmethodID openConnection = env->GetMethodID(urlClass, "openConnection", "()Ljava/net/URLConnection;");
      connection = openConnection ? env->CallObjectMethod(urlObject, openConnection) : nullptr;
      env->DeleteLocalRef(urlObject);
    }

    if (connection && !env->ExceptionCheck()) {
      jmethodID setConnectTimeout = env->GetMethodID(connectionClass, "setConnectTimeout", "(I)V");
      jmethodID setReadTimeout = env->GetMethodID(connectionClass, "setReadTimeout", "(I)V");
      jmethodID setInstanceFollowRedirects = env->GetMethodID(connectionClass, "setInstanceFollowRedirects", "(Z)V");
      jmethodID setRequestProperty = env->GetMethodID(connectionClass, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");

      if (setConnectTimeout) env->CallVoidMethod(connection, setConnectTimeout, static_cast<jint>(timeoutMs));
      if (setReadTimeout) env->CallVoidMethod(connection, setReadTimeout, static_cast<jint>(timeoutMs));
      if (setInstanceFollowRedirects) env->CallVoidMethod(connection, setInstanceFollowRedirects, JNI_TRUE);

      if (setRequestProperty) {
        jstring agentKey = env->NewStringUTF("User-Agent");
        jstring agentValue = env->NewStringUTF("BreezeLauncher/1.0");
        if (agentKey && agentValue) {
          env->CallVoidMethod(connection, setRequestProperty, agentKey, agentValue);
        }
        if (agentKey) env->DeleteLocalRef(agentKey);
        if (agentValue) env->DeleteLocalRef(agentValue);
      }

      jmethodID getResponseCode = env->GetMethodID(connectionClass, "getResponseCode", "()I");
      if (getResponseCode) result.status = env->CallIntMethod(connection, getResponseCode);
      if (!env->ExceptionCheck()) {
        jmethodID getHeaderField = env->GetMethodID(connectionClass, "getHeaderField", "(Ljava/lang/String;)Ljava/lang/String;");
        jstring retryKey = env->NewStringUTF("Retry-After");
        jstring retry = getHeaderField && retryKey ? static_cast<jstring>(env->CallObjectMethod(connection, getHeaderField, retryKey)) : nullptr;
        if (retry) {
          const char *chars = env->GetStringUTFChars(retry, nullptr);
          if (chars) {
            result.retryAfter = chars;
            env->ReleaseStringUTFChars(retry, chars);
          }
          env->DeleteLocalRef(retry);
        }
        if (retryKey) env->DeleteLocalRef(retryKey);

        jmethodID getStream = env->GetMethodID(connectionClass,
            result.status >= 400 ? "getErrorStream" : "getInputStream", "()Ljava/io/InputStream;");
        input = getStream ? env->CallObjectMethod(connection, getStream) : nullptr;
        if (input && !env->ExceptionCheck()) {
          jmethodID outputCtor = env->GetMethodID(outputClass, "<init>", "()V");
          jobject output = outputCtor ? env->NewObject(outputClass, outputCtor) : nullptr;
          jmethodID read = env->GetMethodID(inputClass, "read", "([B)I");
          jmethodID write = env->GetMethodID(outputClass, "write", "([BII)V");
          jmethodID toByteArray = env->GetMethodID(outputClass, "toByteArray", "()[B");
          jbyteArray buffer = env->NewByteArray(8192);
          if (output && read && write && toByteArray && buffer) {
            constexpr std::size_t maxResponseBytes = 8 * 1024 * 1024;
            std::size_t total = 0;
            bool tooLarge = false;
            for (;;) {
              jint count = env->CallIntMethod(input, read, buffer);
              if (env->ExceptionCheck() || count <= 0) break;
              total += static_cast<std::size_t>(count);
              if (total > maxResponseBytes) {
                tooLarge = true;
                break;
              }
              env->CallVoidMethod(output, write, buffer, 0, count);
              if (env->ExceptionCheck()) break;
            }
            if (!tooLarge) {
              jbyteArray responseBytes = static_cast<jbyteArray>(env->CallObjectMethod(output, toByteArray));
              if (!env->ExceptionCheck() && responseBytes) {
                jsize length = env->GetArrayLength(responseBytes);
                jbyte *bytes = env->GetByteArrayElements(responseBytes, nullptr);
                if (bytes && length > 0) {
                  result.body.assign(reinterpret_cast<const char *>(bytes), static_cast<std::size_t>(length));
                  env->ReleaseByteArrayElements(responseBytes, bytes, JNI_ABORT);
                }
                env->DeleteLocalRef(responseBytes);
              }
            } else {
              preloaderLogger.warn("HTTP response from {} exceeded max size", url);
            }
          }
          if (buffer) env->DeleteLocalRef(buffer);
          if (output) env->DeleteLocalRef(output);
        }
      }
    }

    if (env->ExceptionCheck()) env->ExceptionClear();
  }

  if (input) env->DeleteLocalRef(input);
  if (connection) env->DeleteLocalRef(connection);
  if (jUrl) env->DeleteLocalRef(jUrl);
  if (urlClass) env->DeleteLocalRef(urlClass);
  if (connectionClass) env->DeleteLocalRef(connectionClass);
  if (inputClass) env->DeleteLocalRef(inputClass);
  if (outputClass) env->DeleteLocalRef(outputClass);

  env->PopLocalFrame(nullptr);

  if (attached) g_vm->DetachCurrentThread();
  return result;
}

} // namespace pl::runtime

namespace pl::platform {

HttpResponse httpGet(std::string_view url, int timeoutMs) {
  return pl::runtime::HttpGetImpl(url, timeoutMs);
}

} // namespace pl::platform
