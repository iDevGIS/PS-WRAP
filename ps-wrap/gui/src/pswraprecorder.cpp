// PS-WRAP: อัดวิดีโอระหว่างสตรีม (ดู pswraprecorder.h)
#include <pswraprecorder.h>

#include <chiaki/time.h>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QMetaObject>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/opt.h>
}

Q_LOGGING_CATEGORY(pswrapRec, "pswrap.rec", QtInfoMsg)

namespace {

constexpr int kAudioRate = 48000;
constexpr int kAudioTracks = 3;            // 0 = Game + Mic, 1 = Game, 2 = Mic
constexpr size_t kMaxQueuedVideo = 6;      // เกินนี้ = encoder ตามไม่ทัน → ทิ้งเฟรม (ไม่ให้ RAM บวม)
constexpr size_t kMaxQueuedAudio = 500;    // ~5 วินาทีของ chunk 10ms
constexpr int64_t kGapFillSamples = 2880;  // เสียงเกมขาดช่วง > 60ms → เติมเงียบให้ตรงนาฬิกา
constexpr int64_t kGapSlackSamples = 960;
constexpr int64_t kLateDropSamples = 9600; // เสียงมาช้ากว่านาฬิกา > 200ms (มาเป็นก้อนหลังสะดุด) → ทิ้ง
constexpr qint64 kGameIdleUs = 250000;     // ไม่มีเสียงเกม 250ms → เดินนาฬิกาเสียงเองด้วยความเงียบ + ไมค์
constexpr int64_t kIdleLagSamples = 4800;
constexpr size_t kMicFifoMaxFrames = 9600; // ไมค์ค้างใน FIFO เกิน 200ms (นาฬิกาไมค์เร็วกว่า) → ตัดเหลือ 40ms
constexpr size_t kMicFifoKeepFrames = 1920;

QAtomicPointer<PsWrapRecorder> g_instance;

QString avErr(int err)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(err, buf, sizeof(buf));
	return QString::fromUtf8(buf);
}

AVRational q(double v, int den)
{
	return av_make_q(static_cast<int>(std::lround(v * den)), den);
}

struct AudioChunk
{
	bool mic = false;
	qint64 arrival_us = 0;
	std::vector<float> stereo; // interleaved L R
};

struct VideoItem
{
	AVFrame *frame = nullptr;
	qint64 capture_us = 0;
};

struct AudioTrack
{
	AVCodecContext *enc = nullptr;
	AVStream *st = nullptr;
	AVAudioFifo *fifo = nullptr;
	int64_t next_pts = 0;
};

} // namespace

struct PsWrapRecorder::Impl
{
	PsWrapRecorder *owner = nullptr;
	PsWrapRecConfig cfg;
	QString encoder_name;

	AVFormatContext *fmt = nullptr;
	AVCodecContext *venc = nullptr;
	AVStream *vst = nullptr;
	AudioTrack atr[kAudioTracks];
	AVPacket *pkt = nullptr;
	AVMasteringDisplayMetadata mdm = {};
	AVContentLightMetadata cll = {};
	bool has_cll = false;

	std::thread worker;
	std::mutex mtx;
	std::condition_variable cv;
	std::deque<VideoItem> vq;
	std::deque<AudioChunk> aq;
	bool accepting = false;
	bool stop_requested = false;
	std::atomic<bool> failed{false};

	qint64 t0_us = 0;
	// render thread เท่านั้น
	qint64 next_due_us = 0;
	qint64 interval_us = 16667;

	// worker เท่านั้น
	int64_t audio_pos = 0;
	qint64 last_game_arrival_us = 0;
	std::deque<float> mic_fifo; // interleaved stereo
	int64_t last_video_pts = -1;

