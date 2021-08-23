package com.forrest.encoder;

import android.media.MediaCodec;
import android.media.MediaFormat;
import android.util.Log;
import java.io.IOException;
import java.nio.ByteBuffer;

import com.forrest.jrtplib.JrtplibUtil;

public class MediaMuxerWrapper {
	private static final boolean DEBUG = true;
	private static final String TAG = "MediaMuxerWrapper";

	private int mEncoderCount, mStatredCount;
	private boolean mIsStarted;
	private MediaEncoder mVideoEncoder, mAudioEncoder;

	private JrtplibUtil mJrtpLibUtil;
	private String mRemoteIP;
	private byte[] sps_pps = new byte[128];
	private int sps_pps_len;
	private byte[] databytes = new byte[1024*1024];

	MediaMuxerWrapper(String ip) {
		mEncoderCount = mStatredCount = 0;
		mIsStarted = false;
		mJrtpLibUtil = JrtplibUtil.newInstance();
		mRemoteIP = ip;
	}

	void prepare() throws IOException {
		if (mVideoEncoder != null) {
			mVideoEncoder.prepare();
		}
		if (mAudioEncoder != null) {
			mAudioEncoder.prepare();
		}
	}

	void startRecording() {
		if (mJrtpLibUtil != null) {
			String[] tmp = mRemoteIP.split("\\.");
			byte ip0 = (byte) Integer.valueOf(tmp[0]).intValue();
			byte ip1 = (byte) Integer.valueOf(tmp[1]).intValue();
			byte ip2 = (byte) Integer.valueOf(tmp[2]).intValue();
			byte ip3 = (byte) Integer.valueOf(tmp[3]).intValue();
			mJrtpLibUtil.createSendSession(new byte[] {ip0, ip1, ip2, ip3});
		}
		if (mVideoEncoder != null) {
			mVideoEncoder.startRecording();
		}
		if (mAudioEncoder != null) {
			mAudioEncoder.startRecording();
		}
	}

	void stopRecording() {
		if (mVideoEncoder != null) {
			mVideoEncoder.stopRecording();
			mVideoEncoder = null;
		}
		if (mAudioEncoder != null) {
			mAudioEncoder.stopRecording();
			mAudioEncoder = null;
		}
		if (mJrtpLibUtil != null) {
			mJrtpLibUtil.destroySendSession();
		}
	}

	synchronized boolean isStarted() {
		return mIsStarted;
	}

//**********************************************************************
//**********************************************************************
	/**
	 * assign encoder to this calss. this is called from encoder.
	 * @param encoder instance of MediaVideoEncoder or MediaAudioEncoder
	 */
	/*package*/ void addEncoder(final MediaEncoder encoder) {
		/*if (encoder instanceof MediaVideoEncoder) {
			if (mVideoEncoder != null)
				throw new IllegalArgumentException("Video encoder already added.");
			mVideoEncoder = encoder;
		} else*/ if (encoder instanceof MediaSurfaceEncoder) {
				if (mVideoEncoder != null)
					throw new IllegalArgumentException("Video encoder already added.");
				mVideoEncoder = encoder;
		} else if (encoder instanceof MediaVideoBufferEncoder) {
			if (mVideoEncoder != null)
				throw new IllegalArgumentException("Video encoder already added.");
			mVideoEncoder = encoder;
		} else if (encoder instanceof MediaAudioEncoder) {
			if (mAudioEncoder != null)
				throw new IllegalArgumentException("Video encoder already added.");
			mAudioEncoder = encoder;
		} else
			throw new IllegalArgumentException("unsupported encoder");
		mEncoderCount = (mVideoEncoder != null ? 1 : 0) + (mAudioEncoder != null ? 1 : 0);
	}

	 synchronized boolean start() {
		if (DEBUG) Log.v(TAG,  "[MediaMuxerWrapper]: start");
		mStatredCount++;
		if ((mEncoderCount > 0) && (mStatredCount == mEncoderCount)) {
			mIsStarted = true;
			notifyAll();
			if (DEBUG) Log.v(TAG,  "MediaMuxer started:");
		}
		return mIsStarted;
	}

