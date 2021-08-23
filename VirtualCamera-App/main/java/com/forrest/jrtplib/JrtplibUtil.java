package com.forrest.jrtplib;

import android.view.Surface;

import com.forrest.util.AudioTrackUtil;


public class JrtplibUtil {

    static {
        System.loadLibrary("jrtplib");
    }

    private long mContext;

    private static JrtplibUtil instance;

    //private AudioTrackUtil mAudioTrackUtil = new AudioTrackUtil();

    private JrtplibUtil() {}

    public static JrtplibUtil newInstance() {
        if (instance == null) {
            instance = new JrtplibUtil();
        }
        return instance;
    }

    public interface OnFrameAvailableListener {
        void onFrameAvailable();
    }

    private OnFrameAvailableListener listener;

    public native void createSendSession(byte[] ip);
    public native void destroySendSession();
    public native void sendData(byte[] data, int dataLen, int dataType);
    public native void receiveData();

    public native void displayInit();
    public native void displayDraw(int x, int y, int w, int h);
    public native void displayDestroy();
    public native void setSurface(Surface surface);
    public native void releaseSurface();

    public void setOnFrameAvailableListener(OnFrameAvailableListener l) {
        this.listener = l;
    }

    public void postEventFromNative(int event, byte[] data, int dataLen) {
        if (event == 1) {
            listener.onFrameAvailable();
        } else if (event == 2) {
            //mAudioTrackUtil.create();
            //mAudioTrackUtil.writeData(data, 0, dataLen);
        }
    }

}
