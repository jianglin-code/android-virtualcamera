package com.forrest.ui;

import android.content.Context;
import android.graphics.SurfaceTexture;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;

import com.forrest.gles.GlUtil;
import com.forrest.gles.Rectangle;
import com.forrest.jrtplib.JrtplibUtil;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class PreviewGLSurfaceView extends GLSurfaceView implements SurfaceHolder.Callback {
    private final static String TAG = "Preview";
    private final Object mSync = new Object();
    private MyRenderer mRenderer;
    private int mWidth;
    private int mHeight;

    public PreviewGLSurfaceView(Context context) {
        this(context, null);
    }

    public PreviewGLSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        setEGLContextClientVersion(2);
        mRenderer = new MyRenderer();
        setRenderer(mRenderer);
        setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
//      setPreserveEGLContextOnPause(true);
    }

    @Override
    public void onResume() {
        super.onResume();
        mRenderer.onResume();
        Log.d(TAG, "onResume");
    }

    @Override
    public void onPause() {
        super.onPause();
        mRenderer.onPause();
        Log.d(TAG, "onPause");
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        super.surfaceCreated(holder);
        Log.d(TAG, "surfaceCreated");
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        super.surfaceChanged(holder, format, w, h);
        Log.d(TAG, "surfaceChanged");
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        super.surfaceDestroyed(holder);
//        JrtplibUtil.newInstance().displayDestroy();
        Log.d(TAG, "surfaceDestroyed");
    }

    class MyRenderer implements Renderer, SurfaceTexture.OnFrameAvailableListener /*, JrtplibUtil.OnFrameAvailableListener*/{

        private Rectangle mRect;
        private int previewTextureId;
        private SurfaceTexture surfaceTexture;
        private boolean updateSurface = false;

        private void onResume() {
        }

        private void onPause() {
        }

        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            mRect = new Rectangle();
            mRect.initVertexDataTexOES(0,0);
            previewTextureId = GlUtil.generateTextureIdOES();
            surfaceTexture = new SurfaceTexture(previewTextureId);
            surfaceTexture.setDefaultBufferSize(1280, 720);
            surfaceTexture.setOnFrameAvailableListener(this);
            JrtplibUtil.newInstance().setSurface(new Surface(surfaceTexture));
//
//            rectBg = new Rectangle();
//            rectBg.initVertexDataTex2D();
//            texIdBg = GlUtil.initTextureFromResID(R.drawable.bg, PreviewGLSurfaceView.this.getResources());

//            JrtplibUtil.newInstance().displayInit();
//            JrtplibUtil.newInstance().setOnFrameAvailableListener(this);
            Log.d(TAG, "[MyRenderer] : onSurfaceCreated");
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            mWidth = width;
            mHeight = height;
            Log.d(TAG, "[MyRenderer] : onSurfaceChanged width: " + width + " height: " + height);
        }

        @Override
        public void onDrawFrame(GL10 gl) {
            synchronized (mSync) {
                if (updateSurface) {
                    updateSurface = false;
                    GLES20.glClear(GLES20.GL_DEPTH_BUFFER_BIT | GLES20.GL_COLOR_BUFFER_BIT);
                    GLES20.glViewport(0, 0, 1280, 720);
//                    JrtplibUtil.newInstance().displayDraw(0, 0, 1280, 720);
                    surfaceTexture.updateTexImage();
                    mRect.drawSelfOES(previewTextureId);
                }
            }
        }

        @Override
        public void onFrameAvailable(SurfaceTexture surfaceTexture) {
            updateSurface = true;
            requestRender();
        }

//        @Override
//        public void onFrameAvailable(/*SurfaceTexture surfaceTexture*/) {
//            updateSurface = true;
//            requestRender();
//        }

    }

}
