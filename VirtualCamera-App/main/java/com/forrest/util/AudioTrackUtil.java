package com.forrest.util;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;

import java.nio.ByteBuffer;

public class AudioTrackUtil {
    private final static String TAG = "AudioTrackUtil";
    private AudioTrack mAudioTrack;
    private boolean isCreate = false;

    public void create() {
        if (isCreate) return;
        isCreate = true;
        int bufferSize = AudioTrack.getMinBufferSize(44100, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        mAudioTrack = new AudioTrack(AudioManager.STREAM_MUSIC, 44100, AudioFormat.CHANNEL_OUT_MONO,
                AudioFormat.ENCODING_PCM_16BIT, bufferSize*2, AudioTrack.MODE_STREAM);
        mAudioTrack.play();
    }

    public void writeData(byte[] data, int offset, int dataLen) {
        if (!isCreate) return;
        mAudioTrack.write(data, offset, dataLen);
    }

    public void release() {
        if (!isCreate) return;
        isCreate = false;
        mAudioTrack.stop();
        mAudioTrack.release();
    }

}
