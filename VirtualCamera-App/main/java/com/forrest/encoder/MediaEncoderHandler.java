/*
 * Copyright 2013 Google Inc. All rights reserved.
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

package com.forrest.encoder;

import android.content.Context;
import android.graphics.SurfaceTexture;
import android.opengl.EGLContext;
import android.opengl.GLES20;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.util.Log;

import com.forrest.gles.EglCore;
import com.forrest.gles.Rectangle;
import com.forrest.gles.WindowSurface;

import java.io.File;
import java.io.IOException;
import java.lang.ref.WeakReference;
import java.text.SimpleDateFormat;

/**
 * 录制surface 并且采集MIC的声音,一起封装成mp4文件
 */
public class MediaEncoderHandler implements Runnable {
    private final boolean DEBUG = true;
    private static final String TAG = "MediaEncoderHandler";
    public final int GL_TEXTURE_EXTERNAL_OES = 0x8D65;

    private static final int MSG_START_RECORDING = 0;
    private static final int MSG_STOP_RECORDING = 1;
    private static final int MSG_FRAME_AVAILABLE = 2;
    private static final int MSG_SET_TEXTURE_ID = 3;
    private static final int MSG_UPDATE_SHARED_CONTEXT = 4;
    private static final int MSG_QUIT = 5;

    // ----- accessed exclusively by encoder thread -----
    private WindowSurface mInputWindowSurface;
    private EglCore mEglCore;
//    private FullFrameRect mFullScreen;
    private Rectangle mRect;

    private int mTextureId;
    private int mFrameNum;
//    private VideoEncoderCore mVideoEncoder;
    private MediaMuxerWrapper mMuxer;
    private MediaSurfaceEncoder mVideoEncoder;
    private final Object mSync = new Object();
    private EncoderConfig mConfig;

    private int mWidth;
    private int mHeight;
    private Context mContext;

    // ----- accessed by multiple threads -----
    private volatile EncoderHandler mHandler;

    private final Object mReadyFence = new Object();      // guards ready/running
    private boolean mReady;
    private boolean mRunning;

    private int mCurFacing = 0;
    private int mMode = 0;

    public static class EncoderConfig {
        final String mIP;
        final int mWidth;
        final int mHeight;
        final int mBitRate;
        final EGLContext mEglContext;
        final Context mContext;

        public EncoderConfig(String ip, int width, int height, int bitRate, EGLContext sharedEglContext) {
            mIP = ip;
            mWidth = width;
            mHeight = height;
            mBitRate = bitRate;
            mEglContext = sharedEglContext;
            mContext = null;
        }

        public EncoderConfig(Context context , String ip, int width, int height, int bitRate, EGLContext sharedEglContext) {
            mIP = ip;
            mWidth = width;
            mHeight = height;
            mBitRate = bitRate;
            mEglContext = sharedEglContext;
            mContext = context;
        }

        @Override
        public String toString() {
            return "EncoderConfig: " + mWidth + "x" + mHeight + " @" + mBitRate + " to '" + mIP + "' ctxt=" + mEglContext;
        }
    }

    public void switchcamera(int facing,int mode) {
        mCurFacing = facing;
        mMode = mode;
    }

    public void startRecording(EncoderConfig config) {
        Log.d(TAG, "startRecording");
        synchronized (mReadyFence) {
            if (mRunning) {
                Log.w(TAG, "Encoder thread already running");
                return;
            }
            mRunning = true;
            new Thread(this, "TextureMovieEncoder").start();
            while (!mReady) {
                try {
                    mReadyFence.wait();
                } catch (InterruptedException ie) {
                    // ignore
                }
            }
        }
        mConfig = config;
        mHandler.sendMessage(mHandler.obtainMessage(MSG_START_RECORDING, config));
    }
    
    public void stopRecording() {
    	mHandler.removeMessages(MSG_FRAME_AVAILABLE);
        mHandler.sendMessage(mHandler.obtainMessage(MSG_STOP_RECORDING));
        mHandler.sendMessage(mHandler.obtainMessage(MSG_QUIT));
        // We don't know when these will actually finish (or even start).  We don't want to
        // delay the UI thread though, so we return immediately.
    }
    
