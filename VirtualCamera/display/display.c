#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libgen.h>


#ifdef __ANDROID__
#include <GLES2/gl2.h>
#include <android/log.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG ,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR ,  TAG, __VA_ARGS__)
#define LOGFD(x, ...) LOGD("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)
#define LOGFE(x, ...) LOGE("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)

#undef TAG
#define TAG "display"

#endif

#ifdef __MACH__
#include <OpenGL/gl.h>
#define LOGD(...) printf(...)
#define LOGE(...) printf(...)
#define LOGFD(x, ...) printf("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)
#define LOGFE(x, ...) printf("[ %s | %s | %d ] " x, basename(__FILE__), __FUNCTION__,__LINE__, ##__VA_ARGS__)
#endif

#include "display.h"

#define  DEBUG 1
#if (DEBUG==1)
#define CHECK_GL_ERROR \
    { \
    GLenum error = glGetError(); \
    if (error != GL_NO_ERROR) { \
        LOGFD("glGetError() = %x", error); \
    } \
    }
#else
#define CHECK_GL_ERROR
#endif

#define VERTEX_POS_INDX       0
#define VERTEX_TEX_INDX       1

typedef struct stGLDisplay{
    // Handle to a program object：指向program对象
    GLuint programObject;
    // VertexBufferObject Ids:顶点缓存对象ID
    GLuint vertexBuffer;
    GLuint indicesBuffer;
    // Texture handle：纹理句柄
    GLuint inputTexture;
    // FrameBuffer：帧缓存
    GLuint frameBuffer;
    GLuint targetTextureRGB;
    // Sampler locations：采样位置
    GLint samplerLoc;
}GLDisplay;

const char *vShaderStrDisplay =
"attribute vec4 in_position;    \n"
"attribute vec2 in_texcoord;    \n"
"varying vec2 v_texcoord;       \n"
"void main() {                  \n"
"   gl_Position = in_position;  \n"
"   v_texcoord = in_texcoord;   \n"
"}\n";

const char *fShaderStrDisplay =
"precision mediump float;                            \n"
"varying vec2 v_texcoord;                            \n"
"uniform sampler2D sampler;                          \n"
"void main()                                         \n"
"{                                                   \n"
"   gl_FragColor = texture2D( sampler, v_texcoord ); \n"
"}                                                   \n";

GLfloat vVertices[] = {
//    -1.0f,  1.0f, 0.0f,  // Position 0
//    0.0f,  1.0f,        // TexCoord 0
//
//    -1.0f, -1.0f, 0.0f,  // Position 1
//    0.0f,  0.0f,        // TexCoord 1
//
//    1.0f, -1.0f, 0.0f,  // Position 2
//    1.0f,  0.0f,   // TexCoord 2
//
//    1.0f,  1.0f, 0.0f,  // Position 3
//    1.0f,  1.0f,   // TexCoord 3

    // 解决纹理这样贴反的问题
    -1.0f,  1.0f, 0.0f,  // Position 0
    0.0f,  0.0f,        // TexCoord 0

    -1.0f, -1.0f, 0.0f,  // Position 1
    0.0f,  1.0f,        // TexCoord 1

    1.0f, -1.0f, 0.0f,  // Position 2
    1.0f,  1.0f,   // TexCoord 2


    1.0f,  1.0f, 0.0f,  // Position 3
    1.0f,  0.0f,   // TexCoord 3

//		0.0f,  1.0f, 0.0f,  // Position 4
//		0.0f,  1.0f,        // TexCoord 4
//		0.0f, -1.0f, 0.0f,  // Position5
//		0.0f,  0.0f,        // TexCoord5
//		1.0f, -1.0f, 0.0f,  // Position 6
//		1.0f,  0.0f,        // TexCoord 6
//		1.0f,  1.0f, 0.0f,  // Position 7
//		1.0f,  1.0f         // TexCoord 7
};
GLushort indices[] = {0, 1, 2, 0, 2, 3};

