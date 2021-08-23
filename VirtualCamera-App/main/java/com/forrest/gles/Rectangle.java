package com.forrest.gles;

import android.opengl.GLES20;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

public class Rectangle {

    private static final int GL_TEXTURE_EXTERNAL_OES = 0x8D65;
    private static final int FLOAT_SIZE_BYTES = 4;
    private static final int VERTICES_DATA_STRIDE_BYTES = 5 * FLOAT_SIZE_BYTES;
    private static final int VERTICES_DATA_POS_OFFSET = 0;
    private static final int VERTICES_DATA_UV_OFFSET = 3;
    private FloatBuffer mVertices;

    private int mProgram = -1;
    private int mPositionHandle;
    private int mTextureHandle;

    private final String mVertexShader =
        "uniform mat4 uMVPMatrix;\n" +
        "uniform mat4 uSTMatrix;\n" +
        "attribute vec4 aPosition;\n" +
        "attribute vec4 aTextureCoord;\n" +
        "varying vec2 vTextureCoord;\n" +
        "void main() {\n" +
        "  gl_Position = uMVPMatrix * aPosition;\n" +
        "  vTextureCoord = (uSTMatrix * aTextureCoord).xy;\n" +
        "}\n";

    private final String mFragmentShader =
        "#extension GL_OES_EGL_image_external : require\n" +
        "precision mediump float;\n" +
        "varying vec2 vTextureCoord;\n" +
        "uniform samplerExternalOES sTexture;\n" +
        "void main() {\n" +
        "  gl_FragColor = texture2D(sTexture, vTextureCoord);\n" +
        "}\n";

    private final static String mVertexShaderTex2DSimple =
        "attribute vec4 aPosition;                              \n" +
        "attribute vec2 aTextureCoord;                          \n" +
        "varying vec2 vTextureCoord;                            \n" +
        "void main() {                                          \n" +
        "  gl_Position = aPosition;                             \n" +
        "  vTextureCoord = aTextureCoord;                       \n" +
        "}                                                      \n";

    private final static String mFragmentShaderTex2DSimple =
        "precision mediump float;                               \n" +
        "varying vec2 vTextureCoord;                            \n" +
        "uniform sampler2D sTexture;                            \n" +
        "void main() {                                          \n" +
        "  gl_FragColor = texture2D(sTexture, vTextureCoord);   \n" +
        "}                                                      \n";

    private final static String mVertexShaderSimple =
        "attribute vec4 aPosition;                              \n" +
        "attribute vec2 aTextureCoord;                          \n" +
        "varying vec2 vTextureCoord;                            \n" +
        "void main() {                                          \n" +
        "  gl_Position = aPosition;                             \n" +
        "  vTextureCoord = aTextureCoord;                       \n" +
        "}                                                      \n";

    private final static String mFragmentShaderSimple =
        "#extension GL_OES_EGL_image_external : require\n" +
        "precision mediump float;\n" +
        "varying vec2 vTextureCoord;\n" +
        "uniform samplerExternalOES sTexture;\n" +
        "void main() {\n" +
        "  gl_FragColor = texture2D(sTexture, vTextureCoord);\n" +
        "}\n";

    private final float[] mVerticesData = {
            // X, Y, Z, U, V
            -1.0f,  1.0f, 0.0f,  // Position 0 左上
            0.0f,  0.0f,         // TexCoord 0

            -1.0f, -1.0f, 0.0f,  // Position 1 左下
            0.0f,  1.0f,         // TexCoord 1

            1.0f,  1.0f, 0.0f,   // Position 2 右上
            1.0f,  0.0f,         // TexCoord 2

            1.0f, -1.0f, 0.0f,   // Position 3
            1.0f,  1.0f,         // TexCoord 3 右下
    };

    private final float[] mVerticesData0 = {
            // X, Y, Z, U, V
            -1.0f,  1.0f, 0.0f,  // Position 0 左上
            1.0f,  0.0f,         // TexCoord 0

            -1.0f, -1.0f, 0.0f,  // Position 1 左下
            0.0f,  0.0f,         // TexCoord 1

            1.0f,  1.0f, 0.0f,   // Position 2 右上
            1.0f,  1.0f,         // TexCoord 2

            1.0f, -1.0f, 0.0f,   // Position 3
            0.0f,  1.0f,         // TexCoord 3 右下
    };

    private final float[] mVerticesData1 = {
            // X, Y, Z, U, V
            -1.0f,  1.0f, 0.0f,  // Position 0 左上
            1.0f,  1.0f,         // TexCoord 0

            -1.0f, -1.0f, 0.0f,  // Position 1 左下
            1.0f,  0.0f,         // TexCoord 1

            1.0f,  1.0f, 0.0f,   // Position 2 右上
            0.0f,  1.0f,         // TexCoord 2

            1.0f, -1.0f, 0.0f,   // Position 3
            0.0f,  0.0f,         // TexCoord 3 右下
    };

    private final float[] mVerticesData2 = {
            // X, Y, Z, U, V
            -1.0f,  1.0f, 0.0f,  // Position 0 左上
            0.0f,  1.0f,         // TexCoord 0

            -1.0f, -1.0f, 0.0f,  // Position 1 左下
            1.0f,  1.0f,         // TexCoord 1

            1.0f,  1.0f, 0.0f,   // Position 2 右上
            0.0f,  0.0f,         // TexCoord 2

            1.0f, -1.0f, 0.0f,   // Position 3
            1.0f,  0.0f,         // TexCoord 3 右下
    };