	// สถิติ (เขียนจากหลาย thread → atomic)
	std::atomic<quint64> video_frames{0}, video_dropped{0}, audio_dropped{0}, audio_late{0}, gap_fills{0}, mic_trims{0};
	std::atomic<bool> warned_rate{false};
	bool open_failed_on_file = false;
	float game_peak = 0.0f, mic_peak = 0.0f; // worker เท่านั้น — log ตอนจบไว้พิสูจน์ว่าเสียงเข้าจริง

	~Impl() { closeAll(); }

	void closeAll()
	{
		if (venc)
			avcodec_free_context(&venc);
		for (auto &t : atr) {
			if (t.enc)
				avcodec_free_context(&t.enc);
			if (t.fifo) {
				av_audio_fifo_free(t.fifo);
				t.fifo = nullptr;
			}
		}
		if (fmt) {
			if (fmt->pb && !(fmt->oformat->flags & AVFMT_NOFILE))
				avio_closep(&fmt->pb);
			avformat_free_context(fmt);
			fmt = nullptr;
		}
		av_packet_free(&pkt);
		for (auto &v : vq)
			av_frame_free(&v.frame);
		vq.clear();
	}

	bool openVideo(QString *error)
	{
		const AVPixelFormat pix = cfg.hdr ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12;
		const char *const sdr_encoders[] = {"h264_nvenc", "h264_amf", "libx264"};
		const char *const hdr_encoders[] = {"hevc_nvenc", "hevc_amf"};
		const char *const *names = cfg.hdr ? hdr_encoders : sdr_encoders;
		const int count = cfg.hdr ? 2 : 3;

		// ~20 Mbps ที่ 1080p60 สเกลตามพิกเซล×fps (HDR 10-bit +25%)
		double bitrate = 20e6 * (double(cfg.width) * cfg.height * cfg.fps) / (1920.0 * 1080.0 * 60.0);
		if (cfg.hdr)
			bitrate *= 1.25;
		bitrate = std::clamp(bitrate, 8e6, 80e6);

		QStringList tried;
		for (int i = 0; i < count; i++) {
			const AVCodec *codec = avcodec_find_encoder_by_name(names[i]);
			if (!codec)
				continue;
			AVCodecContext *c = avcodec_alloc_context3(codec);
			if (!c)
				continue;
			c->width = cfg.width;
			c->height = cfg.height;
			c->pix_fmt = pix;
			c->time_base = AVRational{1, 1000};
			c->framerate = AVRational{cfg.fps, 1};
			c->gop_size = cfg.fps * 2;
			c->max_b_frames = 0;
			c->bit_rate = static_cast<int64_t>(bitrate);
			c->rc_max_rate = static_cast<int64_t>(bitrate * 1.5);
			c->rc_buffer_size = static_cast<int>(std::min(bitrate * 2.0, 2.0e9));
			c->color_range = AVCOL_RANGE_MPEG;
			c->chroma_sample_location = AVCHROMA_LOC_LEFT;
			if (cfg.hdr) {
				c->colorspace = AVCOL_SPC_BT2020_NCL;
				c->color_primaries = AVCOL_PRI_BT2020;
				c->color_trc = AVCOL_TRC_SMPTE2084;
			} else {
				c->colorspace = AVCOL_SPC_BT709;
				c->color_primaries = AVCOL_PRI_BT709;
				c->color_trc = AVCOL_TRC_BT709;
			}
			if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
				c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

			const QByteArray name(names[i]);
			if (name.endsWith("_nvenc")) {
				av_opt_set(c->priv_data, "preset", "p5", 0);
				av_opt_set(c->priv_data, "tune", "hq", 0);
				av_opt_set(c->priv_data, "rc", "vbr", 0);
				av_opt_set(c->priv_data, "spatial-aq", "1", 0);
				if (cfg.hdr)
					av_opt_set(c->priv_data, "profile", "main10", 0);
				else
					av_opt_set(c->priv_data, "profile", "high", 0);
			} else if (name.endsWith("_amf")) {
				av_opt_set(c->priv_data, "quality", "quality", 0);
				av_opt_set(c->priv_data, "rc", "vbr_peak", 0);
				if (cfg.hdr)
					av_opt_set(c->priv_data, "profile", "main10", 0);
			} else if (name == "libx264") {
				av_opt_set(c->priv_data, "preset", "veryfast", 0);
			}

			const int err = avcodec_open2(c, codec, nullptr);
			if (err < 0) {
				tried << QStringLiteral("%1 (%2)").arg(QString::fromUtf8(name), avErr(err));
				qCWarning(pswrapRec) << "encoder" << name << "failed:" << avErr(err);
				avcodec_free_context(&c);
				continue;
			}
			venc = c;
			encoder_name = QString::fromUtf8(name);
			break;
		}
		if (!venc) {
			*error = tried.isEmpty()
				? QObject::tr("No usable video encoder found.")
				: QObject::tr("Video encoder failed: %1").arg(tried.join(QStringLiteral(", ")));
			return false;
		}

		vst = avformat_new_stream(fmt, nullptr);
		if (!vst || avcodec_parameters_from_context(vst->codecpar, venc) < 0) {
			*error = QObject::tr("Could not create the video stream.");
			return false;
		}
		vst->time_base = venc->time_base;
		vst->avg_frame_rate = AVRational{cfg.fps, 1};

		if (cfg.hdr) {
			const auto &h = cfg.hdr_info;
			mdm.display_primaries[0][0] = q(h.prim_r[0], 50000);
			mdm.display_primaries[0][1] = q(h.prim_r[1], 50000);
			mdm.display_primaries[1][0] = q(h.prim_g[0], 50000);
			mdm.display_primaries[1][1] = q(h.prim_g[1], 50000);
			mdm.display_primaries[2][0] = q(h.prim_b[0], 50000);
			mdm.display_primaries[2][1] = q(h.prim_b[1], 50000);
			mdm.white_point[0] = q(h.white[0], 50000);
			mdm.white_point[1] = q(h.white[1], 50000);
			mdm.min_luminance = q(h.min_luma, 10000);
			mdm.max_luminance = q(h.max_luma, 10000);
			mdm.has_primaries = 1;
			mdm.has_luminance = 1;
			if (AVPacketSideData *sd = av_packet_side_data_new(&vst->codecpar->coded_side_data, &vst->codecpar->nb_coded_side_data,
					AV_PKT_DATA_MASTERING_DISPLAY_METADATA, sizeof(mdm), 0))
				memcpy(sd->data, &mdm, sizeof(mdm));
			if (h.max_cll > 0.0f) {
				has_cll = true;
				cll.MaxCLL = static_cast<unsigned>(h.max_cll);
				cll.MaxFALL = static_cast<unsigned>(h.max_fall);
				if (AVPacketSideData *sd = av_packet_side_data_new(&vst->codecpar->coded_side_data, &vst->codecpar->nb_coded_side_data,
						AV_PKT_DATA_CONTENT_LIGHT_LEVEL, sizeof(cll), 0))
					memcpy(sd->data, &cll, sizeof(cll));
			}
		}
		return true;
	}

