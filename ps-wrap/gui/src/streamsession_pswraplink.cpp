// PS-WRAP Link: จอยของเพื่อนเข้า StreamSession — แยกไฟล์จาก streamsession.cpp เพื่อลด conflict ตอน merge upstream
#include "streamsession.h"

#include <mutex>

namespace {
std::mutex mix_mutex;   // กันตั้ง/ลบ mixer ระหว่าง thread เสียงกำลังเรียก
StreamSession::PsWrapAudioMixFn mix_fn = nullptr;
void *mix_ctx = nullptr;
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
