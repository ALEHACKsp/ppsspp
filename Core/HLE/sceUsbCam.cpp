// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include <mutex>

#include "base/NativeApp.h"
#include "ppsspp_config.h"
#include "Common/ChunkFile.h"
#include "Core/HLE/HLE.h"
#include "Core/HLE/sceUsbCam.h"
#include "Core/HW/Camera.h"
#include "Core/MemMapHelpers.h"

#if defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Windows/CaptureDevice.h"
#endif

Camera::Config *config;

unsigned int videoBufferLength = 0;
unsigned int nextVideoFrame = 0;
uint8_t *videoBuffer;
std::mutex videoBufferMutex;

enum {
	VIDEO_BUFFER_SIZE = 40 * 1000,
};

void __UsbCamInit() {
	config       = new Camera::Config;
	config->mode = Camera::Mode::Unused;
	config->type = Camera::ConfigType::CfNone;
	videoBuffer  = new uint8_t[VIDEO_BUFFER_SIZE];
}

void __UsbCamDoState(PointerWrap &p) {
	auto s = p.Section("sceUsbCam", 0, 1);
	if (!s) {
		return;
	}

	p.Do(*config);
	if (config->mode == Camera::Mode::Video) { // stillImage? TBD
		Camera::startCapture();
	}
}

void __UsbCamShutdown() {
	Camera::stopCapture();
	delete[] videoBuffer;
	videoBuffer = nullptr;
	delete[] config;
	config = nullptr;
}

// TODO: Technically, we should store the videoBuffer into the savestate, if this
// module has been initialized.

static int getCameraResolution(Camera::ConfigType type, int *width, int *height) {
	if (type == Camera::ConfigType::CfStill || type == Camera::ConfigType::CfVideo) {
		switch(config->stillParam.resolution) {
			case 0: *width  = 160; *height = 120; return 0;
			case 1: *width  = 176; *height = 144; return 0;
			case 2: *width  = 320; *height = 240; return 0;
			case 3: *width  = 352; *height = 288; return 0;
			case 4: *width  = 640; *height = 480; return 0;
			case 5: *width  =1024; *height = 768; return 0;
			case 6: *width  =1280; *height = 960; return 0;
			case 7: *width  = 480; *height = 272; return 0;
			case 8: *width  = 360; *height = 272; return 0;
		}
	} else if (type == Camera::ConfigType::CfStillEx || type == Camera::ConfigType::CfVideoEx) {
		switch(config->stillExParam.resolution) {
			case 0: *width  = 160; *height = 120; return 0;
			case 1: *width  = 176; *height = 144; return 0;
			case 2: *width  = 320; *height = 240; return 0;
			case 3: *width  = 352; *height = 288; return 0;
			case 4: *width  = 360; *height = 272; return 0;
			case 5: *width  = 480; *height = 272; return 0;
			case 6: *width  = 640; *height = 480; return 0;
			case 7: *width  =1024; *height = 768; return 0;
			case 8: *width  =1280; *height = 960; return 0;
		}
	}
	*width  = 0; *height = 0; return 1;
}


static int sceUsbCamSetupMic(u32 paramAddr, u32 workareaAddr, int wasize) {
	INFO_LOG(HLE, "UNIMPL sceUsbCamSetupMic");
	if (Memory::IsValidRange(paramAddr, sizeof(PspUsbCamSetupMicParam))) {
		Memory::ReadStruct(paramAddr, &config->micParam);
	}
	return 0;
}

static int sceUsbCamStartMic() {
	INFO_LOG(HLE, "UNIMPL sceUsbCamStartMic");
	return 0;
}

static int sceUsbCamStopMic() {
	INFO_LOG(HLE, "UNIMPL sceUsbCamStopMic");
	return 0;
}

static int sceUsbCamReadMicBlocking(u32 bufAddr, u32 size) {
	INFO_LOG(HLE, "UNIMPL sceUsbCamReadMicBlocking: size: %d", size);
	for (unsigned int i = 0; i < size; i++) {
		if (Memory::IsValidAddress(bufAddr + i)) {
			Memory::Write_U8(i & 0xFF, bufAddr + i);
		}
	}
	hleEatMicro(1000000 / config->micParam.frequency * (size / 2));
	return size;
}