GLDisplay* display_init() {
    GLDisplay *glDisplay = (GLDisplay *)malloc(sizeof(GLDisplay));
	memset(glDisplay, 0, sizeof(GLDisplay));

	GLuint vshader = glCreateShader ( GL_VERTEX_SHADER );
    // Load the shader source
    glShaderSource ( vshader, 1, &vShaderStrDisplay, NULL );
    // Compile the shader
    glCompileShader ( vshader );
    { // Check the compile status
        GLint compileResult = GL_TRUE;
        glGetShaderiv(vshader, GL_COMPILE_STATUS, &compileResult);
        if (!compileResult) {
            LOGFE("vshader compile error...\n");
            char szLog[1024] = {0};
            GLsizei logLen = 0;
            glGetShaderInfoLog(vshader, 1024, &logLen, szLog);
            printf("Compile Shader fail error log: %s \n", szLog);
            glDeleteShader(vshader);
            vshader = 0;
        }
    }
    GLuint fshader = glCreateShader ( GL_FRAGMENT_SHADER );
    // Load the shader source
    glShaderSource ( fshader, 1, &fShaderStrDisplay, NULL );
    // Compile the shader
    glCompileShader ( fshader );
    { // Check the compile status
        GLint compileResult = GL_TRUE;
        glGetShaderiv(fshader, GL_COMPILE_STATUS, &compileResult);
        if (!compileResult) {
            LOGFE("fshader compile error...\n");
            char szLog[1024] = {0};
            GLsizei logLen = 0;
            glGetShaderInfoLog(fshader, 1024, &logLen, szLog);
            printf("Compile Shader fail error log: %s \n", szLog);
            glDeleteShader(fshader);
            vshader = 0;
        }
    }


    glDisplay->programObject = glCreateProgram( );

    glAttachShader(glDisplay->programObject, vshader);
    glAttachShader(glDisplay->programObject, fshader);

    glBindAttribLocation(glDisplay->programObject, VERTEX_POS_INDX, "in_position");
    glBindAttribLocation(glDisplay->programObject, VERTEX_TEX_INDX, "in_texcoord");
    // Link the program
    glLinkProgram (glDisplay->programObject);

    { // Check the compile status
        GLint linked = GL_TRUE;
        glGetProgramiv (glDisplay->programObject, GL_LINK_STATUS, &linked);//检测链接是否成功
        if (!linked) {
            LOGFE("shader link error...\n");
            char szLog[1024] = {0};
            GLsizei logLen = 0;
            glGetProgramInfoLog(glDisplay->programObject, 1024, &logLen, szLog);
            printf("Link program fail error log: %s\n", szLog);
            glDeleteShader(glDisplay->programObject);
            glDisplay->programObject = 0;
            return NULL;
        }
    }
    // Get the sampler location 获取采样位置
    glDisplay->samplerLoc = glGetUniformLocation (glDisplay->programObject, "sampler" );
    CHECK_GL_ERROR
    
    glGenTextures(1, &glDisplay->inputTexture);
//    glBindTexture(GL_TEXTURE_2D, glDisplay->targetTextureRGB);					//绑定纹理
//	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);		//把纹理像素映射到帧缓存中
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
//	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
//	glBindTexture(GL_TEXTURE_2D, 0);

    return glDisplay;
}

void display_draw(GLDisplay *glDisplay, GLFrameData *glFrameData, int offsetX, int offsetY, int displayWidth, int displayHeight) {
    if(glDisplay == NULL || glFrameData == NULL || glFrameData->data == NULL) {
        LOGFE("null pointer error");
        return;
    }

    // set input texture:
    glBindTexture(GL_TEXTURE_2D, glDisplay->inputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, glFrameData->width, glFrameData->height, 0, GL_RGB, GL_UNSIGNED_BYTE, glFrameData->data);//根据指定的参数，生成2D纹理
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);//把纹理像素映射成像素
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);

    glViewport(offsetX, offsetY, displayWidth, displayHeight);

    // Use the program object
    glUseProgram(glDisplay->programObject);

    // Bind the base map
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, glDisplay->inputTexture);
    // Set the base map sampler to texture unit to 0
    glUniform1i(glDisplay->samplerLoc, 0);
    // Load the vertex position
    glVertexAttribPointer (VERTEX_POS_INDX, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), vVertices);
    // Load the texture coordinate
    glVertexAttribPointer (VERTEX_TEX_INDX, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &vVertices[3]);
    glEnableVertexAttribArray ( 0 );
    glEnableVertexAttribArray ( 1 );
    CHECK_GL_ERROR
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    CHECK_GL_ERROR
}

void display_shutdown(GLDisplay *glDisplay) {
    if (glDisplay == NULL) {
        return;
    }

    if (glDisplay->programObject) {
        if (glDisplay->inputTexture != 0) {
            glDeleteTextures(1, &glDisplay->inputTexture );
            glDisplay->inputTexture = 0;
        }
        if (glDisplay->programObject != 0) {
            glDeleteProgram( glDisplay->programObject );
            glDisplay->programObject = 0;
        }
    }
    free(glDisplay);
}