	bool openAudio(QString *error)
	{
		const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_AAC);
		if (!codec) {
			*error = QObject::tr("AAC encoder not available.");
			return false;
		}
		const char *const titles[kAudioTracks] = {"Game + Mic", "Game", "Mic"};
		for (int i = 0; i < kAudioTracks; i++) {
			AudioTrack &t = atr[i];
			t.enc = avcodec_alloc_context3(codec);
			if (!t.enc)
				return false;
			t.enc->sample_fmt = AV_SAMPLE_FMT_FLTP;
			t.enc->sample_rate = kAudioRate;
			av_channel_layout_default(&t.enc->ch_layout, 2);
			t.enc->bit_rate = i == 0 ? 192000 : 160000;
			t.enc->time_base = AVRational{1, kAudioRate};
			if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
				t.enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
			const int err = avcodec_open2(t.enc, codec, nullptr);
			if (err < 0) {
				*error = QObject::tr("Audio encoder failed: %1").arg(avErr(err));
				return false;
			}
			t.st = avformat_new_stream(fmt, nullptr);
			if (!t.st || avcodec_parameters_from_context(t.st->codecpar, t.enc) < 0) {
				*error = QObject::tr("Could not create the audio stream.");
				return false;
			}
			t.st->time_base = t.enc->time_base;
			t.st->disposition = i == 0 ? AV_DISPOSITION_DEFAULT : 0;
			av_dict_set(&t.st->metadata, "title", titles[i], 0);
			av_dict_set(&t.st->metadata, "handler_name", titles[i], 0);
			t.fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, kAudioRate / 10);
			if (!t.fifo)
				return false;
		}
		return true;
	}

	bool open(QString *error)
	{
		const QByteArray path = cfg.path.toUtf8();
		int err = avformat_alloc_output_context2(&fmt, nullptr, "mp4", path.constData());
		if (err < 0 || !fmt) {
			*error = QObject::tr("Could not create the output file: %1").arg(avErr(err));
			return false;
		}
		pkt = av_packet_alloc();
		if (!pkt || !openVideo(error) || !openAudio(error)) {
			if (error->isEmpty())
				*error = QObject::tr("Out of memory.");
			return false;
		}
		av_dict_set(&fmt->metadata, "encoder", "PS-WRAP", 0);
		if (!(fmt->oformat->flags & AVFMT_NOFILE)) {
			err = avio_open(&fmt->pb, path.constData(), AVIO_FLAG_WRITE);
			if (err < 0) {
				open_failed_on_file = true;
				*error = QObject::tr("Could not open %1 for writing: %2").arg(QDir::toNativeSeparators(cfg.path), avErr(err));
				return false;
			}
		}
		AVDictionary *opts = nullptr;
		av_dict_set(&opts, "movflags", "+frag_keyframe+empty_moov+default_base_moof", 0);
		err = avformat_write_header(fmt, &opts);
		av_dict_free(&opts);
		if (err < 0) {
			*error = QObject::tr("Could not write the file header: %1").arg(avErr(err));
			return false;
		}
		return true;
	}

	// ---------------------------------------------------------------- worker
	bool drainEncoder(AVCodecContext *enc, AVStream *st)
	{
		for (;;) {
			int err = avcodec_receive_packet(enc, pkt);
			if (err == AVERROR(EAGAIN) || err == AVERROR_EOF)
				return true;
			if (err < 0) {
				fail(QObject::tr("Encoding failed: %1").arg(avErr(err)));
				return false;
			}
			av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
			pkt->stream_index = st->index;
			err = av_interleaved_write_frame(fmt, pkt);
			if (err < 0) {
				fail(QObject::tr("Writing the file failed: %1").arg(avErr(err)));
				return false;
			}
		}
	}

	void fail(const QString &message)
	{
		if (failed.exchange(true))
			return;
		qCWarning(pswrapRec) << "recording failed:" << message;
		QMetaObject::invokeMethod(owner, [o = owner, message]() { o->workerFailed(message); }, Qt::QueuedConnection);
	}

	void encodeVideo(VideoItem &item)
	{
		AVFrame *frame = item.frame;
		item.frame = nullptr;
		int64_t pts = (item.capture_us - t0_us) / 1000;
		if (pts <= last_video_pts)
			pts = last_video_pts + 1;
		last_video_pts = pts;
		frame->pts = pts;
		if (cfg.hdr) {
			if (AVFrameSideData *sd = av_frame_new_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA, sizeof(mdm)))
				memcpy(sd->data, &mdm, sizeof(mdm));
			if (has_cll)
				if (AVFrameSideData *sd = av_frame_new_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL, sizeof(cll)))
					memcpy(sd->data, &cll, sizeof(cll));
		}
		const int err = avcodec_send_frame(venc, frame);
		av_frame_free(&frame); // ปล่อย slot ของ render thread (encoder ที่ต้องเก็บไว้จะ ref เอง)
		if (err < 0) {
			fail(QObject::tr("Video encoding failed: %1").arg(avErr(err)));
			return;
		}
		video_frames++;
		drainEncoder(venc, vst);
	}

	bool encodeAudioTrack(AudioTrack &t, bool flush)
	{
		const int frame_size = t.enc->frame_size > 0 ? t.enc->frame_size : 1024;
		while (av_audio_fifo_size(t.fifo) >= frame_size || (flush && av_audio_fifo_size(t.fifo) > 0)) {
			AVFrame *f = av_frame_alloc();
			if (!f)
				return false;
			f->nb_samples = frame_size;
			f->format = AV_SAMPLE_FMT_FLTP;
			f->sample_rate = kAudioRate;
			av_channel_layout_copy(&f->ch_layout, &t.enc->ch_layout);
			if (av_frame_get_buffer(f, 0) < 0) {
				av_frame_free(&f);
				return false;
			}
			const int got = av_audio_fifo_read(t.fifo, reinterpret_cast<void **>(f->data), frame_size);
			for (int ch = 0; ch < 2 && got < frame_size; ch++) // เฟรมสุดท้ายไม่เต็ม → เติมเงียบ
				memset(reinterpret_cast<float *>(f->data[ch]) + got, 0, sizeof(float) * (frame_size - got));
			f->pts = t.next_pts;
			t.next_pts += frame_size;
			const int err = avcodec_send_frame(t.enc, f);
			av_frame_free(&f);
			if (err < 0) {
				fail(QObject::tr("Audio encoding failed: %1").arg(avErr(err)));
				return false;
			}
			if (!drainEncoder(t.enc, t.st))
				return false;
		}
		return true;
	}

	// เขียน n เฟรมลงทุก track: เกม (nullptr = เงียบ) + ไมค์จาก FIFO → mix
	void writeBlock(const float *game, int64_t n)
	{
		thread_local std::vector<float> planes;
		planes.resize(static_cast<size_t>(n) * 6);
		float *mixL = planes.data(), *mixR = mixL + n, *gL = mixR + n, *gR = gL + n, *mL = gR + n, *mR = mL + n;
		for (int64_t i = 0; i < n; i++) {
			const float l = game ? game[2 * i] : 0.0f;
			const float r = game ? game[2 * i + 1] : 0.0f;
			float ml = 0.0f, mr = 0.0f;
			if (mic_fifo.size() >= 2) {
				ml = mic_fifo.front(); mic_fifo.pop_front();
				mr = mic_fifo.front(); mic_fifo.pop_front();
			}
			gL[i] = l; gR[i] = r; mL[i] = ml; mR[i] = mr;
			mixL[i] = std::clamp(l + ml, -1.0f, 1.0f);
			mixR[i] = std::clamp(r + mr, -1.0f, 1.0f);
		}
		void *data[kAudioTracks][2] = {{mixL, mixR}, {gL, gR}, {mL, mR}};
		for (int t = 0; t < kAudioTracks; t++) {
			if (av_audio_fifo_write(atr[t].fifo, data[t], static_cast<int>(n)) < n) {
				fail(QObject::tr("Out of memory."));
				return;
			}
			if (!encodeAudioTrack(atr[t], false))
				return;
		}
		audio_pos += n;
	}

	void writeSilence(int64_t n)
	{
		while (n > 0 && !failed) {
			const int64_t step = std::min<int64_t>(n, 960);
			writeBlock(nullptr, step);
			n -= step;
		}
	}

	int64_t usToSamples(qint64 us) const { return (us - t0_us) * kAudioRate / 1000000; }

	void processAudio(AudioChunk &c)
	{
		const int64_t frames = static_cast<int64_t>(c.stereo.size() / 2);
		for (float v : c.stereo)
			(c.mic ? mic_peak : game_peak) = std::max(c.mic ? mic_peak : game_peak, std::fabs(v));
		if (c.mic) {
			mic_fifo.insert(mic_fifo.end(), c.stereo.begin(), c.stereo.end());
			if (mic_fifo.size() / 2 > kMicFifoMaxFrames) {
				mic_fifo.erase(mic_fifo.begin(), mic_fifo.end() - kMicFifoKeepFrames * 2);
				mic_trims++;
			}
			return;
		}
		const int64_t ideal_start = usToSamples(c.arrival_us) - frames;
		if (ideal_start - audio_pos > kGapFillSamples) {
			writeSilence(ideal_start - kGapSlackSamples - audio_pos);
			gap_fills++;
		} else if (audio_pos - ideal_start > kLateDropSamples) {
			audio_late++;
			return;
		}
		last_game_arrival_us = c.arrival_us;
		writeBlock(c.stereo.data(), frames);
	}

	void idleFill(qint64 now_us)
	{
		const qint64 since = last_game_arrival_us > 0 ? now_us - last_game_arrival_us : now_us - t0_us;
		if (since < kGameIdleUs)
			return;
		const int64_t target = usToSamples(now_us) - kIdleLagSamples;
		if (target > audio_pos)
			writeSilence(target - audio_pos);
	}

	void run()
	{
		for (;;) {
			std::deque<VideoItem> vlocal;
			std::deque<AudioChunk> alocal;
			bool stopping = false;
			{
				std::unique_lock<std::mutex> lock(mtx);
				cv.wait_for(lock, std::chrono::milliseconds(20), [this] { return stop_requested || !vq.empty(); });
				vlocal.swap(vq);
				alocal.swap(aq);
				stopping = stop_requested;
			}
			for (auto &a : alocal) {
				if (failed)
					break;
				processAudio(a);
			}
			for (auto &v : vlocal) {
				if (failed)
					av_frame_free(&v.frame);
				else
					encodeVideo(v);
			}
			if (!failed)
				idleFill(static_cast<qint64>(chiaki_time_now_monotonic_us()));
			if (stopping || failed)
				break;
		}
		finish();
	}

	void finish()
	{
		if (!failed) {
			// เสียงให้ยาวถึงเฟรมภาพสุดท้าย แล้ว flush ทุก encoder
			const int64_t video_end = (last_video_pts + 1) * kAudioRate / 1000;
			if (video_end > audio_pos && video_end - audio_pos < kAudioRate * 2)
				writeSilence(video_end - audio_pos);
			for (auto &t : atr)
				if (!failed)
					encodeAudioTrack(t, true);
			if (!failed && avcodec_send_frame(venc, nullptr) >= 0)
				drainEncoder(venc, vst);
			for (auto &t : atr)
				if (!failed && avcodec_send_frame(t.enc, nullptr) >= 0)
					drainEncoder(t.enc, t.st);
		}
		const int err = av_write_trailer(fmt);
		if (err < 0 && !failed)
			fail(QObject::tr("Finishing the file failed: %1").arg(avErr(err)));
		if (fmt->pb)
			avio_closep(&fmt->pb);
		qCInfo(pswrapRec).nospace() << "recording finished: video_frames=" << video_frames.load()
			<< " video_dropped=" << video_dropped.load() << " audio_dropped=" << audio_dropped.load()
			<< " audio_late=" << audio_late.load() << " gap_fills=" << gap_fills.load()
			<< " mic_trims=" << mic_trims.load() << " game_peak_db=" << 20.0 * std::log10(game_peak + 1e-9)
			<< " mic_peak_db=" << 20.0 * std::log10(mic_peak + 1e-9) << " audio_s=" << double(audio_pos) / kAudioRate
			<< " video_s=" << double(last_video_pts + 1) / 1000.0;
	}
};