static int sceUsbCamSetupVideo(u32 paramAddr, u32 workareaAddr, int wasize) {
	if (Memory::IsValidRange(paramAddr, sizeof(PspUsbCamSetupVideoParam))) {
		Memory::ReadStruct(paramAddr, &config->videoParam);
	}
	config->type = Camera::ConfigType::CfVideo;
	return 0;
}

static int sceUsbCamSetupVideoEx(u32 paramAddr, u32 workareaAddr, int wasize) {
	if (Memory::IsValidRange(paramAddr, sizeof(PspUsbCamSetupVideoExParam))) {
		Memory::ReadStruct(paramAddr, &config->videoExParam);
	}
	config->type = Camera::ConfigType::CfVideoEx;
	return 0;
}

static int sceUsbCamStartVideo() {
	std::lock_guard<std::mutex> lock(videoBufferMutex);
	videoBufferLength = sizeof(sceUsbCamDummyImage);
	memset(videoBuffer, 0, VIDEO_BUFFER_SIZE);
	memcpy(videoBuffer, sceUsbCamDummyImage, sizeof(sceUsbCamDummyImage));
	Camera::startCapture();
	return 0;
}

static int sceUsbCamStopVideo() {
	Camera::stopCapture();
	return 0;
}

static int sceUsbCamReadVideoFrameBlocking(u32 bufAddr, u32 size) {
	std::lock_guard<std::mutex> lock(videoBufferMutex);
	u32 transferSize = std::min(videoBufferLength, size);
	if (Memory::IsValidRange(bufAddr, size)) {
		Memory::Memcpy(bufAddr, videoBuffer, transferSize);
	}
	return videoBufferLength;
}

static int sceUsbCamReadVideoFrame(u32 bufAddr, u32 size) {
	std::lock_guard<std::mutex> lock(videoBufferMutex);
	u32 transferSize = std::min(videoBufferLength, size);
	if (Memory::IsValidRange(bufAddr, size)) {
		Memory::Memcpy(bufAddr, videoBuffer, transferSize);
	}
	nextVideoFrame = videoBufferLength;
	return 0;
}

static int sceUsbCamPollReadVideoFrameEnd() {
	VERBOSE_LOG(HLE, "UNIMPL sceUsbCamPollReadVideoFrameEnd: %d", nextVideoFrame);
	return nextVideoFrame;
}

static int sceUsbCamSetupStill(u32 paramAddr) {
	INFO_LOG(HLE, "UNIMPL sceUsbCamSetupStill");
	if (Memory::IsValidRange(paramAddr, sizeof(PspUsbCamSetupStillParam))) {
		Memory::ReadStruct(paramAddr, &config->stillParam);
	}
	config->type = Camera::ConfigType::CfStill;
	return 0;
}

static int sceUsbCamSetupStillEx(u32 paramAddr) {
	INFO_LOG(HLE, "UNIMPL sceUsbCamSetupStillEx");
	if (Memory::IsValidRange(paramAddr, sizeof(PspUsbCamSetupStillExParam))) {
		Memory::ReadStruct(paramAddr, &config->stillExParam);
	}
	config->type = Camera::ConfigType::CfStillEx;
	return 0;
}

static int sceUsbCamAutoImageReverseSW(int rev) {
	INFO_LOG(HLE, "UNIMPL sceUsbCamAutoImageReverseSW");
	return 0;
}

