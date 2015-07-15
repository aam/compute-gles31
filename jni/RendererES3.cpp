/*
 * Copyright 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gles3jni.h"
#include <EGL/egl.h>

#include <stdlib.h>

#define STR(s) #s
#define STRV(s) STR(s)

#define POS_ATTRIB 0
#define COLOR_ATTRIB 1
#define SCALEROT_ATTRIB 2
#define OFFSET_ATTRIB 3

static const char VERTEX_SHADER[] =
    "#version 300 es\n"
    "layout(location = " STRV(POS_ATTRIB) ") in vec2 pos;\n"
    "layout(location=" STRV(COLOR_ATTRIB) ") in vec4 color;\n"
    "layout(location=" STRV(SCALEROT_ATTRIB) ") in vec4 scaleRot;\n"
    "layout(location=" STRV(OFFSET_ATTRIB) ") in vec2 offset;\n"
    "out vec4 vColor;\n"
    "void main() {\n"
    "    mat2 sr = mat2(scaleRot.xy, scaleRot.zw);\n"
    "    gl_Position = vec4(sr*pos + offset, 0.0, 1.0);\n"
    "    vColor = color;\n"
    "}\n";

static const char FRAGMENT_SHADER[] =
    "#version 300 es\n"
    "precision mediump float;\n"
    "in vec4 vColor;\n"
    "out vec4 outColor;\n"
    "void main() {\n"
    "    outColor = vColor;\n"
    "}\n";

class RendererES3: public Renderer {
public:
    RendererES3();
    virtual ~RendererES3();
    bool init();

private:
    enum {VB_INSTANCE, VB_SCALEROT, VB_OFFSET, VB_COUNT};

    virtual float* mapOffsetBuf();
    virtual void unmapOffsetBuf();
    virtual float* mapTransformBuf();
    virtual void unmapTransformBuf();
    virtual void draw(unsigned int numInstances);

    const EGLContext mEglContext;
    GLuint mProgram;
    GLuint mVB[VB_COUNT];
    GLuint mVBState;
};

Renderer* createES3Renderer() {
    RendererES3* renderer = new RendererES3;
    if (!renderer->init()) {
        delete renderer;
        return NULL;
    }
    return renderer;
}

RendererES3::RendererES3()
:   mEglContext(eglGetCurrentContext()),
    mProgram(0),
    mVBState(0)
{
    for (int i = 0; i < VB_COUNT; i++)
        mVB[i] = 0;
}

void tryComputeShader() {
    int i;

   // Initialize our compute program
    GLuint compute_prog = glCreateProgram();
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) { ALOGE("Failed to create program: %d\n", err); }

    static const char compute_shader_source[] =
"#version 310 es\n"
"#define LOCAL_SIZE 128\n"
"\n"
"#extension GL_ANDROID_extension_pack_es31a : require\n"
"\n"
"layout(local_size_x = LOCAL_SIZE, local_size_y = LOCAL_SIZE) in;\n"
"layout(binding=0, rgba32f) uniform mediump readonly imageBuffer velocity_buffer;\n"
"layout(binding=1, rgba32f) uniform mediump writeonly imageBuffer position_buffer;\n"
"\n"
"void main()\n"
"{\n"
"    vec4 vel = imageLoad(velocity_buffer, int(gl_GlobalInvocationID.x));\n"
"    vel += vec4(100.0f, 50.0f, 25.0f, 12.5f);\n"
"    imageStore(position_buffer, int(gl_GlobalInvocationID.x), vel);\n"
"}\n";

    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    err = glGetError();
    if (err != GL_NO_ERROR) { ALOGE("Failed to create shader: %d\n", err); }
    const GLchar* sources = { compute_shader_source };
    const GLint lengths = { strlen(compute_shader_source) };
    glShaderSource(shader, 1, &sources, &lengths);
    err = glGetError();
    if (err != GL_NO_ERROR) { ALOGE("Failed to shader source: %d\n", err); }
    glCompileShader(shader);
    err = glGetError();
    if (err != GL_NO_ERROR) { ALOGE("Failed to compile shader: %d\n", err); }

    GLint shader_ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &shader_ok);
    if (shader_ok != GL_TRUE) {
        ALOGE("Could not compile shader:\n");
        GLint log_len;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
        char* log = (char*)malloc(log_len * sizeof(char));
        glGetShaderInfoLog(shader, log_len, NULL, log);
        ALOGE("%s\n", log);
        glDeleteShader(shader);
        return;
    }

    glAttachShader(compute_prog, shader);
    err = glGetError();
    if (err != GL_NO_ERROR) { ALOGE("Failed to attach shader: %d\n", err); }
    glLinkProgram(compute_prog);
    err = glGetError();
    if (err != GL_NO_ERROR) { ALOGE("Failed to link program: %d\n", err); }

    ALOGV("Program linked");
    const int POINTS = 512*1024-1;
    const size_t sizeInBytes = POINTS * 4 * sizeof(float);
    GLuint buffers[2];
    glGenBuffers(2, buffers);
    GLuint position_buffer = buffers[0];
    GLuint velocity_buffer = buffers[1];

    GLint64 max_texture_size;
    glGetInteger64v(GL_MAX_TEXTURE_BUFFER_SIZE_EXT, &max_texture_size);
    long long int ll_max_texture_size = (long long int)max_texture_size;
    ALOGV("Retrieved max texture size: %lld", ll_max_texture_size);

    GLint64 max_compute_shared_memory;
    //#define MAX_COMPUTE_SHARED_MEMORY_SIZE 0x8262
    glGetInteger64v(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, &max_compute_shared_memory);
    long long int ll_max_compute_shared_memory = (long long int)max_compute_shared_memory;
    ALOGV("Retrieved max compute shared memory size: %lld", ll_max_compute_shared_memory);


    GLint64 max_shader_storage_block_size;
    //#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE  0x90DE
    glGetInteger64v(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, &max_shader_storage_block_size);
    long long int ll_max_shader_storage_block_size = (long long int)max_shader_storage_block_size;
    ALOGV("Retrieved max_shader_storage_block_size: %lld", ll_max_shader_storage_block_size);

    GLint64 max_compute_shader_storage_blocks;
    //#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE  0x90DE
    glGetInteger64v(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, &max_compute_shader_storage_blocks);
    long long int ll_max_compute_shader_storage_blocks = (long long int)max_compute_shader_storage_blocks;
    ALOGV("Retrieved max_compute_shader_storage_blocks: %lld", ll_max_compute_shader_storage_blocks);

    glBindBuffer(GL_TEXTURE_BUFFER_EXT, velocity_buffer);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to bind velocity buffer: %d\n", err); }
    ALOGV("Bound velocity buffer");
    glBufferData(GL_TEXTURE_BUFFER_EXT, sizeInBytes, NULL, GL_DYNAMIC_COPY);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to buffer velocity data: %x\n", err); }

    long unsigned int luiSizeInBytes = (long unsigned int)sizeInBytes;
    ALOGV("Going to glMapBufferRange for %lu bytes", luiSizeInBytes);
    float *velocities = (float*)glMapBufferRange(GL_TEXTURE_BUFFER_EXT,
                                                0,
                                                sizeInBytes,
                                                GL_MAP_WRITE_BIT);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to map velocities buffer range: %x\n", err); }
    for (i = 0; i < POINTS; i++) {
        velocities[i * 4 + 0] = -i * 4;
        velocities[i * 4 + 1] = i * 4 + 1;
        velocities[i * 4 + 2] = -i * 4 + 2;
        velocities[i * 4 + 3] = i * 4 + 3;
    }
    glUnmapBuffer(GL_TEXTURE_BUFFER_EXT);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to unmap velocities buffer: %x\n", err); }
    ALOGV("Unmapped velocities buffer");


    glBindBuffer(GL_TEXTURE_BUFFER_EXT, position_buffer);
    ALOGV("Bound position buffer");
    glBufferData(GL_TEXTURE_BUFFER_EXT, sizeInBytes, NULL, GL_DYNAMIC_COPY);

    GLuint tbos[2];
    glGenTextures(2, tbos);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to gen textures: %d\n", err); }

    int velocity_tbo = tbos[0];
    glBindTexture(GL_TEXTURE_BUFFER_EXT, velocity_tbo);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to bind velocity texture %d to binding point %x: %x\n", velocity_tbo, GL_TEXTURE_BUFFER_EXT, err); }
    glTexBufferEXT(GL_TEXTURE_BUFFER_EXT, GL_RGBA32F, velocity_buffer);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to tex velocity buffer: %x\n", err); }

    int position_tbo = tbos[1];
    glBindTexture(GL_TEXTURE_BUFFER_EXT, position_tbo);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to bind position texture %d to binding point %x: %x\n", position_tbo, GL_TEXTURE_BUFFER_EXT, err); }
    glTexBufferEXT(GL_TEXTURE_BUFFER_EXT, GL_RGBA32F, position_buffer);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to tex velocity buffer: %x\n", err); }


    // === End of initialization and setup ===

    // === Run the compute shader and retrieve the results ===

    glUseProgram(compute_prog);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to use program: %x\n", err); }

    glBindImageTexture(0, velocity_tbo, 0, false, 0, GL_READ_ONLY, GL_RGBA32F);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to bind velocity image texture: %x\n", err); }
    glBindImageTexture(1, position_tbo, 0, false, 0, GL_WRITE_ONLY, GL_RGBA32F);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to bind position image texture: %x\n", err); }

    const int workgroupSize = 8;
    glDispatchCompute(POINTS / workgroupSize, 1, 1);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to dispatch compute: %x\n", err); }

    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to memory barrier: %x\n", err); }
    ALOGV("Program completed");

    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to buffer position data: %x\n", err); }
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to bind positions buffer: %d\n", err); }
    ALOGV("Going to glMapBufferRange");
    float *positions = (float*)glMapBufferRange(GL_TEXTURE_BUFFER_EXT,
                                                0,
                                                sizeInBytes,
                                                GL_MAP_READ_BIT);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to map buffer range: %x\n", err); }
    for (i = 0; i < POINTS; i++) {
        ALOGV("positions[%d]=(%f, %f, %f, %f)\n", i,
            positions[i * 4 + 0],
            positions[i * 4 + 1],
            positions[i * 4 + 2],
            positions[i * 4 + 3]);
        if (i > 10) {
            break;
        }
    }
    glUnmapBuffer(GL_TEXTURE_BUFFER_EXT);
    err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to unmap buffer: %x\n", err); }
    ALOGV("Unmapped positions buffer");
    ALOGV("All done with tryComputeShader");

    return;
}



bool RendererES3::init() {
    mProgram = createProgram(VERTEX_SHADER, FRAGMENT_SHADER);
    if (!mProgram)
        return false;

    // GLuint buffers[2];
    // glGenBuffers(2, buffers);
    // GLuint position_buffer = buffers[0];
    // GLuint velocity_buffer = buffers[1];

    // glBindBuffer(GL_TEXTURE_BUFFER_EXT, velocity_buffer);
    // GLenum err = glGetError(); if (err != GL_NO_ERROR) { ALOGE("Failed to bind velocity buffer: %d\n", err); }
    // ALOGV("Bound velocity buffer");

    tryComputeShader();

    glGenBuffers(VB_COUNT, mVB);
    glBindBuffer(GL_ARRAY_BUFFER, mVB[VB_INSTANCE]);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), &QUAD[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, mVB[VB_SCALEROT]);
    glBufferData(GL_ARRAY_BUFFER, MAX_INSTANCES * 4*sizeof(float), NULL, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, mVB[VB_OFFSET]);
    glBufferData(GL_ARRAY_BUFFER, MAX_INSTANCES * 2*sizeof(float), NULL, GL_STATIC_DRAW);

    glGenVertexArrays(1, &mVBState);
    glBindVertexArray(mVBState);

    glBindBuffer(GL_ARRAY_BUFFER, mVB[VB_INSTANCE]);
    glVertexAttribPointer(POS_ATTRIB, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const GLvoid*)offsetof(Vertex, pos));
    glVertexAttribPointer(COLOR_ATTRIB, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (const GLvoid*)offsetof(Vertex, rgba));
    glEnableVertexAttribArray(POS_ATTRIB);
    glEnableVertexAttribArray(COLOR_ATTRIB);

    glBindBuffer(GL_ARRAY_BUFFER, mVB[VB_SCALEROT]);
    glVertexAttribPointer(SCALEROT_ATTRIB, 4, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);
    glEnableVertexAttribArray(SCALEROT_ATTRIB);
    glVertexAttribDivisor(SCALEROT_ATTRIB, 1);

    glBindBuffer(GL_ARRAY_BUFFER, mVB[VB_OFFSET]);
    glVertexAttribPointer(OFFSET_ATTRIB, 2, GL_FLOAT, GL_FALSE, 2*sizeof(float), 0);
    glEnableVertexAttribArray(OFFSET_ATTRIB);
    glVertexAttribDivisor(OFFSET_ATTRIB, 1);

    ALOGV("Using OpenGL ES 3.0 renderer");
    return true;
}

RendererES3::~RendererES3() {
    /* The destructor may be called after the context has already been
     * destroyed, in which case our objects have already been destroyed.
     *
     * If the context exists, it must be current. This only happens when we're
     * cleaning up after a failed init().
     */
    if (eglGetCurrentContext() != mEglContext)
        return;
    glDeleteVertexArrays(1, &mVBState);
    glDeleteBuffers(VB_COUNT, mVB);
    glDeleteProgram(mProgram);
}

float* RendererES3::mapOffsetBuf() {
    glBindBuffer(GL_ARRAY_BUFFER, mVB[VB_OFFSET]);
    return (float*)glMapBufferRange(GL_ARRAY_BUFFER,
            0, MAX_INSTANCES * 2*sizeof(float),
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
}

void RendererES3::unmapOffsetBuf() {
    glUnmapBuffer(GL_ARRAY_BUFFER);
}

float* RendererES3::mapTransformBuf() {
    glBindBuffer(GL_ARRAY_BUFFER, mVB[VB_SCALEROT]);
    return (float*)glMapBufferRange(GL_ARRAY_BUFFER,
            0, MAX_INSTANCES * 4*sizeof(float),
            GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
}

void RendererES3::unmapTransformBuf() {
    glUnmapBuffer(GL_ARRAY_BUFFER);
}

void RendererES3::draw(unsigned int numInstances) {
    glUseProgram(mProgram);
    glBindVertexArray(mVBState);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, numInstances);
}