// ---------------------------------------------------------------- PsWrapRecorder

PsWrapRecorder::PsWrapRecorder(QObject *parent)
	: QObject(parent)
{
	g_instance.storeRelease(this);
	tick_timer.setInterval(500);
	connect(&tick_timer, &QTimer::timeout, this, [this]() {
		const int s = static_cast<int>((QDateTime::currentMSecsSinceEpoch() - start_ms) / 1000);
		if (s != seconds_value) {
			seconds_value = s;
			emit secondsChanged();
		}
	});
}

PsWrapRecorder::~PsWrapRecorder()
{
	stop();
	g_instance.testAndSetOrdered(this, nullptr);
}

PsWrapRecorder *PsWrapRecorder::instance()
{
	return g_instance.loadAcquire();
}

bool PsWrapRecorder::start(const PsWrapRecConfig &config, QString *error)
{
	QString err;
	start_file_error = false;
	if (isRecording() || impl) {
		err = tr("Already recording.");
	} else {
		auto p = std::make_unique<Impl>();
		p->owner = this;
		p->cfg = config;
		p->cfg.width &= ~1;
		p->cfg.height &= ~1;
		p->cfg.fps = std::clamp(config.fps, 24, 120);
		const QString dir = QFileInfo(config.path).absolutePath();
		bool file_error = false;
		if (!QDir().mkpath(dir) || !QFileInfo(dir).isWritable()) {
			err = tr("Can't write to %1.").arg(QDir::toNativeSeparators(dir));
			file_error = true;
		} else if (!p->open(&err)) {
			file_error = p->open_failed_on_file;
		}
		start_file_error = file_error;
		if (!err.isEmpty()) {
#ifdef Q_OS_WIN
			// แก้ในแอปไม่ได้: Windows Security "Controlled folder access" บล็อก exe ที่ไม่รู้จักไม่ให้เขียน Videos/Documents/Desktop (พบ 2026-10-06)
			if (file_error)
				err += QStringLiteral(" ") + tr("If Windows Security \"Controlled folder access\" is on, allow PS-WRAP.exe there or choose another recording folder in Settings.");
#else
			Q_UNUSED(file_error);
#endif
		} else {
			p->interval_us = 1000000 / p->cfg.fps;
			p->t0_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
			p->next_due_us = p->t0_us;
			p->accepting = true;
			description_value = QStringLiteral("%1x%2 · %3 · %4").arg(p->cfg.width).arg(p->cfg.height)
				.arg(p->cfg.hdr ? QStringLiteral("HDR") : QStringLiteral("SDR"), p->encoder_name);
			qCInfo(pswrapRec) << "recording started:" << config.path << description_value;
			Impl *raw = p.get();
			raw->worker = std::thread([raw]() { raw->run(); });
			{
				std::lock_guard<std::mutex> life(life_mutex);
				impl = std::move(p);
			}
			start_ms = QDateTime::currentMSecsSinceEpoch();
			seconds_value = 0;
			last_error.clear();
			recording.storeRelease(1);
			tick_timer.start();
			emit secondsChanged();
			emit lastErrorChanged();
			emit recordingChanged();
			return true;
		}
		p.reset();
		QFile::remove(config.path);
	}
	qCWarning(pswrapRec) << "recording start failed:" << err;
	if (error)
		*error = err;
	last_error = err;
	emit lastErrorChanged();
	return false;
}

