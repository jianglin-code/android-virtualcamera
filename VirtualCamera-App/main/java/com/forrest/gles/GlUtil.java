/*
 * Copyright 2014 Google Inc. All rights reserved.
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

package com.forrest.gles;

import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.opengl.GLES20;
import android.opengl.GLES30;
import android.opengl.GLUtils;
import android.opengl.Matrix;
import android.util.Log;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

/**
 * Some OpenGL utility functions.
 */
public class GlUtil {
    public static final String TAG = "glutil";
    private static final int GL_TEXTURE_EXTERNAL_OES = 0x8D65;
    /** Identity matrix for general use.  Don't modify or life will get weird. */
    public static final float[] IDENTITY_MATRIX;
    static {
        IDENTITY_MATRIX = new float[16];
        Matrix.setIdentityM(IDENTITY_MATRIX, 0);
    }

    private static final int SIZEOF_FLOAT = 4;

    private static int fbo_flag = 0;

    public static int mshadowId;
    public static int mframeBufferId;
    public static int mrenderDepthBufferId;

    private GlUtil() {}     // do not instantiate

    /**
     * Creates a new program from the supplied vertex and fragment shaders.
     *
     * @return A handle to the program, or 0 on failure.
     */
    public static int createProgram(String vertexSource, String fragmentSource) {
        int vertexShader = loadShader(GLES20.GL_VERTEX_SHADER, vertexSource);
        if (vertexShader == 0) {
            return 0;
        }
        int pixelShader = loadShader(GLES20.GL_FRAGMENT_SHADER, fragmentSource);
        if (pixelShader == 0) {
            return 0;
        }

        int program = GLES20.glCreateProgram();
        checkGlError("glCreateProgram");
        if (program == 0) {
            Log.e(TAG, "Could not create program");
        }
        GLES20.glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        GLES20.glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        GLES20.glLinkProgram(program);
        int[] linkStatus = new int[1];
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linkStatus, 0);
        if (linkStatus[0] != GLES20.GL_TRUE) {
            Log.e(TAG, "Could not link program: ");
            Log.e(TAG, GLES20.glGetProgramInfoLog(program));
            GLES20.glDeleteProgram(program);
            program = 0;
        }
        return program;
    }

    /**
     * Compiles the provided shader source.
     *
     * @return A handle to the shader, or 0 on failure.
     */
    public static int loadShader(int shaderType, String source) {
        int shader = GLES20.glCreateShader(shaderType);
        checkGlError("glCreateShader type=" + shaderType);
        GLES20.glShaderSource(shader, source);
        GLES20.glCompileShader(shader);
        int[] compiled = new int[1];
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, compiled, 0);
        if (compiled[0] == 0) {
            Log.e(TAG, "Could not compile shader " + shaderType + ":");
            Log.e(TAG, " " + GLES20.glGetShaderInfoLog(shader));
            GLES20.glDeleteShader(shader);
            shader = 0;
        }
        return shader;
    }

    /**
     * Checks to see if a GLES error has been raised.
     */
    public static void checkGlError(String op) {
        int error = GLES20.glGetError();
        if (error != GLES20.GL_NO_ERROR) {
            String msg = op + ": glError 0x" + Integer.toHexString(error);
            Log.e(TAG, msg);
            throw new RuntimeException(msg);
        }
    }

    /**
     * Checks to see if the location we obtained is valid.  GLES returns -1 if a label
     * could not be found, but does not set the GL error.
     * <p>
     * Throws a RuntimeException if the location is invalid.
     */
    public static void checkLocation(int location, String label) {
        if (location < 0) {
            throw new RuntimeException("Unable to locate '" + label + "' in program");
        }
    }

    /**
     * Creates a texture from raw data.
     *
     * @param data Image data, in a "direct" ByteBuffer.
     * @param width Texture width, in pixels (not bytes).
     * @param height Texture height, in pixels.
     * @param format Image data format (use constant appropriate for glTexImage2D(), e.g. GL_RGBA).
     * @return Handle to texture.
     */
    public static int createImageTexture(ByteBuffer data, int width, int height, int format) {
        int[] textureHandles = new int[1];
        int textureHandle;

        GLES20.glGenTextures(1, textureHandles, 0);
        textureHandle = textureHandles[0];
        GlUtil.checkGlError("glGenTextures");

        // Bind the texture handle to the 2D texture target.
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textureHandle);

        // Configure min/mag filtering, i.e. what scaling method do we use if what we're rendering
        // is smaller or larger than the source image.
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER,
                GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER,
                GLES20.GL_LINEAR);
        GlUtil.checkGlError("loadImageTexture");

        // Load the data from the buffer into the texture handle.
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, /*level*/ 0, format,
                width, height, /*border*/ 0, format, GLES20.GL_UNSIGNED_BYTE, data);
        GlUtil.checkGlError("loadImageTexture");

        return textureHandle;
    }

    /**
     * 把外部图片资源或文件名转化成bitmap
     * @param drawableId 图片资源的id
     * @param rs  Resources对象
     * @return
     */
    public static int initTextureFromResID(int drawableId, Resources rs) { // 根据drawableId传入初始化图像
        InputStream is = rs.openRawResource(drawableId);
        Bitmap bitmapTmp;
        try {
            bitmapTmp = BitmapFactory.decodeStream(is);
        } finally {
            try {
                is.close();
            } catch (IOException e) {
                e.printStackTrace();
                throw new RuntimeException("initTextureFromRedID error :" + e.toString());
            }
        }
        return initTextureFromBitmap(bitmapTmp);
    }

    /**
     * 将图片转换成rgba纹理
     * @param filename 本地文件路径
     */
    public static int initTextureFromName(String filename) {
        int textureid = 0;
        try	{
            Bitmap bitmapTmp = BitmapFactory.decodeFile(filename);
            textureid = initTextureFromBitmap(bitmapTmp);
        }
        catch(Exception e) 	{
            e.printStackTrace();
            throw new RuntimeException("initTextureFromName error :" + e.toString());
        }
        return textureid;
    }

    /**
     * bitmap 转换成textureID 入口
     * @param bitmap
     * @return
     */

    public static int initTextureFromBitmap(Bitmap bitmap){
        return initTextureFromBitmap(bitmap, GLES20.GL_NEAREST, GLES20.GL_NEAREST, GLES20.GL_CLAMP_TO_EDGE, GLES20.GL_CLAMP_TO_EDGE);
    }

    private static int initTextureFromBitmap(Bitmap bitmap, int minfilter, int magfilter, int wraps, int wrapt) {
        int textureId=initTexture( minfilter, magfilter, wraps, wrapt );
        GLUtils.texImage2D( GLES20.GL_TEXTURE_2D, 0, bitmap, 0 );
        bitmap.recycle(); 		  //
        checkGlError("initTextureFromBitmap texImage2D");
        return textureId;
    }

    private static int initTexture(int minfilter, int magfilter, int wraps, int wrapt){
        int[] textures = new int[1];
        GLES20.glGenTextures( 1, textures, 0 );
        checkGlError("glGenTextures error");
        //获得自身的纹理ID值
        int  textureId=textures[0];
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textureId);
        int errno = GLES20.glGetError();
        if (errno != 0) {
            textureId = 0;
        }
        //GL_NEAREST  GL_LINEAR
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, minfilter);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, magfilter);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, wraps);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, wrapt);
        checkGlError("initTexture generate texutre");
        return textureId;
    }
    /**销毁texI对应的内存区域*/
    public static void deleteTexture(int texId){
        int[] textures = new int[1];
        textures[0] = texId;
        GLES20.glDeleteTextures(1, textures, 0);
    }

    /**
     * Allocates a direct float buffer, and populates it with the float array data.
     */
    public static FloatBuffer createFloatBuffer(float[] coords) {
        // Allocate a direct ByteBuffer, using 4 bytes per float, and copy coords into it.
        ByteBuffer bb = ByteBuffer.allocateDirect(coords.length * SIZEOF_FLOAT);
        bb.order(ByteOrder.nativeOrder());
        FloatBuffer fb = bb.asFloatBuffer();
        fb.put(coords);
        fb.position(0);
        return fb;
    }

    /**
     * Writes GL version info to the log.
     */
    public static void logVersionInfo() {
        Log.i(TAG, "vendor  : " + GLES20.glGetString(GLES20.GL_VENDOR));
        Log.i(TAG, "renderer: " + GLES20.glGetString(GLES20.GL_RENDERER));
        Log.i(TAG, "version : " + GLES20.glGetString(GLES20.GL_VERSION));

        if (false) {
            int[] values = new int[1];
            GLES30.glGetIntegerv(GLES30.GL_MAJOR_VERSION, values, 0);
            int majorVersion = values[0];
            GLES30.glGetIntegerv(GLES30.GL_MINOR_VERSION, values, 0);
            int minorVersion = values[0];
            if (GLES30.glGetError() == GLES30.GL_NO_ERROR) {
                Log.i(TAG, "iversion: " + majorVersion + "." + minorVersion);
            }
        }
    }

    // 产生FBO帧临时 Buffer
    public static void generateMidFrameBuffer(int w, int h) {
        if(fbo_flag == 1) {
            return;
        }
        int[] tia=new int[1];
        GLES20.glGenFramebuffers(1, tia, 0);
        mframeBufferId = tia[0];
        GLES20.glGenTextures(1, tia, 0);
        mshadowId=tia[0];
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, mshadowId);
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, w, h, 0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER,GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,GLES20.GL_TEXTURE_MAG_FILTER,GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S,GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T,GLES20.GL_CLAMP_TO_EDGE);

        checkGlError("generateMidFrameBuffer generate texture");

        GLES20.glGenRenderbuffers(1, tia, 0);
        mrenderDepthBufferId=tia[0];
        GLES20.glBindRenderbuffer(GLES20.GL_RENDERBUFFER, mrenderDepthBufferId);
        GLES20.glRenderbufferStorage(GLES20.GL_RENDERBUFFER, GLES20.GL_DEPTH_COMPONENT16, w, h);

        checkGlError("generateMidFrameBuffer generate Renderbuffers depth");

        GLES20.glViewport(0, 0, w, h);
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, mframeBufferId);
        GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0, GLES20.GL_TEXTURE_2D, mshadowId, 0);

        checkGlError("generateMidFrameBuffer generate Renderbuffers color");

        GLES20.glFramebufferRenderbuffer(GLES20.GL_FRAMEBUFFER, GLES20.GL_DEPTH_ATTACHMENT, GLES20.GL_RENDERBUFFER, mrenderDepthBufferId);

        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0);

        checkGlError("generateMidFrameBuffer generate Renderbuffers");
        fbo_flag = 1;
    }


    public static int generateTextureId(int texw, int texh){
        int[] textures = new int[1];
        GLES20.glGenTextures( 1, textures, 0 );
        checkGlError("glGenTextures error");
        //获得自身的纹理ID值
        int  textureId=textures[0];
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textureId);
        GLES20.glTexImage2D( GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, texw, texh, 0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR); //GL_NEAREST  GL_LINEAR
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0);
        checkGlError("initTexture generate texutre");
        return textureId;
    }

    public static int generateTextureIdOES() {
        int[] textures = new int[1];
        GLES20.glGenTextures(1, textures, 0);
        int textureId = textures[0];
        GLES20.glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);
        GLES20.glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameterf(GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, 0);
        return textureId;
    }

    public static int generateFrameBufferId() {
        int[] tia=new int[1];
        GLES20.glGenFramebuffers(1, tia, 0);
        checkGlError("glGenFramebuffers error");
        int frameBufferId = tia[0];
        return frameBufferId;
    }

    public static void releaseTextureId(int textureId) {
        int[] tmp = new int[1];
        tmp[0] = textureId;
        GLES20.glDeleteTextures(1, tmp, 0);
    }

    public static void releaseFramebufferId(int fbID) {
        int[] tmp = new int[1];
        tmp[0] = fbID;
        GLES20.glDeleteFramebuffers(1, tmp, 0);
    }

    public static void release() {
        if(fbo_flag == 0) {
            return;
        }

        int[] tmp = new int[1];

        tmp[0] = mshadowId;
        GLES20.glDeleteTextures(1, tmp, 0);

        tmp[0] = mrenderDepthBufferId;
        GLES20.glDeleteRenderbuffers(1, tmp, 0);

        tmp[0] = mframeBufferId;
        GLES20.glDeleteFramebuffers(1, tmp, 0);

        fbo_flag = 0;
    }

    public static int genFrameBuffer(int w, int h) {
        int[] tia = new int[1];
        GLES20.glGenFramebuffers(1, tia, 0);
        int fbId = tia[0];

        GLES20.glGenTextures(1, tia, 0);
        int shadowId = tia[0];
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, shadowId);
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, w, h, 0, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER,GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D,GLES20.GL_TEXTURE_MAG_FILTER,GLES20.GL_LINEAR);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S,GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T,GLES20.GL_CLAMP_TO_EDGE);
        checkGlError("genFrameBuffer generate texture");

        GLES20.glGenRenderbuffers(1, tia, 0);
        int renderDepthBufferId = tia[0];
        GLES20.glBindRenderbuffer(GLES20.GL_RENDERBUFFER, renderDepthBufferId);
        GLES20.glRenderbufferStorage(GLES20.GL_RENDERBUFFER, GLES20.GL_DEPTH_COMPONENT16, w, h);
        checkGlError("genFrameBuffer generate Renderbuffers depth");

        GLES20.glViewport(0, 0, w, h);
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, fbId);
        GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0, GLES20.GL_TEXTURE_2D, shadowId, 0);
        checkGlError("generateMidFrameBuffer generate Renderbuffers color");

        GLES20.glFramebufferRenderbuffer(GLES20.GL_FRAMEBUFFER, GLES20.GL_DEPTH_ATTACHMENT, GLES20.GL_RENDERBUFFER, renderDepthBufferId);
        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0);
        checkGlError("generateMidFrameBuffer generate Renderbuffers");
        return fbId;
    }


    public static void saveFrame(File file) throws IOException {
        int width = 3040;
        int height = 1520;
        ByteBuffer buf = ByteBuffer.allocateDirect(width * height * 4);
        buf.order(ByteOrder.LITTLE_ENDIAN);
        GLES20.glReadPixels(0, 0, width, height, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buf);
        GlUtil.checkGlError("glReadPixels");
        buf.rewind();
        //直接保存为rgba数据
//        FileOutputStream os = new FileOutputStream("/sdcard/test.rgba");
//        os.write(buf.array());
//        os.close();
        BufferedOutputStream bos = null;
        try {
            bos = new BufferedOutputStream(new FileOutputStream(file));
            Bitmap bmp = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            bmp.copyPixelsFromBuffer(buf);
            Bitmap bmp2 = reverseBitmap(bmp,1);
            bmp2.compress(Bitmap.CompressFormat.JPEG, 90, bos);
            bmp2.recycle();
//            bmp.compress(Bitmap.CompressFormat.JPEG, 90, bos);
            bmp.recycle();
        } finally {
            if (bos != null) bos.close();
        }
    }

    public static void saveFrame(String filePath, int w, int h) {
        int width = w;
        int height = h;
        ByteBuffer buf = ByteBuffer.allocateDirect(width * height * 4);
        buf.order(ByteOrder.LITTLE_ENDIAN);
        GLES20.glReadPixels(0, 0, width, height, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, buf);
        GlUtil.checkGlError("glReadPixels");
        buf.rewind();
        //直接保存为rgba数据
//        FileOutputStream os = new FileOutputStream("/sdcard/test.rgba");
//        os.write(buf.array());
//        os.close();
        BufferedOutputStream bos = null;
        try {
            bos = new BufferedOutputStream(new FileOutputStream(filePath));
            Bitmap bmp = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888);
            bmp.copyPixelsFromBuffer(buf);
            Bitmap bmp2 = reverseBitmap(bmp,1);
            bmp2.compress(Bitmap.CompressFormat.JPEG, 90, bos);
            bmp2.recycle();
//            bmp.compress(Bitmap.CompressFormat.JPEG, 90, bos);
            bmp.recycle();

        } catch (FileNotFoundException e) {
            e.printStackTrace();

        } finally {
            if (bos != null) try {
                bos.close();
            } catch (IOException e) {
                e.printStackTrace();
            }
        }
    }

    /**
     * Picture reversal
     * @param bmp flag
     * @param flag 0:Horizontal reversal 1:Vertica reversal
     * @return Bitmap
     */
    public static Bitmap reverseBitmap(Bitmap bmp, int flag) {
        float[] floats = null;
        switch (flag) {
            case 0:
                floats = new float[] { -1f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 1f };
                break;
            case 1:
                floats = new float[] { 1f, 0f, 0f, 0f, -1f, 0f, 0f, 0f, 1f };
                break;
        }
        if (floats != null) {
            android.graphics.Matrix matrix = new android.graphics.Matrix();
            matrix.setValues(floats);
            return Bitmap.createBitmap(bmp, 0, 0, bmp.getWidth(), bmp.getHeight(), matrix, true);
        }
        return null;
    }
}