    public boolean isRecording() {
        synchronized (mReadyFence) {
            return mRunning;
        }
    }

    /**
     * Tells the video recorder to refresh its EGL surface.  (Call from non-encoder thread.)
     */
    public void updateSharedContext(EGLContext sharedContext) {
        mHandler.sendMessage(mHandler.obtainMessage(MSG_UPDATE_SHARED_CONTEXT, sharedContext));
    }

    float[] transform = new float[16];
    public void frameAvailable(SurfaceTexture st) {
        synchronized (mReadyFence) {
            if (!mReady) {
                return;
            }
        }
        st.getTransformMatrix(transform);
        long timestamp = st.getTimestamp();
        if (timestamp == 0) {
            // Seeing this after device is toggled off/on with power button.
            // The first frame back has a zero timestamp.
            // MPEG4Writer thinks this is cause to abort() in native code,
            // so it's very important that we just ignore the frame.
            Log.w(TAG, "HEY: got SurfaceTexture with timestamp of zero");
            return;
        }
        mHandler.sendMessage(mHandler.obtainMessage(MSG_FRAME_AVAILABLE, (int) (timestamp >> 32), (int) timestamp, transform));
    }

    public void setTextureId(int id) {
        synchronized (mReadyFence) {
            if (!mReady) {
                return;
            }
        }
        mHandler.sendMessage(mHandler.obtainMessage(MSG_SET_TEXTURE_ID, id, 0, null));
    }

    @Override
    public void run() {
        // Establish a Looper for this thread, and define a Handler for it.
        Looper.prepare();
        synchronized (mReadyFence) {
            mHandler = new EncoderHandler(this);
            mReady = true;
            mReadyFence.notify();
        }
        Looper.loop();

        Log.d(TAG, "Encoder thread exiting");
        synchronized (mReadyFence) {
            mReady = mRunning = false;
            mHandler = null;
        }
    }


    /**
     * Handles encoder state change requests.  The handler is created on the encoder thread.
     */
    private static class EncoderHandler extends Handler {
        private WeakReference<MediaEncoderHandler> mWeakEncoder;

        public EncoderHandler(MediaEncoderHandler encoder) {
            mWeakEncoder = new WeakReference<MediaEncoderHandler>(encoder);
        }

        @Override  // runs on encoder thread
        public void handleMessage(Message inputMessage) {
            int what = inputMessage.what;
            Object obj = inputMessage.obj;

            MediaEncoderHandler encoder = mWeakEncoder.get();
            if (encoder == null) {
                Log.w(TAG, "EncoderHandler.handleMessage: encoder is null");
                return;
            }

            switch (what) {
                case MSG_START_RECORDING:
                    encoder.handleStartRecording((EncoderConfig) obj);
                    break;

                case MSG_STOP_RECORDING:
                    encoder.handleStopRecording();
                    break;

                case MSG_FRAME_AVAILABLE:
                    long timestamp = (((long) inputMessage.arg1) << 32) | (((long) inputMessage.arg2) & 0xffffffffL);
                    encoder.handleFrameAvailable((float[]) obj, timestamp);
                    break;

                case MSG_SET_TEXTURE_ID:
                    encoder.handleSetTexture(inputMessage.arg1);
                    break;

                case MSG_UPDATE_SHARED_CONTEXT:
                    encoder.handleUpdateSharedContext((EGLContext) inputMessage.obj);
                    break;

                case MSG_QUIT:
                    Looper.myLooper().quit();
                    break;

                default:
                    throw new RuntimeException("Unhandled msg what=" + what);
            }
        }
    }

    private void handleStartRecording(EncoderConfig config) {
        Log.d(TAG, "handleStartRecording " + config);
        mFrameNum = 0;
        mWidth = config.mWidth;
        mHeight = config.mHeight;
        mContext = config.mContext;
        prepareEncoder(config.mEglContext, config.mWidth, config.mHeight, config.mBitRate, config.mIP);
    }