void PsWrapRecorder::stop()
{
	if (!impl)
		return;
	recording.storeRelease(0);
	tick_timer.stop();
	busy = true;
	emit recordingChanged();
	{
		std::lock_guard<std::mutex> lock(impl->mtx);
		impl->accepting = false;
		impl->stop_requested = true;
	}
	impl->cv.notify_all();
	if (impl->worker.joinable())
		impl->worker.join();
	const bool ok = !impl->failed;
	const QString path = impl->cfg.path;
	{
		std::lock_guard<std::mutex> life(life_mutex);
		impl.reset();
	}
	busy = false;
	if (ok) {
		last_path = path;
		emit lastPathChanged();
		emit saved(path);
	}
	emit recordingChanged();
}

void PsWrapRecorder::workerFailed(const QString &message)
{
	last_error = message;
	emit lastErrorChanged();
	emit failed(message);
	stop();
}

bool PsWrapRecorder::wantsVideoFrame(qint64 now_us)
{
	std::lock_guard<std::mutex> life(life_mutex);
	if (!isRecording() || !impl)
		return false;
	Impl &p = *impl;
	constexpr qint64 tolerance_us = 3000; // render ที่ 165 Hz มาทุก ~6ms — ยอมเร็วกว่ากำหนดเล็กน้อย
	if (now_us + tolerance_us < p.next_due_us)
		return false;
	p.next_due_us += p.interval_us;
	if (now_us - p.next_due_us > p.interval_us)
		p.next_due_us = now_us + p.interval_us;
	return true;
}

