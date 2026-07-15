# APP_ABI is supplied per-job by the build-bot (APP_ABI=arm64-v8a, armeabi-v7a, ...);
# "all" is only the fallback for a bare local `ndk-build`.
APP_STL      := c++_static
APP_ABI      := all
APP_PLATFORM := android-21
