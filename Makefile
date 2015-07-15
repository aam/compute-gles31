all: deploy-debug

init:
	android update project -p . -t android-21

JNI_SOURCES := jni/gl3stub.c jni/gl3stub.h jni/gles3jni.cpp jni/gles3jni.h jni/RendererES3.cpp
JNI_LIBS := libs/arm64-v8a/libgles3jni.so \
	libs/armeabi/libgles3jni.so \
	libs/armeabi-v7a/libgles3jni.so \
	libs/mips/libgles3jni.so \
	libs/mips64/libgles3jni.so \
	libs/x86/libgles3jni.so \
	libs/x86_64/libgles3jni.so

JAVA_SOURCES := src/com/android/gles3jni/GLES3JNIActivity.java \
	src/com/android/gles3jni/GLES3JNILib.java \
	src/com/android/gles3jni/GLES3JNIView.java


$(JNI_LIBS): $(JNI_SOURCES)
	ndk-build

build: bin/GLES3JNIActivity-release-unsigned.apk bin/GLES3JNIActivity-debug.apk

bin/GLES3JNIActivity-%.apk: $(JNI_LIBS) $(JAVA_SOURCES)
	ant $*

deploy-release: bin/GLES3JNIActivity-release-unsigned.apk
	adb install -r bin/GLES3JNIActivity-release-unsigned.apk

deploy-debug: bin/GLES3JNIActivity-debug.apk
	adb install -r bin/GLES3JNIActivity-debug.apk

clean:
	rm -rf obj
	rm -rf libs
	rm -rf gen
	rm -rf bin