bool PsWrapRecorder::videoSpec(int *width, int *height, bool *hdr) const
{
	std::lock_guard<std::mutex> life(life_mutex);
	if (!isRecording() || !impl)
		return false;
	*width = impl->cfg.width;
	*height = impl->cfg.height;
	*hdr = impl->cfg.hdr;
	return true;
}

void PsWrapRecorder::pushVideoFrame(AVFrame *frame, qint64 capture_us)
{
	std::lock_guard<std::mutex> life(life_mutex);
	if (!impl) {
		av_frame_free(&frame);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(impl->mtx);
		if (impl->accepting && impl->vq.size() < kMaxQueuedVideo) {
			impl->vq.push_back(VideoItem{frame, capture_us});
			frame = nullptr;
		}
	}
	if (frame) {
		impl->video_dropped++;
		av_frame_free(&frame);
		return;
	}
	impl->cv.notify_one();
}

void PsWrapRecorder::tapGameAudio(const int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	PsWrapRecorder *r = instance();
	if (r && r->isRecording())
		r->pushAudio(false, pcm, frames, channels, rate);
}

void PsWrapRecorder::tapMicAudio(const int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	PsWrapRecorder *r = instance();
	if (r && r->isRecording())
		r->pushAudio(true, pcm, frames, channels, rate);
}

