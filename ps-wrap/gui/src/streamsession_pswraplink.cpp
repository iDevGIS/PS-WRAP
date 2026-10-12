// PS-WRAP Link: จอยของเพื่อนเข้า StreamSession — แยกไฟล์จาก streamsession.cpp เพื่อลด conflict ตอน merge upstream
#include "streamsession.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace {
std::mutex mix_mutex;   // กันตั้ง/ลบ mixer ระหว่าง thread เสียงกำลังเรียก
StreamSession::PsWrapAudioMixFn mix_fn = nullptr;
void *mix_ctx = nullptr;
}

namespace {
std::mutex rumble_mutex;
StreamSession::PsWrapRumbleFn rumble_fn = nullptr;
void *rumble_ctx = nullptr;
}

void StreamSession::PsWrapSetRumbleSink(PsWrapRumbleFn fn, void *ctx)
{
	std::lock_guard<std::mutex> lock(rumble_mutex);
	rumble_fn = fn;
	rumble_ctx = ctx;
}

void StreamSession::PsWrapEmitRumble(uint8_t left, uint8_t right, bool from_haptics)
{
	std::lock_guard<std::mutex> lock(rumble_mutex);
	if(rumble_fn)
		rumble_fn(rumble_ctx, left, right, from_haptics);
}

void StreamSession::PsWrapSetLinkRedirect(bool on)
{
	if(pswrap_link_redirect.exchange(on) == on)
		return;
	uint8_t led[3];
	memcpy(led, led_color, 3);
	for(auto controller : controllers)
	{
		if(on)
			controller->SetRumble(0, 0);   // หยุดที่ค้างอยู่ — ระหว่างนี้คำสั่งสั่นไปที่เพื่อน
		else
			controller->ChangeLEDColor(led);   // คืนสีล่าสุดที่เกมสั่ง (ระหว่างเพื่อนถือ ไม่ได้เปลี่ยนตาม)
	}
	if(!on)
		PsWrapEmitRumble(0, 0, false);
}

// haptics ของ DualSense (เสียง stereo 3 kHz) → แรงสั่นซ้าย/ขวา 0-255 แบบเดียวกับ Rumble Haptics (ค่าเฉลี่ยแอมพลิจูด × 2, ต่ำกว่า 100 = 0 เหมือน HAPTIC_RUMBLE_MIN_STRENGTH)
void StreamSession::PsWrapRedirectHaptics(const uint8_t *buf, size_t buf_size)
{
	const size_t frames = buf_size / (2 * sizeof(int16_t));
	if(!frames)
		return;
	uint64_t suml = 0, sumr = 0;
	for(size_t i = 0; i < frames; i++)
	{
		int16_t l, r;
		memcpy(&l, buf + i * 4, 2);
		memcpy(&r, buf + i * 4 + 2, 2);
		suml += (uint64_t)std::abs((int)l) * 2;
		sumr += (uint64_t)std::abs((int)r) * 2;
	}
	auto to8 = [](uint64_t v) -> uint8_t { return v < 100 ? 0 : (uint8_t)std::min<uint64_t>(255, v >> 8); };
	PsWrapEmitRumble(to8(suml / frames), to8(sumr / frames), true);
}

void StreamSession::PsWrapSetAudioMixer(PsWrapAudioMixFn fn, void *ctx)
{
	std::lock_guard<std::mutex> lock(mix_mutex);
	mix_fn = fn;
	mix_ctx = ctx;
}

void StreamSession::PsWrapMixExtraAudio(int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	std::lock_guard<std::mutex> lock(mix_mutex);
	if(mix_fn)
		mix_fn(mix_ctx, pcm, frames, channels, rate);
}

void StreamSession::PsWrapSetLinkState(const ChiakiControllerState &state)
{
	link_state = state;
	SendFeedbackState();
}
