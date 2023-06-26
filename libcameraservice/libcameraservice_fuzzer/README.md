# Fuzzer for libcameraservice

## Plugin Design Considerations
The fuzzer plugin is designed based on the understanding of the
library and tries to achieve the following:

##### Maximize code coverage
The configuration parameters are not hardcoded, but instead selected based on
incoming data. This ensures more code paths are reached by the fuzzer.

libcameraservice supports the following parameters:
1. Camera Type (parameter name: `cameraType`)
2. Camera API Version (parameter name: `cameraAPIVersion`)
3. Event ID (parameter name: `eventId`)
4. Camera Sound Kind (parameter name: `soundKind`)
5. Shell Command (parameter name: `shellCommand`)

| Parameter| Valid Values| Configured Value|
|------------- |-------------| ----- |
| `cameraType` | 0. `CAMERA_TYPE_BACKWARD_COMPATIBLE` 1. `CAMERA_TYPE_ALL` | Value obtained from FuzzedDataProvider |
| `cameraAPIVersion` |  0. `API_VERSION_1` 1. `API_VERSION_2` | Value obtained from FuzzedDataProvider |
| `eventId` |  0. `EVENT_USER_SWITCHED` 1. `EVENT_NONE` | Value obtained from FuzzedDataProvider |
| `soundKind` |  0. `SOUND_SHUTTER` 1. `SOUND_RECORDING_START` 2. `SOUND_RECORDING_STOP`| Value obtained from FuzzedDataProvider |
| `shellCommand` |  0. `set-uid-state` 1. `reset-uid-state` 2. `get-uid-state` 3. `set-rotate-and-crop` 4. `get-rotate-and-crop` 5. `help`| Value obtained from FuzzedDataProvider |

This also ensures that the plugin is always deterministic for any given input.

##### Maximize utilization of input data
The plugin tolerates any kind of input (empty, huge,
malformed, etc) and doesn't `exit()` on any input and thereby increasing the
chance of identifying vulnerabilities.

## Build

This describes steps to build camera_service_fuzzer binary.

### Android

#### Steps to build
Build the fuzzer
```
  $ mm -j$(nproc) camera_service_fuzzer
```

#### Steps to run
Create a directory CORPUS_DIR
```
  $ adb shell mkdir CORPUS_DIR
```

To run on device
```
  $ adb sync data
  $ adb shell /data/fuzz/arm64/camera_service_fuzzer/camera_service_fuzzer CORPUS_DIR
```

## References:
 * http://llvm.org/docs/LibFuzzer.html
 * https://github.com/google/oss-fuzz
