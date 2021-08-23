#ifndef __DISPLAY_H__
#define __DISPLAY_H__

//#ifdef TARGET_OS_MAC
//#include <gl.h>
//#endif

//#ifdef TARGET_OS_IOS
//#include <OpenGLES/ES2/gl.h>
//#endif

//#ifdef __ANDROID__
//#include <GLES2/gl2.h>
//#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stGLDisplay GLDisplay;
typedef struct GLFrameData {
    unsigned char *data;
    int width;
    int height;
}GLFrameData;
    
GLDisplay* display_init();
void display_draw(GLDisplay *glDisplay, GLFrameData *glFrameData, int offsetX, int offsetY, int displayWidth, int displayHeight);
void display_shutdown(GLDisplay *glDisplay);

#ifdef __cplusplus
}
#endif

#endif
