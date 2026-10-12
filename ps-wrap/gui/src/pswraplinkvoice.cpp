// PS-WRAP Link: เสียงไมค์ของเพื่อน (ดู pswraplinkvoice.h) · ผสมเข้าเสียงเกมใน StreamSession (ไม่เปิดอุปกรณ์เสียงของตัวเอง)
#include <pswraplinkvoice.h>

#include <QDateTime>
#include <QLoggingCategory>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

Q_DECLARE_LOGGING_CATEGORY(pswrapLink)

namespace {

constexpr int kRate = 48000;
constexpr int kCh = 2;
constexpr size_t kPrebuffer = kRate * kCh * 60 / 1000;    // เริ่มเล่นเมื่อมี 60 ms (กันกระตุกจากเน็ตแกว่ง)
constexpr size_t kMaxFifo = kRate * kCh * 250 / 1000;     // ค้างเกิน 250 ms = ช้าไปแล้ว → ตัดเหลือ 100 ms
constexpr size_t kTrimTo = kRate * kCh * 100 / 1000;
constexpr float kSpeakLevel = 0.04f;                       // peak เกินนี้ = กำลังพูด (หลัง AGC/NS ของเบราว์เซอร์)
constexpr qint64 kSpeakHoldMs = 300;

struct Voice
{
	AVCodecContext *dec = nullptr;
	AVPacket *pkt = nullptr;
	AVFrame *frame = nullptr;
	std::vector<float> fifo;   // interleaved stereo · อ่านจากหัว (read) — กันย้ายหน่วยความจำบ่อย
	size_t read = 0;
	bool playing = false;
	bool muted = false;
	bool has_ts = false;
	uint32_t last_ts = 0;
	qint64 loud_ms = 0;

	~Voice()
	{
		av_frame_free(&frame);
		av_packet_free(&pkt);
		if (dec)
			avcodec_free_context(&dec);
	}
	size_t available() const { return fifo.size() - read; }
	void compact()
	{
		if (read > 0 && read * 2 > fifo.size()) {
			fifo.erase(fifo.begin(), fifo.begin() + static_cast<std::ptrdiff_t>(read));
			read = 0;
		}
	}
};

bool openDecoder(Voice &v)
{
	// libopus ก่อน (ตัวเดียวกับที่ใช้ encode) · ไม่มีก็ใช้ตัวถอดในตัวของ FFmpeg
	const AVCodec *codec = avcodec_find_decoder_by_name("libopus");
	if (!codec)
		codec = avcodec_find_decoder(AV_CODEC_ID_OPUS);
	if (!codec)
		return false;
	v.dec = avcodec_alloc_context3(codec);
	v.dec->sample_rate = kRate;
	av_channel_layout_default(&v.dec->ch_layout, kCh);
	v.dec->request_sample_fmt = AV_SAMPLE_FMT_FLT;
	if (avcodec_open2(v.dec, codec, nullptr) < 0)
		return false;
	v.pkt = av_packet_alloc();
	v.frame = av_frame_alloc();
	return v.pkt && v.frame;
}

// เฟรมที่ถอดได้ → float stereo interleaved ต่อท้าย fifo · คืน peak
float appendFrame(Voice &v, const AVFrame *f)
{
	const int n = f->nb_samples;
	const int ch = f->ch_layout.nb_channels;
	if (n <= 0 || ch <= 0)
		return 0.0f;
	auto sample = [&](int c, int i) -> float {
		const int cc = std::min(c, ch - 1);   // โมโน → ซ้ายขวาเท่ากัน
		switch (f->format) {
		case AV_SAMPLE_FMT_FLT: return reinterpret_cast<const float *>(f->data[0])[i * ch + cc];
		case AV_SAMPLE_FMT_FLTP: return reinterpret_cast<const float *>(f->data[cc])[i];
		case AV_SAMPLE_FMT_S16: return reinterpret_cast<const int16_t *>(f->data[0])[i * ch + cc] / 32768.0f;
		case AV_SAMPLE_FMT_S16P: return reinterpret_cast<const int16_t *>(f->data[cc])[i] / 32768.0f;
		default: return 0.0f;
		}
	};
	float peak = 0.0f;
	v.fifo.reserve(v.fifo.size() + static_cast<size_t>(n) * kCh);
	for (int i = 0; i < n; i++)
		for (int c = 0; c < kCh; c++) {
			const float s = sample(c, i);
			peak = std::max(peak, std::fabs(s));
			v.fifo.push_back(s);
		}
	return peak;
}

} // namespace

struct PsWrapLinkVoice::Impl
{
	mutable std::mutex m;
	std::map<QString, std::unique_ptr<Voice>> voices;
};

