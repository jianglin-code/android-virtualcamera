package com.forrest.communication;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.content.Context;
import android.content.DialogInterface;
import android.content.pm.PackageManager;
import android.content.res.Resources;
import android.graphics.SurfaceTexture;
import android.graphics.Rect;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraCaptureSession;
import android.hardware.camera2.CameraCharacteristics;
import android.hardware.camera2.CameraMetadata;
import android.hardware.camera2.CameraDevice;
import android.hardware.camera2.CameraManager;
import android.hardware.camera2.CaptureRequest;
import android.hardware.camera2.params.StreamConfigurationMap;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.support.annotation.NonNull;
import android.support.v4.app.ActivityCompat;
import android.support.v4.app.DialogFragment;
import android.support.v4.app.Fragment;
import android.support.v4.content.ContextCompat;
import android.util.Log;
import android.util.Size;
import android.util.SparseIntArray;
import android.util.Range;
import android.util.TypedValue;
import android.view.LayoutInflater;
import android.view.Surface;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.RelativeLayout;

import com.forrest.ui.CameraGLSurfaceView;
import com.forrest.ui.PreviewGLSurfaceView;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;


public class Camera2BasicFragment extends Fragment implements View.OnClickListener, ActivityCompat.OnRequestPermissionsResultCallback {

    /**
     * Conversion from screen rotation to JPEG orientation.
     */
    private static final SparseIntArray ORIENTATIONS = new SparseIntArray();
    private static final int REQUEST_PERMISSION = 1;
    private static final String FRAGMENT_DIALOG = "dialog";

    static {
        ORIENTATIONS.append(Surface.ROTATION_0, 90);
        ORIENTATIONS.append(Surface.ROTATION_90, 0);
        ORIENTATIONS.append(Surface.ROTATION_180, 270);
        ORIENTATIONS.append(Surface.ROTATION_270, 180);
    }

    private static final String TAG = "Camera2BasicFragment";

    private static final int STATE_PREVIEW = 0;
    private static final int STATE_WAITING_LOCK = 1;
    private static final int STATE_WAITING_PRECAPTURE = 2;
    private static final int STATE_WAITING_NON_PRECAPTURE = 3;
    private static final int STATE_PICTURE_TAKEN = 4;

    private static final int MAX_PREVIEW_WIDTH_SB = 640;
    private static final int MAX_PREVIEW_HEIGHT_SB = 480;

    private static final int MAX_PREVIEW_WIDTH_SM = 1080;
    private static final int MAX_PREVIEW_HEIGHT_SM = 1920;

    private final CameraGLSurfaceView.MySurfaceListener mySurfaceListener = new CameraGLSurfaceView.MySurfaceListener() {
        @Override
        public void surfaceCreated() {
            openCamera();
        }

        @Override
        public void surfaceChanged() {

        }

        @Override
        public void surfaceDestroyed() {

        }
    };

    private String mCameraId;

    private CameraGLSurfaceView mGLSurfaceView;

    //private PreviewGLSurfaceView mPreviewView;

    /**
     * A {@link CameraCaptureSession } for camera preview.
     */
    private CameraCaptureSession mCaptureSession;
    private CameraDevice mCameraDevice;

    /**
     * {@link CameraDevice.StateCallback} is called when {@link CameraDevice} changes its state.
     */
    private final CameraDevice.StateCallback mStateCallback = new CameraDevice.StateCallback() {

        @Override
        public void onOpened(@NonNull CameraDevice cameraDevice) {
            // This method is called when the camera is opened.  We start camera preview here.
            mCameraOpenCloseLock.release();
            mCameraDevice = cameraDevice;
            createCameraPreviewSession();
        }

        @Override
        public void onDisconnected(@NonNull CameraDevice cameraDevice) {
            mCameraOpenCloseLock.release();
            cameraDevice.close();
            mCameraDevice = null;
        }

        @Override
        public void onError(@NonNull CameraDevice cameraDevice, int error) {
            mCameraOpenCloseLock.release();
            cameraDevice.close();
            mCameraDevice = null;
            Activity activity = getActivity();
            if (null != activity) {
                activity.finish();
            }
        }

    };

    private HandlerThread mBackgroundThread;
    private Handler mBackgroundHandler;