void PsWrapRecorder::pushAudio(bool mic, const int16_t *pcm, size_t frames, unsigned channels, unsigned rate)
{
	std::lock_guard<std::mutex> life(life_mutex); // stop() reset impl ใต้ lock นี้ → ไม่มี use-after-free
	Impl *p = impl.get();
	if (!p || !pcm || frames == 0 || channels == 0)
		return;
	if (rate != static_cast<unsigned>(kAudioRate)) {
		if (!p->warned_rate.exchange(true))
			qCWarning(pswrapRec) << "unsupported audio rate" << rate << (mic ? "(mic)" : "(game)") << "- skipped";
		return;
	}
	AudioChunk c;
	c.mic = mic;
	c.arrival_us = static_cast<qint64>(chiaki_time_now_monotonic_us());
	c.stereo.resize(frames * 2);
	constexpr float scale = 1.0f / 32768.0f;
	for (size_t i = 0; i < frames; i++) {
		const int16_t *s = pcm + i * channels;
		c.stereo[2 * i] = s[0] * scale;
		c.stereo[2 * i + 1] = (channels > 1 ? s[1] : s[0]) * scale;
	}
	std::lock_guard<std::mutex> lock(p->mtx); // ลำดับ lock: life_mutex → Impl::mtx เสมอ
	if (!p->accepting)
		return;
	if (p->aq.size() >= kMaxQueuedAudio) {
		p->audio_dropped++;
		return;
	}
	p->aq.push_back(std::move(c));
}
