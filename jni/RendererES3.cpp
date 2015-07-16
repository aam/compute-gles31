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
#include <stdio.h>

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

void printOpenGLStats() {
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
}

void assertNoGLErrors(const char *step) {
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        ALOGE("Failed to %s: %d", step, err);
    } else {
        ALOGV("Completed %s", step);
    }
}

void tryComputeShader() {
    int i;

   // Initialize our compute program
    GLuint compute_prog = glCreateProgram();
    assertNoGLErrors("create program");

    const int workgroupSize = 1024; // max supported by Nexus 6

    static const char compute_shader_header[] =
R"(#version 310 es
#define LOCAL_SIZE )";
    static const char compute_shader_body[] =
R"(#extension GL_ANDROID_extension_pack_es31a : require

layout(local_size_x = LOCAL_SIZE) in;
layout(binding=0, rgba32f) uniform mediump readonly imageBuffer velocity_buffer;
layout(binding=1, rgba32f) uniform mediump writeonly imageBuffer position_buffer;

void main()
{
    vec4 vel = imageLoad(velocity_buffer, int(gl_GlobalInvocationID.x));
    vel += vec4(0.0f, 0.0f, 25.0f, 12.5f);
    vec4 result = vec4(gl_LocalInvocationID.x, gl_WorkGroupID.x, vel.z, vel.w);
    imageStore(position_buffer, int(gl_GlobalInvocationID.x), result);
}
)";

    const int compute_shader_source_max_len = strlen(compute_shader_header) + 32 + strlen(compute_shader_body);
    char compute_shader_source[compute_shader_source_max_len];
    snprintf(compute_shader_source, compute_shader_source_max_len,
        R"(%s%d
           %s)", compute_shader_header, workgroupSize, compute_shader_body);


    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    assertNoGLErrors("create shader");
    const GLchar* sources = { compute_shader_source };
    glShaderSource(shader, 1, &sources, NULL);
    assertNoGLErrors("shader source");
    glCompileShader(shader);
    assertNoGLErrors("compile shader");

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
    assertNoGLErrors("attach shader");
    glLinkProgram(compute_prog);
    assertNoGLErrors("link shader");

    ALOGV("Program linked");
    const int POINTS = 63*1024;  // N6: max number of points that can be actually retrieved using MapBufferRange
    const size_t sizeInBytes = POINTS * 4 * sizeof(float); // N6: only 1MB out of 134,217,728 max texture size works with MapBufferRange
    GLuint buffers[2];
    glGenBuffers(2, buffers);
    GLuint position_buffer = buffers[0];
    GLuint velocity_buffer = buffers[1];

    printOpenGLStats();

    glBindBuffer(GL_TEXTURE_BUFFER_EXT, velocity_buffer);
    assertNoGLErrors("bind velocity buffer");
    glBufferData(GL_TEXTURE_BUFFER_EXT, sizeInBytes, NULL, GL_DYNAMIC_COPY);
    assertNoGLErrors("buffer velocity data");

    long unsigned int luiSizeInBytes = (long unsigned int)sizeInBytes;
    ALOGV("Going to glMapBufferRange for %lu bytes", luiSizeInBytes);
    float *velocities = (float*)glMapBufferRange(GL_TEXTURE_BUFFER_EXT,
                                                0,
                                                sizeInBytes,
                                                GL_MAP_WRITE_BIT);
    assertNoGLErrors("map velocity buffer range");
    for (i = 0; i < POINTS; i++) {
        velocities[i * 4 + 0] = -i * 4;
        velocities[i * 4 + 1] = i * 4 + 1;
        velocities[i * 4 + 2] = -i * 4 + 2;
        velocities[i * 4 + 3] = i * 4 + 3;
    }
    glUnmapBuffer(GL_TEXTURE_BUFFER_EXT);
    assertNoGLErrors("unmap velocity buffer");

    glBindBuffer(GL_TEXTURE_BUFFER_EXT, position_buffer);
    assertNoGLErrors("bind position buffer");
    glBufferData(GL_TEXTURE_BUFFER_EXT, sizeInBytes, NULL, GL_DYNAMIC_COPY);
    assertNoGLErrors("buffer position data");

    GLuint tbos[2];
    glGenTextures(2, tbos);
    assertNoGLErrors("gen textures");

    int velocity_tbo = tbos[0];
    glBindTexture(GL_TEXTURE_BUFFER_EXT, velocity_tbo);
    assertNoGLErrors("bind velocity texture");
    glTexBufferEXT(GL_TEXTURE_BUFFER_EXT, GL_RGBA32F, velocity_buffer);
    assertNoGLErrors("tex velocity buffer");

    int position_tbo = tbos[1];
    glBindTexture(GL_TEXTURE_BUFFER_EXT, position_tbo);
    assertNoGLErrors("bind position texture");
    glTexBufferEXT(GL_TEXTURE_BUFFER_EXT, GL_RGBA32F, position_buffer);
    assertNoGLErrors("tex position buffer");

    // === End of initialization and setup ===

    // === Run the compute shader and retrieve the results ===

    glUseProgram(compute_prog);
    assertNoGLErrors("use program");

    glBindImageTexture(0, velocity_tbo, 0, false, 0, GL_READ_ONLY, GL_RGBA32F);
    assertNoGLErrors("bind image texture for velocity tbo");
    glBindImageTexture(1, position_tbo, 0, false, 0, GL_WRITE_ONLY, GL_RGBA32F);
    assertNoGLErrors("bind image texture for position tbo");

    glDispatchCompute(POINTS / workgroupSize, 1, 1);
    assertNoGLErrors("dispatch compute");

    glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT);
    assertNoGLErrors("memory barrier");
    glFinish();
    assertNoGLErrors("finish");
    ALOGV("Program completed");

    float *positions = (float*)glMapBufferRange(GL_TEXTURE_BUFFER_EXT,
                                                0,
                                                sizeInBytes,
                                                GL_MAP_READ_BIT);
    assertNoGLErrors("map positions buffer");
    for (i = 0; i < POINTS / workgroupSize; i++) {
        // if ((i < 2 * workgroupSize) || (i > POINTS - 2 * workgroupSize)) {
            ALOGV("positions[%d]=(%f, %f, %f, %f)\n", i * workgroupSize,
                positions[i * workgroupSize * 4 + 0],
                positions[i * workgroupSize * 4 + 1],
                positions[i * workgroupSize * 4 + 2],
                positions[i * workgroupSize * 4 + 3]);
            ALOGV("positions[%d]=(%f, %f, %f, %f)\n", i * workgroupSize + 1,
                positions[(i * workgroupSize + 1) * 4 + 0],
                positions[(i * workgroupSize + 1) * 4 + 1],
                positions[(i * workgroupSize + 1) * 4 + 2],
                positions[(i * workgroupSize + 1) * 4 + 3]);
    }
    glUnmapBuffer(GL_TEXTURE_BUFFER_EXT);
    assertNoGLErrors("unmap positions buffer");

    ALOGV("All done with tryComputeShader");
    return;
}



bool RendererES3::init() {
    mProgram = createProgram(VERTEX_SHADER, FRAGMENT_SHADER);
    if (!mProgram)
        return false;

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