PsWrapLinkVoice::PsWrapLinkVoice() : d(std::make_unique<Impl>()) {}

PsWrapLinkVoice::~PsWrapLinkVoice() { stop(); }

// thread เสียงของ StreamSession: บวกเสียงเพื่อนที่สะสมพอแล้วเข้าเสียงเกม (s16 interleaved) — นาฬิกาเล่น = เสียงเกมที่มาจาก PS5
void PsWrapLinkVoice::mix(int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	if (!pcm || !frames || channels == 0 || rate != unsigned(kRate))
		return;
	std::lock_guard<std::mutex> lock(d->m);
	for (auto &[id, v] : d->voices) {
		if (v->muted)
			continue;
		if (!v->playing && v->available() >= kPrebuffer)
			v->playing = true;
		if (!v->playing)
			continue;
		const size_t n = std::min(frames, v->available() / kCh);
		const float *src = v->fifo.data() + v->read;
		for (size_t i = 0; i < n; i++)
			for (unsigned c = 0; c < channels; c++) {
				const float f = src[i * kCh + std::min<unsigned>(c, kCh - 1)];
				const int x = int(pcm[i * channels + c]) + int(std::lround(f * 32767.0f));
				pcm[i * channels + c] = int16_t(std::clamp(x, -32768, 32767));
			}
		v->read += n * kCh;
		if (v->available() < kCh)
			v->playing = false;   // หมด = รอสะสมใหม่ (เพื่อนหยุดพูด/เน็ตสะดุด)
		v->compact();
	}
}

void PsWrapLinkVoice::push(const QString &peer, const uint8_t *data, size_t size, uint32_t timestamp)
{
	if (!data || size == 0 || size > 1500)
		return;
	{
		std::lock_guard<std::mutex> lock(d->m);
		auto &slot = d->voices[peer];
		if (!slot) {
			slot = std::make_unique<Voice>();
			if (!openDecoder(*slot)) {
				qCWarning(pswrapLink) << "voice: no Opus decoder";
				d->voices.erase(peer);
				return;
			}
		}
		Voice &v = *slot;
		if (v.muted)
			return;
		// มาช้ากว่าก้อนที่เล่นไปแล้ว = ทิ้ง (ไม่มี jitter buffer เรียงลำดับ — เสียงพูดรับได้)
		if (v.has_ts && static_cast<int32_t>(timestamp - v.last_ts) <= 0)
			return;
		v.has_ts = true;
		v.last_ts = timestamp;
		v.pkt->data = const_cast<uint8_t *>(data);
		v.pkt->size = static_cast<int>(size);
		if (avcodec_send_packet(v.dec, v.pkt) < 0)
			return;
		float peak = 0.0f;
		while (avcodec_receive_frame(v.dec, v.frame) == 0) {
			peak = std::max(peak, appendFrame(v, v.frame));
			av_frame_unref(v.frame);
		}
		if (v.available() > kMaxFifo) {
			v.read = v.fifo.size() - kTrimTo;
			v.compact();
		}
		if (peak > kSpeakLevel)
			v.loud_ms = QDateTime::currentMSecsSinceEpoch();
	}
}

void PsWrapLinkVoice::removePeer(const QString &peer)
{
	std::lock_guard<std::mutex> lock(d->m);
	d->voices.erase(peer);
}

void PsWrapLinkVoice::setMuted(const QString &peer, bool muted)
{
	std::lock_guard<std::mutex> lock(d->m);
	auto it = d->voices.find(peer);
	if (it == d->voices.end()) {
		if (!muted)
			return;
		// ยังไม่เคยพูด → สร้างช่องไว้จำว่าปิดเสียง
		auto v = std::make_unique<Voice>();
		if (!openDecoder(*v))
			return;
		it = d->voices.emplace(peer, std::move(v)).first;
	}
	Voice &v = *it->second;
	v.muted = muted;
	if (muted) {
		v.fifo.clear();
		v.read = 0;
		v.playing = false;
		v.loud_ms = 0;
	}
}

bool PsWrapLinkVoice::speaking(const QString &peer) const
{
	std::lock_guard<std::mutex> lock(d->m);
	auto it = d->voices.find(peer);
	return it != d->voices.end() && !it->second->muted && QDateTime::currentMSecsSinceEpoch() - it->second->loud_ms < kSpeakHoldMs;
}

bool PsWrapLinkVoice::anyVoice() const
{
	std::lock_guard<std::mutex> lock(d->m);
	return !d->voices.empty();
}

void PsWrapLinkVoice::stop()
{
	std::lock_guard<std::mutex> lock(d->m);
	d->voices.clear();
}