    private void handleFrameAvailable(float[] transform, long timestampNanos) {
        GLES20.glClear(GLES20.GL_DEPTH_BUFFER_BIT | GLES20.GL_COLOR_BUFFER_BIT);
        GLES20.glViewport(0, 0, mWidth, mHeight);
        mRect.drawSelfOES(mTextureId);
//        drawBox(mFrameNum++);
        mInputWindowSurface.setPresentationTime(timestampNanos);
        mInputWindowSurface.swapBuffers();
        mVideoEncoder.frameAvailableSoon();
    }

    private void handleStopRecording() {
        Log.d(TAG, "handleStopRecording");
        final MediaMuxerWrapper muxer;
        synchronized (mSync) {
            muxer = mMuxer;
            mMuxer = null;
            if (muxer != null) {
                muxer.stopRecording();
            }

            if (mInputWindowSurface != null) {
                mInputWindowSurface.release();
                mInputWindowSurface = null;
            }

            if(mRect != null) {
                mRect.release();
                mRect = null;
            }

            if (mEglCore != null) {
                mEglCore.release();
                mEglCore = null;
            }
        }
    }
    
    private void handleSetTexture(int id) {
        mTextureId = id;
    }

    private void handleUpdateSharedContext(EGLContext newSharedContext) {
        Log.d(TAG, "handleUpdatedSharedContext " + newSharedContext);
        // Release the EGLSurface and EGLContext.
        mInputWindowSurface.releaseEglSurface();
        mEglCore.release();

        // Create a new EGLContext and recreate the window surface.
        mEglCore = new EglCore(newSharedContext, EglCore.FLAG_RECORDABLE);
        mInputWindowSurface.recreate(mEglCore);
        mInputWindowSurface.makeCurrent();

        // Create new programs and such for the new context.
        // mFullScreen = new FullFrameRect(new Texture2dProgram(Texture2dProgram.ProgramType.TEXTURE_EXT));
    }

    // 准备编码
    private void prepareEncoder(EGLContext sharedContext, int width, int height, int bitRate, String ip) {
        try {
            final MediaMuxerWrapper muxer = new MediaMuxerWrapper(ip);
            MediaSurfaceEncoder videoEncoder = new MediaSurfaceEncoder(muxer, width, height, mMediaEncoderListener);
            //new MediaAudioEncoder(muxer, mMediaEncoderListener);
            muxer.prepare();
            muxer.startRecording();
            synchronized (mSync) {
                mMuxer = muxer;
                mVideoEncoder = videoEncoder;
            }
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    private final MediaEncoder.MediaEncoderListener mMediaEncoderListener = new MediaEncoder.MediaEncoderListener() {
        @Override
        public void onPrepared(final MediaEncoder encoder) {
            if (DEBUG) Log.v(TAG, "onPrepared:encoder=" + encoder);
            if (encoder instanceof MediaSurfaceEncoder) {
                try {
                    mEglCore = new EglCore(mConfig.mEglContext, EglCore.FLAG_RECORDABLE);
                    mInputWindowSurface = new WindowSurface(mEglCore, ((MediaSurfaceEncoder)encoder).getInputSurface(), true);
                    mInputWindowSurface.makeCurrent();

                    mRect = new Rectangle();
                    mRect.initVertexDataTexOES(mCurFacing,mMode);

                } catch (final Exception e) {
                    Log.e(TAG, "onPrepared:", e);
                }
            }
        }

        @Override
        public void onStopped(final MediaEncoder encoder) {
            if (DEBUG) Log.v(TAG, "onStopped:encoder=" + encoder);
        }
    };

    /**
     * Draws a box, with position offset.
     */
    private void drawBox(int posn) {
        final int width = mInputWindowSurface.getWidth();
        int xpos = (posn * 4) % (width - 50);
        GLES20.glEnable(GLES20.GL_SCISSOR_TEST);
        GLES20.glScissor(xpos, 0, 100, 100);
        GLES20.glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        GLES20.glDisable(GLES20.GL_SCISSOR_TEST);
    }
    
}
