/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "modules/video_capture/linux/device_info_v4l2.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
// v4l includes
#if defined(__OpenBSD__)
#include <sys/videoio.h>
#else
#include <linux/videodev2.h>
#endif

#include <algorithm>
#include <vector>

#include "modules/video_capture/video_capture.h"
#include "modules/video_capture/video_capture_defines.h"
#include "modules/video_capture/video_capture_impl.h"
#include "rtc_base/logging.h"

// These defines are here to support building on kernel 3.16 which some
// downstream projects, e.g. Firefox, use.
// TODO(apehrson): Remove them and their undefs when no longer needed.
#ifndef V4L2_PIX_FMT_ABGR32
#define ABGR32_OVERRIDE 1
#define V4L2_PIX_FMT_ABGR32 v4l2_fourcc('A', 'R', '2', '4')
#endif

#ifndef V4L2_PIX_FMT_ARGB32
#define ARGB32_OVERRIDE 1
#define V4L2_PIX_FMT_ARGB32 v4l2_fourcc('B', 'A', '2', '4')
#endif

#ifndef V4L2_PIX_FMT_RGBA32
#define RGBA32_OVERRIDE 1
#define V4L2_PIX_FMT_RGBA32 v4l2_fourcc('A', 'B', '2', '4')
#endif