const HLEFunction sceUsbCam[] =
{
	{ 0X03ED7A82, &WrapI_UUI<sceUsbCamSetupMic>,              "sceUsbCamSetupMic",                       'i', "xxi" },
	{ 0X2E930264, nullptr,                                    "sceUsbCamSetupMicEx",                     '?', "" },
	{ 0X82A64030, &WrapI_V<sceUsbCamStartMic>,                "sceUsbCamStartMic",                       'i', "" },
	{ 0X5145868A, &WrapI_V<sceUsbCamStopMic>,                 "sceUsbCamStopMic",                        'i', "" },
	{ 0X36636925, &WrapI_UU<sceUsbCamReadMicBlocking>,        "sceUsbCamReadMicBlocking",                'i', "xx" },
	{ 0X3DC0088E, nullptr,                                    "sceUsbCamReadMic",                        '?', "" },
	{ 0XB048A67D, nullptr,                                    "sceUsbCamWaitReadMicEnd",                 '?', "" },
	{ 0XF8847F60, nullptr,                                    "sceUsbCamPollReadMicEnd",                 '?', "" },
	{ 0X5778B452, nullptr,                                    "sceUsbCamGetMicDataLength",               '?', "" },
	{ 0X08AEE98A, nullptr,                                    "sceUsbCamSetMicGain",                     '?', "" },

	{ 0X17F7B2FB, &WrapI_UUI<sceUsbCamSetupVideo>,            "sceUsbCamSetupVideo",                     'i', "xxi" },
	{ 0XCFE9E999, &WrapI_UUI<sceUsbCamSetupVideoEx>,          "sceUsbCamSetupVideoEx",                   'i', "xxi" },
	{ 0X574A8C3F, &WrapI_V<sceUsbCamStartVideo>,              "sceUsbCamStartVideo",                     'i', "" },
	{ 0X6CF32CB9, &WrapI_V<sceUsbCamStopVideo>,               "sceUsbCamStopVideo",                      'i', "" },
	{ 0X7DAC0C71, &WrapI_UU<sceUsbCamReadVideoFrameBlocking>, "sceUsbCamReadVideoFrameBlocking",         'i', "xx" },
	{ 0X99D86281, &WrapI_UU<sceUsbCamReadVideoFrame>,         "sceUsbCamReadVideoFrame",                 'i', "xx" },
	{ 0XF90B2293, nullptr,                                    "sceUsbCamWaitReadVideoFrameEnd",          '?', "" },
	{ 0X41E73E95, &WrapI_V<sceUsbCamPollReadVideoFrameEnd>,   "sceUsbCamPollReadVideoFrameEnd",          'i', "" },
	{ 0XDF9D0C92, nullptr,                                    "sceUsbCamGetReadVideoFrameSize",          '?', "" },

	{ 0X3F0CF289, &WrapI_U<sceUsbCamSetupStill>,              "sceUsbCamSetupStill",                     'i', "x" },
	{ 0X0A41A298, &WrapI_U<sceUsbCamSetupStillEx>,            "sceUsbCamSetupStillEx",                   'i', "x" },
	{ 0X61BE5CAC, nullptr,                                    "sceUsbCamStillInputBlocking",             '?', "" },
	{ 0XFB0A6C5D, nullptr,                                    "sceUsbCamStillInput",                     '?', "" },
	{ 0X7563AFA1, nullptr,                                    "sceUsbCamStillWaitInputEnd",              '?', "" },
	{ 0X1A46CFE7, nullptr,                                    "sceUsbCamStillPollInputEnd",              '?', "" },
	{ 0XA720937C, nullptr,                                    "sceUsbCamStillCancelInput",               '?', "" },
	{ 0XE5959C36, nullptr,                                    "sceUsbCamStillGetInputLength",            '?', "" },

	{ 0XF93C4669, &WrapI_I<sceUsbCamAutoImageReverseSW>,      "sceUsbCamAutoImageReverseSW",             'i', "i" },
	{ 0X11A1F128, nullptr,                                    "sceUsbCamGetAutoImageReverseState",       '?', "" },
	{ 0X4C34F553, nullptr,                                    "sceUsbCamGetLensDirection",               '?', "" },

	{ 0X383E9FA8, nullptr,                                    "sceUsbCamGetSaturation",                  '?', "" },
	{ 0X6E205974, nullptr,                                    "sceUsbCamSetSaturation",                  '?', "" },
	{ 0X70F522C5, nullptr,                                    "sceUsbCamGetBrightness",                  '?', "" },
	{ 0X4F3D84D5, nullptr,                                    "sceUsbCamSetBrightness",                  '?', "" },
	{ 0XA063A957, nullptr,                                    "sceUsbCamGetContrast",                    '?', "" },
	{ 0X09C26C7E, nullptr,                                    "sceUsbCamSetContrast",                    '?', "" },
	{ 0XFDB68C23, nullptr,                                    "sceUsbCamGetSharpness",                   '?', "" },
	{ 0X622F83CC, nullptr,                                    "sceUsbCamSetSharpness",                   '?', "" },
	{ 0X994471E0, nullptr,                                    "sceUsbCamGetImageEffectMode",             '?', "" },
	{ 0XD4876173, nullptr,                                    "sceUsbCamSetImageEffectMode",             '?', "" },
	{ 0X2BCD50C0, nullptr,                                    "sceUsbCamGetEvLevel",                     '?', "" },
	{ 0X1D686870, nullptr,                                    "sceUsbCamSetEvLevel",                     '?', "" },
	{ 0XD5279339, nullptr,                                    "sceUsbCamGetReverseMode",                 '?', "" },
	{ 0X951BEDF5, nullptr,                                    "sceUsbCamSetReverseMode",                 '?', "" },
	{ 0X9E8AAF8D, nullptr,                                    "sceUsbCamGetZoom",                        '?', "" },
	{ 0XC484901F, nullptr,                                    "sceUsbCamSetZoom",                        '?', "" },
	{ 0XAA7D94BA, nullptr,                                    "sceUsbCamGetAntiFlicker",                 '?', "" },
	{ 0X6784E6A8, nullptr,                                    "sceUsbCamSetAntiFlicker",                 '?', "" },

	{ 0XD293A100, nullptr,                                    "sceUsbCamRegisterLensRotationCallback",   '?', "" },
	{ 0X41EE8797, nullptr,                                    "sceUsbCamUnregisterLensRotationCallback", '?', "" },
};