    /**
     * {@link CaptureRequest.Builder} for the camera preview
     */
    private CaptureRequest.Builder mPreviewRequestBuilder;

    /**
     * {@link CaptureRequest} generated by {@link #mPreviewRequestBuilder}
     */
    private CaptureRequest mPreviewRequest;

    /**
     * A {@link Semaphore} to prevent the app from exiting before closing the camera.
     */
    private Semaphore mCameraOpenCloseLock = new Semaphore(1);

    /**
     * Whether the current camera device supports Flash or not.
     */
    private boolean mFlashSupported;

    /**
     * Orientation of the camera sensor
     */
    private int mSensorOrientation;

    private EditText mETRemoteIp;
    private Button mBtnStart;
    private Button mBtnSwitch;
    private Button mBtnSM;
    private Button mBtnSB;

    private Size mPreviewSize;

    public static Camera2BasicFragment newInstance() {
        return new Camera2BasicFragment();
    }

    @Override
    public View onCreateView(LayoutInflater inflater, ViewGroup container, Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_camera2_basic, container, false);
    }

    @Override
    public void onViewCreated(final View view, Bundle savedInstanceState) {
        mGLSurfaceView = view.findViewById(R.id.gl_surface_view);
        //mPreviewView = view.findViewById(R.id.preview_view);
        mBtnStart = view.findViewById(R.id.btn_start);
        mBtnSwitch = view.findViewById(R.id.btn_switch);
        mBtnSB = view.findViewById(R.id.btn_sb);
        mBtnSM = view.findViewById(R.id.btn_sm);
        mETRemoteIp = view.findViewById(R.id.et_remote_ip);
        mBtnStart.setOnClickListener(this);
        mBtnSwitch.setOnClickListener(this);
        mBtnSB.setOnClickListener(this);
        mBtnSM.setOnClickListener(this);
    }

    @Override
    public void onActivityCreated(Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);
    }

    @Override
    public void onResume() {
        super.onResume();
        startBackgroundThread();

        // When the screen is turned off and turned back on, the SurfaceTexture is already available,
        // and "onSurfaceTextureAvailable" will not be called. In that case, we can open
        // a camera and start preview from here (otherwise, we wait until the surface is ready in the SurfaceTextureListener).
        if (mGLSurfaceView.isAvailable()) {
            openCamera();
        } else {
            mGLSurfaceView.setSurfaceListener(mySurfaceListener);
        }
    }

    @Override
    public void onPause() {
        if(mGLSurfaceView.isRecording()){
            mGLSurfaceView.stopRecord();
            mBtnStart.setText("开始");
        }
        closeCamera();
        stopBackgroundThread();
        super.onPause();
    }

    private void requestCameraPermission() {
        if (shouldShowRequestPermissionRationale(Manifest.permission.CAMERA)) {
            new ConfirmationDialog().show(getChildFragmentManager(), FRAGMENT_DIALOG);
        } else {
            requestPermissions(new String[]{ Manifest.permission.CAMERA, Manifest.permission.WRITE_EXTERNAL_STORAGE, Manifest.permission.RECORD_AUDIO },
                    REQUEST_PERMISSION);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        Log.d(TAG, "" + requestCode + "/" + permissions.length + " / " + grantResults.length);
        if (requestCode == REQUEST_PERMISSION) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                // do nothing
            } else {
                ErrorDialog.newInstance("请打开需要的权限").show(getChildFragmentManager(), FRAGMENT_DIALOG);
            }
        } else {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        }
    }

    Integer mCurFacing = CameraCharacteristics.LENS_FACING_FRONT;
    Integer mSBORSM = 0;
    CameraCharacteristics mCharacteristics = null;

    /**
     * Sets up member variables related to camera.
     * @param width  The width of available size for camera preview
     * @param height The height of available size for camera preview
     */
    @SuppressWarnings("SuspiciousNameCombination")
    private void setUpCameraOutputs() {
        Activity activity = getActivity();
        CameraManager manager = (CameraManager) activity.getSystemService(Context.CAMERA_SERVICE);
        try {
            for (String cameraId : manager.getCameraIdList()) {
                CameraCharacteristics characteristics = manager.getCameraCharacteristics(cameraId);

                // We don't use a front facing camera in this sample.
                Integer facing = characteristics.get(CameraCharacteristics.LENS_FACING);
                if (facing == null || facing != mCurFacing) {
                    continue;
                }

                StreamConfigurationMap map = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP);
                if (map == null) {
                    continue;
                }

                mCharacteristics = characteristics;

                mPreviewSize = getPreferredPreviewSize(map.getOutputSizes(SurfaceTexture.class),
                        mSBORSM==1?MAX_PREVIEW_WIDTH_SB:MAX_PREVIEW_WIDTH_SM,
                        mSBORSM==1?MAX_PREVIEW_HEIGHT_SB:MAX_PREVIEW_HEIGHT_SM,
                        0.5625f);

                SurfaceTexture texture = mGLSurfaceView.getSurfaceTexture();
                assert texture != null;

                // We configure the size of default buffer to be the size of camera preview we want.
                texture.setDefaultBufferSize(mPreviewSize.getWidth(), mPreviewSize.getHeight());

                // Find out if we need to swap dimension to get the preview size relative to sensor coordinate.
                int displayRotation = activity.getWindowManager().getDefaultDisplay().getRotation();
                // noinspection ConstantConditions
                mSensorOrientation = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION);
                Log.d(TAG, "DisplayRotation = " + displayRotation + " SensorOrientation = " + mSensorOrientation);

                boolean swappedDimensions = false;
                switch (displayRotation) {
                    case Surface.ROTATION_0:
                    case Surface.ROTATION_180:
                        if (mSensorOrientation == 90 || mSensorOrientation == 270) {
                            swappedDimensions = true;
                        }
                        break;
                    case Surface.ROTATION_90:
                    case Surface.ROTATION_270:
                        if (mSensorOrientation == 0 || mSensorOrientation == 180) {
                            swappedDimensions = true;
                        }
                        break;
                    default:
                        Log.e(TAG, "Display rotation is invalid: " + displayRotation);
                }

                // Check if the flash is supported.
                Boolean available = characteristics.get(CameraCharacteristics.FLASH_INFO_AVAILABLE);
                mFlashSupported = available == null ? false : available;

                mCameraId = cameraId;
                return;
            }

        } catch (CameraAccessException e) {
            e.printStackTrace();

        } catch (NullPointerException e) {
            // Currently an NPE is thrown when the Camera2API is used but not supported on the device this code runs.
            ErrorDialog.newInstance("设置Camera错误").show(getChildFragmentManager(), FRAGMENT_DIALOG);
        }
    }

    /**
     * Opens the camera specified by {@link Camera2BasicFragment#mCameraId}.
     */
    private void openCamera() {
        if (ContextCompat.checkSelfPermission(getActivity(), Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED
            || ContextCompat.checkSelfPermission(getActivity(), Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED
            || ContextCompat.checkSelfPermission(getActivity(), Manifest.permission. RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            requestCameraPermission();
            return;
        }
        setUpCameraOutputs(); // camera 相关配置
        Activity activity = getActivity();
        CameraManager manager = (CameraManager) activity.getSystemService(Context.CAMERA_SERVICE);
        try {
            if (!mCameraOpenCloseLock.tryAcquire(2500, TimeUnit.MILLISECONDS)) {
                throw new RuntimeException("Time out waiting to lock camera opening.");
            }
            manager.openCamera(mCameraId, mStateCallback, mBackgroundHandler);
        } catch (CameraAccessException e) {
            e.printStackTrace();
        } catch (InterruptedException e) {
            throw new RuntimeException("Interrupted while trying to lock camera opening.", e);
        }
    }

    /**
     * Closes the current {@link CameraDevice}.
     */
    private void closeCamera() {
        try {
            mCameraOpenCloseLock.acquire();
            if (null != mCaptureSession) {
                mCaptureSession.close();
                mCaptureSession = null;
            }
            if (null != mCameraDevice) {
                mCameraDevice.close();
                mCameraDevice = null;
            }
        } catch (InterruptedException e) {
            throw new RuntimeException("Interrupted while trying to lock camera closing.", e);
        } finally {
            mCameraOpenCloseLock.release();
        }
    }

    /**
     * Starts a background thread and its {@link Handler}.
     */
    private void startBackgroundThread() {
        mBackgroundThread = new HandlerThread("CameraBackground");
        mBackgroundThread.start();
        mBackgroundHandler = new Handler(mBackgroundThread.getLooper());
    }

    /**
     * Stops the background thread and its {@link Handler}.
     */
    private void stopBackgroundThread() {
        mBackgroundThread.quitSafely();
        try {
            mBackgroundThread.join();
            mBackgroundThread = null;
            mBackgroundHandler = null;
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
    }

    private int getFaceDetectMode(int[] faceDetectModes){
        if(faceDetectModes == null){
            return CaptureRequest.STATISTICS_FACE_DETECT_MODE_FULL;
        }else{
            return faceDetectModes[faceDetectModes.length-1];
        }
    }

    /**
     * Creates a new {@link CameraCaptureSession} for camera preview.
     */
    private void createCameraPreviewSession() {
        try {
            SurfaceTexture texture = mGLSurfaceView.getSurfaceTexture();
            assert texture != null;

            // We configure the size of default buffer to be the size of camera preview we want.
            //texture.setDefaultBufferSize(MAX_PREVIEW_WIDTH, MAX_PREVIEW_HEIGHT);

            // This is the output Surface we need to start preview.
            Surface surface = new Surface(texture);

            // We set up a CaptureRequest.Builder with the output Surface.
            mPreviewRequestBuilder = mCameraDevice.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW);
            mPreviewRequestBuilder.addTarget(surface);
            mPreviewRequestBuilder.set(CaptureRequest.CONTROL_MODE, CameraMetadata.CONTROL_MODE_AUTO);//自动曝光、白平衡、对焦
            mPreviewRequestBuilder.set(CaptureRequest.CONTROL_SCENE_MODE, CameraMetadata.CONTROL_SCENE_MODE_FACE_PRIORITY);
            mPreviewRequestBuilder.set(CaptureRequest.CONTROL_EFFECT_MODE, CameraMetadata.CONTROL_EFFECT_MODE_MONO);

            /*可用于判断是否支持人脸检测，以及支持到哪种程度
            int[] faceDetectModes = mCharacteristics.get(CameraCharacteristics.STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES);//支持的人脸检测模式
            int maxFaceCount = mCharacteristics.get(CameraCharacteristics.STATISTICS_INFO_MAX_FACE_COUNT);//支持的最大检测人脸数量
            mPreviewRequestBuilder.set(CaptureRequest.STATISTICS_FACE_DETECT_MODE,getFaceDetectMode(faceDetectModes));*/

            /*Range<Long> rangel = mCharacteristics.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE);
            long max = rangel.getUpper();
            long min = rangel.getLower();
            long se = ((20 * (max - min)) / 100 + min);
            mPreviewRequestBuilder.set(CaptureRequest.SENSOR_EXPOSURE_TIME, se);*/

            /*Range<Integer> range = mCharacteristics.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE);
            int minmin = range.getLower();
            int maxmax = range.getUpper();
            int iso = ((20 * (maxmax - minmin)) / 100 + minmin);
            mPreviewRequestBuilder.set(CaptureRequest.SENSOR_SENSITIVITY, iso);*/

            /*float minimumLens = mCharacteristics.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE);
            float num = (((float) 100) * minimumLens / 100);
            mPreviewRequestBuilder.set(CaptureRequest.LENS_FOCUS_DISTANCE, num);*/

            //曝光增益
            /*if(mSBORSM == 1) {
                Range<Integer> range1 = mCharacteristics.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE);
                int maxmax = range1.getUpper();
                int minmin = range1.getLower();
                float all = (-minmin) + maxmax;
                float time = all / 100.0f;
                int aetime = mCurFacing == CameraCharacteristics.LENS_FACING_FRONT ? 24 : 32;
                Log.d(TAG, "CONTROL_AE_COMPENSATION_RANGE: maxmax: " + maxmax + " minmin: " + minmin);
                int ae = mCurFacing == CameraCharacteristics.LENS_FACING_FRONT ? -12 : -8;//((aetime / time) - maxmax) > maxmax ? maxmax : ((aetime / time) - maxmax) < minmin ? minmin : ((aetime / time) - maxmax);
                mPreviewRequestBuilder.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, ae);
            }*/

            /*int maxZoom = mCharacteristics.get(CameraCharacteristics.SCALER_AVAILABLE_MAX_DIGITAL_ZOOM).intValue() / 3;
            //Log.d(TAG, "handleZoom: maxZoom: " + maxZoom);
            Rect rect = mCharacteristics.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE);
            float mZoom = 1.0f;
            //Log.d(TAG, "handleZoom: mZoom: " + mZoom);
            int minW = rect.width() / maxZoom;
            int minH = rect.height() / maxZoom;
            int difW = rect.width() - minW;
            int difH = rect.height() - minH;
            int cropW = (int)(((float)difW) * mZoom / 100);
            int cropH = (int)(((float)difH) * mZoom / 100);
            cropW -= cropW & 3;
            cropH -= cropH & 3;
            //Log.d(TAG, "handleZoom: cropW: " + cropW + ", cropH: " + cropH);
            Rect zoomRect = new Rect(cropW, cropH, rect.width() - cropW, rect.height() - cropH);
            mPreviewRequestBuilder.set(CaptureRequest.SCALER_CROP_REGION, zoomRect);*/

            // Here, we create a CameraCaptureSession for camera preview.
            mCameraDevice.createCaptureSession(Arrays.asList(surface),
                    new CameraCaptureSession.StateCallback() {
                        @Override
                        public void onConfigured(@NonNull CameraCaptureSession cameraCaptureSession) {
                            // The camera is already closed
                            if (null == mCameraDevice) {
                                return;
                            }

                            // When the session is ready, we start displaying the preview.
                            mCaptureSession = cameraCaptureSession;
                            try {
                                // Auto focus should be continuous for camera preview.
                                mPreviewRequestBuilder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO);
                                // Flash is automatically enabled when necessary.
                                setAutoFlash(mPreviewRequestBuilder);

                                // Finally, we start displaying the camera preview.
                                mPreviewRequest = mPreviewRequestBuilder.build();
                                mCaptureSession.setRepeatingRequest(mPreviewRequest, null, mBackgroundHandler);
                            } catch (CameraAccessException e) {
                                e.printStackTrace();
                            }
                        }

                        @Override
                        public void onConfigureFailed(@NonNull CameraCaptureSession cameraCaptureSession) {
                            Log.e(TAG, "onConfigureFailed");
                        }
                    }, null
            );
        } catch (CameraAccessException e) {
            e.printStackTrace();
        }
    }

    @Override
    public void onClick(View view) {
        switch (view.getId()) {
            case R.id.btn_start:
                if (!mGLSurfaceView.isRecording()) {
                    Log.d(TAG, "remote ip = " + mETRemoteIp.getText().toString());
                    mGLSurfaceView.startRecord(
                            mSBORSM==1?MAX_PREVIEW_WIDTH_SB:MAX_PREVIEW_WIDTH_SM,
                            mSBORSM==1?MAX_PREVIEW_HEIGHT_SB:MAX_PREVIEW_HEIGHT_SM,
                            mETRemoteIp.getText().toString());
                    ((Button)view).setText("停止");
                } else {
                    mGLSurfaceView.stopRecord();
                    ((Button)view).setText("开始");
                }
                break;
            case R.id.btn_switch:
                if(mCurFacing == CameraCharacteristics.LENS_FACING_FRONT){
                    mCurFacing = CameraCharacteristics.LENS_FACING_BACK;
                    ((Button)view).setText("前置");
                }else if(mCurFacing == CameraCharacteristics.LENS_FACING_BACK){
                    mCurFacing = CameraCharacteristics.LENS_FACING_FRONT;
                    ((Button)view).setText("后置");
                }

                if(mGLSurfaceView.isRecording()){
                    mGLSurfaceView.stopRecord();
                    mBtnStart.setText("开始");
                }

                closeCamera();
                mGLSurfaceView.switchcamera(mCurFacing == CameraCharacteristics.LENS_FACING_FRONT?0:1, mSBORSM);
                openCamera();
                break;
            case R.id.btn_sm:
                if(mSBORSM == 1) {
                    if(mGLSurfaceView.isRecording()){
                        mGLSurfaceView.stopRecord();
                        mBtnStart.setText("开始");
                    }
                    closeCamera();
                    mSBORSM = 0;
                    RelativeLayout.LayoutParams linearParams = (RelativeLayout.LayoutParams) mGLSurfaceView.getLayoutParams(); //取控件textView当前的布局参数
                    linearParams.width = dip2px(getActivity(), MAX_PREVIEW_WIDTH_SM);// 控件的宽强制设成30
                    linearParams.height = dip2px(getActivity(), MAX_PREVIEW_HEIGHT_SM);// 控件的高强制设成20
                    mGLSurfaceView.setLayoutParams(linearParams);
                    mGLSurfaceView.switchcamera(mCurFacing == CameraCharacteristics.LENS_FACING_FRONT?0:1, mSBORSM);
                    openCamera();
                }
                break;
            case R.id.btn_sb:
                if(mSBORSM == 0) {
                    if(mGLSurfaceView.isRecording()){
                        mGLSurfaceView.stopRecord();
                        mBtnStart.setText("开始");
                    }
                    closeCamera();
                    mSBORSM = 1;
                    RelativeLayout.LayoutParams linearParams = (RelativeLayout.LayoutParams) mGLSurfaceView.getLayoutParams(); //取控件textView当前的布局参数
                    linearParams.width = dip2px(getActivity(), MAX_PREVIEW_WIDTH_SB);// 控件的宽强制设成30
                    linearParams.height = dip2px(getActivity(), MAX_PREVIEW_HEIGHT_SB);// 控件的高强制设成20
                    mGLSurfaceView.setLayoutParams(linearParams);
                    mGLSurfaceView.switchcamera(mCurFacing == CameraCharacteristics.LENS_FACING_FRONT?0:1, mSBORSM);
                    openCamera();
                }
                break;

        }
    }

    private int dip2px(Context context,float dipValue){
        Resources r = context.getResources();
        return (int) TypedValue.applyDimension(
                TypedValue.COMPLEX_UNIT_DIP, dipValue, r.getDisplayMetrics());
    }


    private void setAutoFlash(CaptureRequest.Builder requestBuilder) {
        if (mFlashSupported) {
            requestBuilder.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_ON_AUTO_FLASH);
        }
    }

    private Size getPreferredPreviewSize(Size[] sizes, int width, int height, float ratio) {
        List<Size> collectorSizes = new ArrayList<>();
        for (Size option : sizes) {
            if (width > height) {
                if (option.getWidth() >= width && option.getHeight() >= height
                        && Float.compare(((float)height / (float)width), ratio) == 0) {
                    collectorSizes.add(option);
                }
            } else {
                if (option.getHeight() >= width && option.getWidth() >= height
                        && Float.compare(((float)width / (float)height), ratio) == 0) {
                    collectorSizes.add(option);
                }
            }
        }
        if (collectorSizes.size() > 0) {
            return Collections.min(collectorSizes, new Comparator<Size>() {
                @Override
                public int compare(Size s1, Size s2) {
                    return Long.signum(s1.getWidth() * s1.getHeight() - s2.getWidth() * s2.getHeight());
                }
            });
        }
        return sizes[0];
    }

    /**
     * Shows an error message dialog.
     */
    public static class ErrorDialog extends DialogFragment {

        private static final String ARG_MESSAGE = "message";

        public static ErrorDialog newInstance(String message) {
            ErrorDialog dialog = new ErrorDialog();
            Bundle args = new Bundle();
            args.putString(ARG_MESSAGE, message);
            dialog.setArguments(args);
            return dialog;
        }

        @NonNull
        @Override
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            final Activity activity = getActivity();
            return new AlertDialog.Builder(activity)
                    .setMessage(getArguments().getString(ARG_MESSAGE))
                    .setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialogInterface, int i) {
                            activity.finish();
                        }
                    })
                    .create();
        }

    }

    /**
     * Shows OK/Cancel confirmation dialog about camera permission.
     */
    public static class ConfirmationDialog extends DialogFragment {

        @NonNull
        @Override
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            final Fragment parent = getParentFragment();
            return new AlertDialog.Builder(getActivity())
                    .setMessage("请打开Camera权限")
                    .setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            parent.requestPermissions(new String[]{Manifest.permission.CAMERA},
                                    REQUEST_PERMISSION);
                        }
                    })
                    .setNegativeButton(android.R.string.cancel,
                            new DialogInterface.OnClickListener() {
                                @Override
                                public void onClick(DialogInterface dialog, int which) {
                                    Activity activity = parent.getActivity();
                                    if (activity != null) {
                                        activity.finish();
                                    }
                                }
                            })
                    .create();
        }
    }

}
