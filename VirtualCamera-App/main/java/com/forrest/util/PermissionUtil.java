package com.forrest.util;

import android.app.Activity;
import android.app.AlertDialog;
import android.app.Dialog;
import android.app.DialogFragment;
import android.content.DialogInterface;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;

/**
 * Created by yuzh on 2017/8/29.
 * 权限操作相关的工具类
 */
public class PermissionUtil {

    //Tells whether permissions are granted to the app.
    public static boolean hasPermissionsGranted(Activity activity, String[] permissions) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            for (String permission : permissions) {
                if (activity.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED) {
                    return false;
                }
            }
        }
        return true;
    }

    public static void requestStoragePermission(Activity activity, String[] permissions, int requestCode, String message) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (shouldShowRationale(activity, permissions)) {
                PermissionConfirmationDialog.newInstance()
                        .setActivity(activity)
                        .setMessage(message)
                        .setPermissions(permissions, requestCode)
                        .show(activity.getFragmentManager(), "dialog");
            } else {
                activity.requestPermissions(permissions, requestCode);
            }
        }
    }

    /**
     * Gets whether you should show UI with rationale for requesting the permissions.
     * @return True if the UI should be shown.
     */
    public static boolean shouldShowRationale(Activity activity, String[] permissions) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            for (String permission : permissions) {
                if (activity.shouldShowRequestPermissionRationale(permission)) {
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * A dialog that explains about the necessary permissions.
     */
    public static class PermissionConfirmationDialog extends DialogFragment {
        private Activity activity;
        private String[] permissions;
        private String message;
        private int requestCode;
        public static PermissionConfirmationDialog newInstance() {
            return new PermissionConfirmationDialog();
        }

        public PermissionConfirmationDialog setActivity(Activity activity) {
            this.activity = activity;
            return this;
        }

        public PermissionConfirmationDialog setMessage(String message) {
            this.message = message;
            return this;
        }

        public PermissionConfirmationDialog setPermissions(String[] permissions, int requestCode) {
            this.permissions = permissions;
            this.requestCode = requestCode;
            return this;
        }

        @Override
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            return new AlertDialog.Builder(getActivity())
                    .setMessage(message)
                    .setPositiveButton(android.R.string.ok, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                                activity.requestPermissions(permissions, requestCode);
                            }
                        }
                    })
                    .setNegativeButton(android.R.string.cancel, new DialogInterface.OnClickListener() {
                        @Override
                        public void onClick(DialogInterface dialog, int which) {
                            activity.finish();
                        }
                    })
                    .create();
        }

    }
}