void Register_sceUsbCam()
{
	RegisterModule("sceUsbCam", ARRAY_SIZE(sceUsbCam), sceUsbCam);
}

std::vector<std::string> Camera::getDeviceList() {
	#if PPSSPP_PLATFORM(LINUX) && !PPSSPP_PLATFORM(ANDROID)
		return __v4l_getDeviceList();
	#elif defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
		if (winCamera) {
			return winCamera->getDeviceList();
		}
	#endif
	return std::vector<std::string>();
}

int Camera::startCapture() {
	int width, height;
	getCameraResolution(config->type, &width, &height);
	INFO_LOG(HLE, "%s resolution: %dx%d", __FUNCTION__, width, height);

	config->mode = Camera::Mode::Video;
	#if PPSSPP_PLATFORM(ANDROID)
		System_SendMessage("camera_command", "startVideo");
	#elif PPSSPP_PLATFORM(LINUX)
		__v4l_startCapture(width, height);
	#elif defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
		if (winCamera) {
			if (winCamera->isShutDown()) {
				delete winCamera;
				winCamera = new WindowsCaptureDevice(CAPTUREDEVIDE_TYPE::VIDEO);
				winCamera->sendMessage({ CAPTUREDEVIDE_COMMAND::INITIALIZE, nullptr });
			}
			winCamera->sendMessage({ CAPTUREDEVIDE_COMMAND::START, nullptr });
		}
	#else
		ERROR_LOG(HLE, "%s not implemented", __FUNCTION__);
	#endif
	return 0;
}

int Camera::stopCapture() {
	INFO_LOG(HLE, "%s", __FUNCTION__);
	#if PPSSPP_PLATFORM(ANDROID)
		System_SendMessage("camera_command", "stopVideo");
	#elif PPSSPP_PLATFORM(LINUX)
		__v4l_stopCapture();
	#elif defined(_WIN32) && !PPSSPP_PLATFORM(UWP)
		if (winCamera) {
			winCamera->sendMessage({ CAPTUREDEVIDE_COMMAND::STOP, nullptr });
			winCamera->sendMessage({ CAPTUREDEVIDE_COMMAND::SHUTDOWN, nullptr });
		}
	#else
		ERROR_LOG(HLE, "%s not implemented", __FUNCTION__);
	#endif
	config->mode = Camera::Mode::Unused;
	return 0;
}

void Camera::pushCameraImage(long long length, unsigned char* image) {
	std::lock_guard<std::mutex> lock(videoBufferMutex);
	if (!videoBuffer) {
		return;
	}
	memset(videoBuffer, 0, VIDEO_BUFFER_SIZE);
	if (length > VIDEO_BUFFER_SIZE) {
		videoBufferLength = 0;
		ERROR_LOG(HLE, "pushCameraImage: length error: %lld > %d", length, VIDEO_BUFFER_SIZE);
	} else {
		videoBufferLength = length;
		memcpy(videoBuffer, image, length);
	}
}