	/**
	 * request stop recording from encoder when encoder received EOS
	*/
	synchronized void stop() {
		if (DEBUG) Log.v(TAG,  "[MediaMuxerWrapper]: stop mStatredCount=" + mStatredCount);
		mStatredCount--;
		if ((mEncoderCount > 0) && (mStatredCount <= 0)) {
			mIsStarted = false;
			if (DEBUG) Log.v(TAG,  "[MediaMuxerWrapper]: MediaMuxer stopped");
		}
	}

	synchronized int addTrack(final MediaFormat format) {
		if (mIsStarted) {
			throw new IllegalStateException("muxer already started");
		}
		int trackIx = 0;
		String mimeType = format.getString(MediaFormat.KEY_MIME); // video/avc audio/mp4a-latm
        if (mimeType.contains("video")) {
            trackIx = 1;
        } else if (mimeType.contains("audio")) {
            trackIx = 2;
        }
		if (DEBUG) Log.i(TAG, "[MediaMuxerWrapper]: addTrack:trackNum=" + mEncoderCount + ",trackIx=" + trackIx + ",format=" + format);
		return trackIx;
	}

	synchronized void writeSampleData(final int trackIndex, final ByteBuffer byteBuffer, final MediaCodec.BufferInfo bufferInfo) {
		if (mStatredCount > 0){
			if (trackIndex == 1) {
//				Log.d(TAG, String.format(" trackIndex(%d) BufferInfo(size(%d), flags(%d), ptsMs(%d))",
//						trackIndex, bufferInfo.size, bufferInfo.flags, bufferInfo.presentationTimeUs/1000));

				if (bufferInfo.flags == 2) { // sps pps
					sps_pps_len = bufferInfo.size;
					byteBuffer.get(sps_pps, 0, bufferInfo.size);

				} else if (bufferInfo.flags == 1 || bufferInfo.flags == 9) { // I
					mJrtpLibUtil.sendData(sps_pps, sps_pps_len, 1);
					byteBuffer.get(databytes, 0, bufferInfo.size);
					mJrtpLibUtil.sendData(databytes, bufferInfo.size, 1);

				} else if (bufferInfo.flags == 0 || bufferInfo.flags == 8) { // P
					byteBuffer.get(databytes, 0, bufferInfo.size);
					mJrtpLibUtil.sendData(databytes, bufferInfo.size, 1);
				}


			} else if (trackIndex == 2) {
//				Log.d(TAG, String.format(" trackIndex(%d) BufferInfo(size(%d), flags(%d), ptsMs(%d))",
//						trackIndex, bufferInfo.size, bufferInfo.flags, bufferInfo.presentationTimeUs/1000));
				byteBuffer.get(databytes, 7, bufferInfo.size);
				addADTStoPacket(databytes, bufferInfo.size+7);
				mJrtpLibUtil.sendData(databytes, bufferInfo.size+7, 2);
			}
		}
	}

	/**
	 * 给编码出的aac裸流添加adts头字段
	 * @param packet 要空出前7个字节，否则会搞乱数据
	 * @param packetLen
	 */
	private void addADTStoPacket(byte[] packet, int packetLen) {
		int profile = 2;  //AAC LC
		int freqIdx = 4;  //44.1KHz
		int chanCfg = 1;  //CPE
		packet[0] = (byte) 0xFF;
		packet[1] = (byte) 0xF9;
		packet[2] = (byte) (((profile - 1) << 6) + (freqIdx << 2) + (chanCfg >> 2));
		packet[3] = (byte) (((chanCfg & 3) << 6) + (packetLen >> 11));
		packet[4] = (byte) ((packetLen & 0x7FF) >> 3);
		packet[5] = (byte) (((packetLen & 7) << 5) + 0x1F);
		packet[6] = (byte) 0xFC;
	}


}
