#pragma once
#include "pch.h"

struct ColorInfo {
	bool operator==(const ColorInfo& other) const = default;

	winrt::AdvancedColorKind kind = winrt::AdvancedColorKind::StandardDynamicRange;
	// HDR 模式下最大亮度缩放
	float maxLuminance = 1.0f;
	// HDR 模式下 SDR 内容亮度缩放
	float sdrWhiteLevel = 1.0f;
};
