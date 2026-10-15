#pragma once

#include <cstddef>

struct WireLayout
{
	size_t requestSize, requestAlign, requestPayloadOffset;
	size_t responseSize, responseAlign, responsePayloadOffset;
	size_t transformSize, transformTranslation, transformRotation, transformScale, transformLerp, transformQuash, transformSmooth;
	size_t alignmentSize, smoothingSize, statsRequestSize;
	size_t fingerSize, fingerPerFinger, fingerMask;
	size_t stickSize, stickRampSize, stickRelease, stickStrength, stickLocked, stickDelay;
	size_t statsSize, statsActive, statsReseeds, statsRawJitter, statsSmoothJitter;
	size_t poseSize, poseAlign;
	size_t poseFields[16];
	size_t augmentedSize, augmentedDeviceId, augmentedPose;
	unsigned version;
	int requestTypes[9];
	int responseTypes[4];
	unsigned fingerMaskAll;
	const char* pipeName;
	const char* shmemName;
};

WireLayout OverlayWireLayout();
WireLayout DriverWireLayout();