namespace webrtc {
namespace videocapturemodule {
DeviceInfoV4l2::DeviceInfoV4l2() : DeviceInfoImpl() {}

int32_t DeviceInfoV4l2::Init() {
  return 0;
}

DeviceInfoV4l2::~DeviceInfoV4l2() {}

uint32_t DeviceInfoV4l2::NumberOfDevices() {
  uint32_t count = 0;
  char device[20];
  int fd = -1;
  struct v4l2_capability cap;

  /* detect /dev/video [0-63]VideoCaptureModule entries */
  for (int n = 0; n < 64; n++) {
    snprintf(device, sizeof(device), "/dev/video%d", n);
    if ((fd = open(device, O_RDONLY)) != -1) {
      // query device capabilities and make sure this is a video capture device
      if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0 ||
          !(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
        close(fd);
        continue;
      }

      close(fd);
      count++;
    }
  }

  return count;
}

int32_t DeviceInfoV4l2::GetDeviceName(uint32_t deviceNumber,
                                      char* deviceNameUTF8,
                                      uint32_t deviceNameLength,
                                      char* deviceUniqueIdUTF8,
                                      uint32_t deviceUniqueIdUTF8Length,
                                      char* /*productUniqueIdUTF8*/,
                                      uint32_t /*productUniqueIdUTF8Length*/) {
  // Travel through /dev/video [0-63]
  uint32_t count = 0;
  char device[20];
  int fd = -1;
  bool found = false;
  struct v4l2_capability cap;
  for (int n = 0; n < 64; n++) {
    snprintf(device, sizeof(device), "/dev/video%d", n);
    if ((fd = open(device, O_RDONLY)) != -1) {
      // query device capabilities and make sure this is a video capture device
      if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0 ||
          !(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
        close(fd);
        continue;
      }
      if (count == deviceNumber) {
        // Found the device
        found = true;
        break;
      } else {
        close(fd);
        count++;
      }
    }
  }

  if (!found)
    return -1;

  // query device capabilities
  if (ioctl(fd, VIDIOC_QUERYCAP, &cap) < 0) {
    RTC_LOG(LS_INFO) << "error in querying the device capability for device "
                     << device << ". errno = " << errno;
    close(fd);
    return -1;
  }

  close(fd);

  char cameraName[64];
  memset(deviceNameUTF8, 0, deviceNameLength);
  memcpy(cameraName, cap.card, sizeof(cap.card));

  if (deviceNameLength > strlen(cameraName)) {
    memcpy(deviceNameUTF8, cameraName, strlen(cameraName));
  } else {
    RTC_LOG(LS_INFO) << "buffer passed is too small";
    return -1;
  }

  if (cap.bus_info[0] != 0) {  // may not available in all drivers
    // copy device id
    size_t len = strlen(reinterpret_cast<const char*>(cap.bus_info));
    if (deviceUniqueIdUTF8Length > len) {
      memset(deviceUniqueIdUTF8, 0, deviceUniqueIdUTF8Length);
      memcpy(deviceUniqueIdUTF8, cap.bus_info, len);
    } else {
      RTC_LOG(LS_INFO) << "buffer passed is too small";
      return -1;
    }
  }

  return 0;
}

int32_t DeviceInfoV4l2::CreateCapabilityMap(const char* deviceUniqueIdUTF8) {
  int fd;
  char device[32];
  bool found = false;

  const int32_t deviceUniqueIdUTF8Length = strlen(deviceUniqueIdUTF8);
  if (deviceUniqueIdUTF8Length >= kVideoCaptureUniqueNameLength) {
    RTC_LOG(LS_INFO) << "Device name too long";
    return -1;
  }
  RTC_LOG(LS_INFO) << "CreateCapabilityMap called for device "
                   << deviceUniqueIdUTF8;

  /* detect /dev/video [0-63] entries */
  for (int n = 0; n < 64; ++n) {
    snprintf(device, sizeof(device), "/dev/video%d", n);
    fd = open(device, O_RDONLY);
    if (fd == -1)
      continue;

    // query device capabilities
    struct v4l2_capability cap;
    if (ioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
      // skip devices without video capture capability
      if (!(cap.device_caps & V4L2_CAP_VIDEO_CAPTURE)) {
        continue;
      }

      if (cap.bus_info[0] != 0) {
        if (strncmp(reinterpret_cast<const char*>(cap.bus_info),
                    deviceUniqueIdUTF8,
                    strlen(deviceUniqueIdUTF8)) == 0) {  // match with device id
          found = true;
          break;  // fd matches with device unique id supplied
        }
      } else {  // match for device name
        if (IsDeviceNameMatches(reinterpret_cast<const char*>(cap.card),
                                deviceUniqueIdUTF8)) {
          found = true;
          break;
        }
      }
    }
    close(fd);  // close since this is not the matching device
  }

  if (!found) {
    RTC_LOG(LS_INFO) << "no matching device found";
    return -1;
  }

  // now fd will point to the matching device
  // reset old capability list.
  _captureCapabilities.clear();

  int size = FillCapabilities(fd);
  close(fd);

  // Store the new used device name
  _lastUsedDeviceNameLength = deviceUniqueIdUTF8Length;
  _lastUsedDeviceName = reinterpret_cast<char*>(
      realloc(_lastUsedDeviceName, _lastUsedDeviceNameLength + 1));
  memcpy(_lastUsedDeviceName, deviceUniqueIdUTF8,
         _lastUsedDeviceNameLength + 1);

  RTC_LOG(LS_INFO) << "CreateCapabilityMap " << _captureCapabilities.size();

  return size;
}

int32_t DeviceInfoV4l2::DisplayCaptureSettingsDialogBox(
    const char* /*deviceUniqueIdUTF8*/,
    const char* /*dialogTitleUTF8*/,
    void* /*parentWindow*/,
    uint32_t /*positionX*/,
    uint32_t /*positionY*/) {
  return -1;
}

bool DeviceInfoV4l2::IsDeviceNameMatches(const char* name,
                                         const char* deviceUniqueIdUTF8) {
  if (strncmp(deviceUniqueIdUTF8, name, strlen(name)) == 0)
    return true;
  return false;
}

int32_t DeviceInfoV4l2::FillCapabilities(int fd) {
  // set image format
  struct v4l2_format video_fmt;
  memset(&video_fmt, 0, sizeof(struct v4l2_format));

  video_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  video_fmt.fmt.pix.sizeimage = 0;

  unsigned int videoFormats[] = {
      V4L2_PIX_FMT_MJPEG,  V4L2_PIX_FMT_JPEG,   V4L2_PIX_FMT_YUV420,
      V4L2_PIX_FMT_YVU420, V4L2_PIX_FMT_YUYV,   V4L2_PIX_FMT_UYVY,
      V4L2_PIX_FMT_NV12,   V4L2_PIX_FMT_BGR24,  V4L2_PIX_FMT_RGB24,
      V4L2_PIX_FMT_RGB565, V4L2_PIX_FMT_ABGR32, V4L2_PIX_FMT_ARGB32,
      V4L2_PIX_FMT_RGBA32, V4L2_PIX_FMT_BGR32,  V4L2_PIX_FMT_RGB32,
  };
  constexpr int totalFmts = sizeof(videoFormats) / sizeof(unsigned int);

  int sizes = 13;
  unsigned int size[][2] = {{128, 96},   {160, 120},  {176, 144},  {320, 240},
                            {352, 288},  {640, 480},  {704, 576},  {800, 600},
                            {960, 720},  {1280, 720}, {1024, 768}, {1440, 1080},
                            {1920, 1080}};

  // Query which pixel formats the device actually offers before probing.
  // VIDIOC_ENUM_FMT and VIDIOC_ENUM_FRAMESIZES are answered from in-kernel
  // descriptors without any USB traffic on uvcvideo, while every
  // VIDIOC_TRY_FMT can trigger a full UVC probe negotiation with the device.
  // Some webcams have firmware that answers those probe requests slowly
  // (measured ~33 ms per TRY_FMT on a "Hy-UXGA(B5M2)" camera), so the blind
  // `totalFmts * sizes` (195 requests) probe matrix below could take ~7
  // seconds and break callers that have a short camera start timeout. By
  // enumerating first we can skip every combination that cannot produce a
  // new capability anyway, and avoid reporting bogus capabilities: for a
  // pixel format the device does not support the kernel answers TRY_FMT
  // with the default format at the closest supported size, which the code
  // below would otherwise store as a capability of the requested (but
  // unsupported) format.
  std::vector<unsigned int> offeredFormats;
  struct v4l2_fmtdesc fmtdesc;
  memset(&fmtdesc, 0, sizeof(fmtdesc));
  fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  while (ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
    offeredFormats.push_back(fmtdesc.pixelformat);
    fmtdesc.index++;
  }

  for (int fmts = 0; fmts < totalFmts; fmts++) {
    // If the device did not report any formats (ENUM_FMT unsupported) fall
    // back to probing all formats as before. Otherwise only probe formats
    // the device offers.
    const bool offered =
        offeredFormats.empty() ||
        std::find(offeredFormats.begin(), offeredFormats.end(),
                  videoFormats[fmts]) != offeredFormats.end();

    // For discrete frame sizes only probe the sizes the device reports,
    // since TRY_FMT answers with the closest supported frame and a size
    // that is not offered can never be an exact match.
    bool discreteSizes = false;
    std::vector<std::pair<unsigned int, unsigned int>> offeredSizes;
    if (offered && !offeredFormats.empty()) {
      struct v4l2_frmsizeenum frmsize;
      memset(&frmsize, 0, sizeof(frmsize));
      frmsize.pixel_format = videoFormats[fmts];
      while (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &frmsize) == 0) {
        if (frmsize.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
          offeredSizes.emplace_back(frmsize.discrete.width,
                                    frmsize.discrete.height);
          discreteSizes = true;
        } else {
          // Stepwise or continuous sizes: any requested size can be
          // supported, keep probing them all.
          discreteSizes = false;
          offeredSizes.clear();
          break;
        }
        frmsize.index++;
      }
    }

    for (int i = 0; i < sizes; i++) {
      if (!offered) {
        continue;
      }
      if (discreteSizes &&
          std::find(offeredSizes.begin(), offeredSizes.end(),
                    std::make_pair(size[i][0], size[i][1])) ==
              offeredSizes.end()) {
        continue;
      }
      video_fmt.fmt.pix.pixelformat = videoFormats[fmts];
      video_fmt.fmt.pix.width = size[i][0];
      video_fmt.fmt.pix.height = size[i][1];

      if (ioctl(fd, VIDIOC_TRY_FMT, &video_fmt) >= 0) {
        // The kernel overwrites `pixelformat` with the format actually
        // used, which for an unsupported format is the device's default
        // format. Skip those answers so we don't report capabilities for
        // formats the device does not have.
        if (video_fmt.fmt.pix.pixelformat != videoFormats[fmts]) {
          continue;
        }
        if ((video_fmt.fmt.pix.width == size[i][0]) &&
            (video_fmt.fmt.pix.height == size[i][1])) {
          VideoCaptureCapability cap;
          cap.width = video_fmt.fmt.pix.width;
          cap.height = video_fmt.fmt.pix.height;
          if (videoFormats[fmts] == V4L2_PIX_FMT_YUYV) {
            cap.videoType = VideoType::kYUY2;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_YUV420) {
            cap.videoType = VideoType::kI420;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_YVU420) {
            cap.videoType = VideoType::kYV12;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_MJPEG ||
                     videoFormats[fmts] == V4L2_PIX_FMT_JPEG) {
            cap.videoType = VideoType::kMJPEG;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_UYVY) {
            cap.videoType = VideoType::kUYVY;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_NV12) {
            cap.videoType = VideoType::kNV12;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_BGR24) {
            // NB that for RGB formats, `VideoType` follows naming conventions
            // of libyuv[1], where e.g. the format for FOURCC "ARGB" stores
            // pixels in BGRA order in memory. V4L2[2] on the other hand names
            // its formats based on the order of the RGB components as stored in
            // memory. Applies to all RGB formats below.
            // [1]https://chromium.googlesource.com/libyuv/libyuv/+/refs/heads/main/docs/formats.md#the-argb-fourcc
            // [2]https://www.kernel.org/doc/html/v6.2/userspace-api/media/v4l/pixfmt-rgb.html#bits-per-component
            cap.videoType = VideoType::kRGB24;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_RGB24) {
            cap.videoType = VideoType::kBGR24;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_RGB565) {
            cap.videoType = VideoType::kRGB565;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_ABGR32) {
            cap.videoType = VideoType::kARGB;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_ARGB32) {
            cap.videoType = VideoType::kBGRA;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_BGR32) {
            cap.videoType = VideoType::kARGB;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_RGB32) {
            cap.videoType = VideoType::kBGRA;
          } else if (videoFormats[fmts] == V4L2_PIX_FMT_RGBA32) {
            cap.videoType = VideoType::kABGR;
          } else {
            RTC_DCHECK_NOTREACHED();
          }

          // get fps of current camera mode
          // V4l2 does not have a stable method of knowing so we just guess.
          if (cap.width >= 800 && cap.videoType != VideoType::kMJPEG) {
            cap.maxFPS = 15;
          } else {
            cap.maxFPS = 30;
          }

          _captureCapabilities.push_back(cap);
          RTC_LOG(LS_VERBOSE) << "Camera capability, width:" << cap.width
                              << " height:" << cap.height
                              << " type:" << static_cast<int32_t>(cap.videoType)
                              << " fps:" << cap.maxFPS;
        }
      }
    }
  }

  RTC_LOG(LS_INFO) << "CreateCapabilityMap " << _captureCapabilities.size();
  return _captureCapabilities.size();
}

}  // namespace videocapturemodule
}  // namespace webrtc

#ifdef ABGR32_OVERRIDE
#undef ABGR32_OVERRIDE
#undef V4L2_PIX_FMT_ABGR32
#endif

#ifdef ARGB32_OVERRIDE
#undef ARGB32_OVERRIDE
#undef V4L2_PIX_FMT_ARGB32
#endif

#ifdef RGBA32_OVERRIDE
#undef RGBA32_OVERRIDE
#undef V4L2_PIX_FMT_RGBA32
#endif
