package com.forrest.ui;

import android.content.Context;
import android.graphics.SurfaceTexture;
import android.opengl.EGL14;
import android.opengl.GLES20;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;
import android.util.Log;
import android.view.SurfaceHolder;

import com.forrest.encoder.MediaEncoderHandler;
import com.forrest.gles.GlUtil;
import com.forrest.gles.Rectangle;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class CameraGLSurfaceView extends GLSurfaceView {

    private final static String TAG = "CameraGLSurfaceView";
    private boolean surfaceAvailable = false;

    public final int GL_TEXTURE_EXTERNAL_OES = 0x8D65;
    private static final int RECORDING_OFF = 0;
    private static final int RECORDING_ON = 1;
    private static final int RECORDING_RESUMED = 2;

    private int mRecordingStatus = -1;
    private boolean mRecordingEnabled = false;
    private int mRecordWidth;
    private int mRecordHeight;
    private String mIP;
    private MediaEncoderHandler mMediaEncoderHandler;

    private boolean mPictureEnable = false;
    private int mPictureWidth;
    private int mPictureHeight;
    private String mPicturePath;

    private int mOffsetX = 0;
    private int mOffsetY = 0;
    private int mWidth = 0;
    private int mHeight = 0;
    private double mAspectRatio = 16.0f/9.0f;

    private int mCurFacing = 0;
    private int mMode = 0;

    public interface MySurfaceListener {
        void surfaceCreated();
        void surfaceChanged();
        void surfaceDestroyed();
    }

    private MyRenderer mRenderer;
    private MySurfaceListener mSurfaceListener;

    public CameraGLSurfaceView(Context context) {
        this(context, null);
    }

    public CameraGLSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        init();
    }

    private void init() {
        setEGLContextClientVersion(2);
        mRenderer = new MyRenderer();
        setRenderer(mRenderer);
        setRenderMode(GLSurfaceView.RENDERMODE_WHEN_DIRTY);
    }

    @Override
    public void onResume() {
        super.onResume();
//        mRenderer.onResume();
    }

    @Override
    public void onPause() {
        super.onPause();
//        mRenderer.onPause();
    }

    public void startRecord(int width, int height, String ip) {
        if(mRecordingStatus == RECORDING_ON) {
            return ;
        }
        mRecordingStatus = RECORDING_ON;
        this.mIP = ip;
        this.mRecordWidth = width;
        this.mRecordHeight = height;
    }

    public void stopRecord() {
        mRecordingEnabled = mMediaEncoderHandler.isRecording();
        if(mRecordingEnabled) {
            mRecordingStatus = RECORDING_OFF;
            mMediaEncoderHandler.stopRecording();
        }
    }

    public boolean isRecording() {
        if (mRecordingStatus == RECORDING_ON) {
            return true;
        } else {
            return false;
        }
    }

    /**
     * 拍照
     * @param width 宽
     * @param height 高
     * @param picturePath 保存路径
     */
    public void takePicture(int width, int height, String picturePath) {
        this.mPictureWidth = width;
        this.mPictureHeight = height;
        this.mPicturePath = picturePath;
        mPictureEnable = true;
    }

    public void setAspectRatio(double aspectRatio) {
        this.mAspectRatio = aspectRatio;
    }

    public void setAspectRatio(int width, int height) {
        this.mAspectRatio = (double)width / (double)height;
    }

    public SurfaceTexture getSurfaceTexture() {
        return mRenderer.getSurfaceTexture();
    }

    public void switchcamera(int facing, int mode) {
        mCurFacing = facing;
        mMode = mode;
        mRenderer.switchcamera(facing, mode);
        mMediaEncoderHandler.switchcamera(facing, mode);
    }

    public void setSurfaceListener(MySurfaceListener surfaceListener) {
        this.mSurfaceListener = surfaceListener;
    }

    public boolean isAvailable() {
        return this.surfaceAvailable;
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        super.surfaceCreated(holder);
        surfaceAvailable = true;
        Log.d(TAG, "[CameraGLSurfaceView] : surfaceCreated");
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        super.surfaceChanged(holder, format, w, h);
        Log.d(TAG, "[CameraGLSurfaceView] : surfaceChanged");
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        super.surfaceDestroyed(holder);
        if (mSurfaceListener != null) {
            mSurfaceListener.surfaceDestroyed();
        }
        //surfaceAvailable = false;
        Log.d(TAG, "[CameraGLSurfaceView] : surfaceDestroyed");
    }

    class MyRenderer implements Renderer, SurfaceTexture.OnFrameAvailableListener {

        private Rectangle mRect;
        private int previewTextureId;
        private SurfaceTexture surfaceTexture;
        private boolean updateSurface = false;

        //private Rectangle bgRect;
        private int bgTexureId;

        private int frameBufferId;
        private int i=0;
        private long t;

        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            mRect = new Rectangle();
            mRect.initVertexDataTexOES(mCurFacing,mMode);
            previewTextureId = GlUtil.generateTextureIdOES();
            surfaceTexture = new SurfaceTexture(previewTextureId);
            surfaceTexture.setOnFrameAvailableListener(this);
            synchronized (this) {
                updateSurface = false;
            }

            if (mSurfaceListener != null) {
                mSurfaceListener.surfaceCreated();
            }

            //bgRect = new Rectangle();
            //bgRect.initVertexDataTex2D();
            //bgTexureId = GlUtil.initTextureFromResID(R.drawable.wp, UVCCameraGLSurfaceView.this.getResources());

            //frameBufferId = GlUtil.genFrameBuffer(1080, 1920);

            mMediaEncoderHandler = new MediaEncoderHandler();
            Log.d(TAG, "[CameraGLSurfaceView MyRenderer] : onSurfaceCreated");
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            mWidth = width;
            mHeight = height;
//            mHeight = (int)(mWidth / mAspectRatio);
//            mOffsetY = (height - mHeight) / 2;
            //mOffsetY = height - 720;
            if (mSurfaceListener != null) {
                mSurfaceListener.surfaceChanged();
            }
            Log.d(TAG, "[CameraGLSurfaceView MyRenderer] : onSurfaceChanged (width :" + width + " height : " + height+ ")");
        }

        @Override
        public void onDrawFrame(GL10 gl) {
            synchronized (this) {
                if (updateSurface) {
                    surfaceTexture.updateTexImage();
                    GLES20.glClear(GLES20.GL_DEPTH_BUFFER_BIT | GLES20.GL_COLOR_BUFFER_BIT);

                    GLES20.glViewport(0, 0, mWidth, mHeight);
//                    bgRect.drawSelfTex2D(bgTexureId);
                    mRect.drawSelfOES(previewTextureId);
                    i++;
                    if (i == 1) {
                        t = System.currentTimeMillis();
                    } else if (i==1000) {
                        Log.d("test", "1000fps cost time :" + (System.currentTimeMillis()-t));
                    }

                    //录像相关操作
                    mRecordingEnabled = mMediaEncoderHandler.isRecording();
                    if (!mRecordingEnabled && mRecordingStatus == RECORDING_ON) {
                        mMediaEncoderHandler.startRecording(new MediaEncoderHandler.EncoderConfig(getContext(),
                                mIP, mRecordWidth, mRecordHeight, 10000000, EGL14.eglGetCurrentContext()));
                    }
                    if (mRecordingEnabled) {
                        mMediaEncoderHandler.setTextureId(previewTextureId);
                        mMediaEncoderHandler.frameAvailable(surfaceTexture);
                    }

                    //拍照相关操作
                    if (mPictureEnable) {
                        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, frameBufferId);
                        GLES20.glClear(GLES20.GL_DEPTH_BUFFER_BIT | GLES20.GL_COLOR_BUFFER_BIT);
                        GLES20.glViewport(0, 0, mPictureWidth, mPictureHeight);
                        mRect.drawSelfOES(previewTextureId);
                        GlUtil.saveFrame(mPicturePath, mPictureWidth, mPictureHeight);
                        GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, 0);
                        mPictureEnable = false;
                    }
                }
            }
        }

        @Override
        synchronized public void onFrameAvailable(SurfaceTexture surfaceTexture) {
            updateSurface = true;
            requestRender();
        }

        private SurfaceTexture getSurfaceTexture() {
            return this.surfaceTexture;
        }

        private void switchcamera(int facing, int mode) {
            synchronized (this) {
                updateSurface = false;
                mRect.reCreateVertexData(facing,mode);
            }
        }
    }
}