    public void initVertexDataTex2D() {
        mVertices = ByteBuffer.allocateDirect(mVerticesData1.length * FLOAT_SIZE_BYTES).order(ByteOrder.nativeOrder()).asFloatBuffer();
        mVertices.put(mVerticesData1).position(0);

        mProgram = GlUtil.createProgram(mVertexShaderTex2DSimple, mFragmentShaderTex2DSimple);

        mPositionHandle = GLES20.glGetAttribLocation(mProgram, "aPosition");
        GlUtil.checkGlError("glGetAttribLocation aPosition");

        mTextureHandle = GLES20.glGetAttribLocation(mProgram, "aTextureCoord");
        GlUtil.checkGlError("glGetAttribLocation aTextureCoord");
    }

    public void drawSelfTex2D(int textureId) {
        GLES20.glUseProgram(mProgram);
        GlUtil.checkGlError("glUseProgram");

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, textureId);

        mVertices.position(VERTICES_DATA_POS_OFFSET);
        GLES20.glVertexAttribPointer(mPositionHandle, 3, GLES20.GL_FLOAT, false, VERTICES_DATA_STRIDE_BYTES, mVertices);
        GlUtil.checkGlError("glVertexAttribPointer maPosition");
        GLES20.glEnableVertexAttribArray(mPositionHandle);
        GlUtil.checkGlError("glEnableVertexAttribArray maPositionHandle");

        mVertices.position(VERTICES_DATA_UV_OFFSET);
        GLES20.glVertexAttribPointer(mTextureHandle, 2, GLES20.GL_FLOAT, false, VERTICES_DATA_STRIDE_BYTES, mVertices);
        GlUtil.checkGlError("glVertexAttribPointer maTextureHandle");
        GLES20.glEnableVertexAttribArray(mTextureHandle);
        GlUtil.checkGlError("glEnableVertexAttribArray maTextureHandle");

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        GlUtil.checkGlError("glDrawArrays");
    }

    public void reCreateVertexData(int facing, int mode) {
        if(facing == 1) {
            if(mode == 1){
                mVertices = ByteBuffer.allocateDirect(mVerticesData1.length * FLOAT_SIZE_BYTES).order(ByteOrder.nativeOrder()).asFloatBuffer();
                mVertices.put(mVerticesData1).position(0);
            }else{
                mVertices = ByteBuffer.allocateDirect(mVerticesData2.length * FLOAT_SIZE_BYTES).order(ByteOrder.nativeOrder()).asFloatBuffer();
                mVertices.put(mVerticesData2).position(0);
            }
        }else{
            if(mode == 1){
                mVertices = ByteBuffer.allocateDirect(mVerticesData.length * FLOAT_SIZE_BYTES).order(ByteOrder.nativeOrder()).asFloatBuffer();
                mVertices.put(mVerticesData).position(0);
            }else{
                mVertices = ByteBuffer.allocateDirect(mVerticesData0.length * FLOAT_SIZE_BYTES).order(ByteOrder.nativeOrder()).asFloatBuffer();
                mVertices.put(mVerticesData0).position(0);
            }
        }
    }

    public void initVertexDataTexOES(int facing, int mode) {
        reCreateVertexData(facing, mode);

        mProgram = GlUtil.createProgram(mVertexShaderSimple, mFragmentShaderSimple);

        mPositionHandle = GLES20.glGetAttribLocation(mProgram, "aPosition");
        GlUtil.checkGlError("glGetAttribLocation aPosition");

        mTextureHandle = GLES20.glGetAttribLocation(mProgram, "aTextureCoord");
        GlUtil.checkGlError("glGetAttribLocation aTextureCoord");
    }

    public void drawSelfOES(int textureId) {
        GLES20.glUseProgram(mProgram);
        GlUtil.checkGlError("glUseProgram");

        GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
        GLES20.glBindTexture(GL_TEXTURE_EXTERNAL_OES, textureId);

        mVertices.position(VERTICES_DATA_POS_OFFSET);
        GLES20.glVertexAttribPointer(mPositionHandle, 3, GLES20.GL_FLOAT, false, VERTICES_DATA_STRIDE_BYTES, mVertices);
        GlUtil.checkGlError("glVertexAttribPointer maPosition");
        GLES20.glEnableVertexAttribArray(mPositionHandle);
        GlUtil.checkGlError("glEnableVertexAttribArray maPositionHandle");

        mVertices.position(VERTICES_DATA_UV_OFFSET);
        GLES20.glVertexAttribPointer(mTextureHandle, 2, GLES20.GL_FLOAT, false, VERTICES_DATA_STRIDE_BYTES, mVertices);
        GlUtil.checkGlError("glVertexAttribPointer maTextureHandle");
        GLES20.glEnableVertexAttribArray(mTextureHandle);
        GlUtil.checkGlError("glEnableVertexAttribArray maTextureHandle");

        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        GlUtil.checkGlError("glDrawArrays");
    }

    public void release() {
        GLES20.glDeleteProgram(mProgram);
        mProgram = -1;
    }

}
