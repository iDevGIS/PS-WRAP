// PS-WRAP: Stay Awake — ต่อเครื่องไว้แต่ไม่มีใครกดจอยนาน PS จะเข้าโหมดพักเองตามการตั้งค่าประหยัดพลังงาน (เช่น 15 นาที)
// เปิดไว้ (Settings › General › While Streaming) → ไม่มี input จริงจากใครเลย (จอย/คีย์บอร์ด/ทัชแพด/เพื่อนใน Link) ครบ 10 นาที
// → ดันสติ๊กซ้ายเบาๆ แวบเดียว (สลับซ้าย/ขวาทุกครั้ง) ให้เครื่องรู้ว่ามีคนอยู่ · มีคนกดอยู่ = ไม่ยุ่งเลย
// แยกไฟล์จาก streamsession.cpp เพื่อลด conflict ตอน merge upstream (ดู docs/04-upstream-sync.md)
// ทดสอบ: env PSWRAP_STAYAWAKE_TEST_S=<วินาที> ย่อเวลารอ (เช่น 30)
#include "streamsession.h"
#include "settings.h"

#include <chiaki/time.h>

#include <QTimer>

#include <cstdlib>

namespace {
constexpr qint64 kIdleMs = 10 * 60 * 1000;   // ไม่มีใครกดนานเท่านี้ → ขยับ
constexpr int kCheckMs = 15 * 1000;
constexpr int kPulseMs = 150;                // ค้างสติ๊กไว้นานเท่านี้ (ให้ feedback sender ส่งทันแน่)
constexpr int16_t kNudge = 3000;             // ~9% ของสุดสติ๊ก — ในเดดโซนของเกมส่วนใหญ่ แต่เครื่องเห็นว่าค่าเปลี่ยน
constexpr int kStickNoise = 1500;            // สติ๊กขยับน้อยกว่านี้ = drift ไม่นับเป็นคนกด

qint64 idleLimitMs()
{
	static const qint64 v = [] {
		const char *e = std::getenv("PSWRAP_STAYAWAKE_TEST_S");
		const long s = e ? std::strtol(e, nullptr, 10) : 0;
		return s > 0 ? qint64(s) * 1000 : kIdleMs;
	}();
	return v;
}

qint64 nowMs() { return qint64(chiaki_time_now_monotonic_ms()); }

bool stickMoved(int16_t a, int16_t b) { return std::abs(int(a) - int(b)) > kStickNoise; }
}

void StreamSession::PsWrapStayAwakeInit(Settings *settings)
{
	pswrap_settings = settings;
	chiaki_controller_state_set_idle(&awake_state);
	chiaki_controller_state_set_idle(&pswrap_last_input);
	pswrap_last_input_ms = nowMs();
	pswrap_awake_timer = new QTimer(this);
	pswrap_awake_timer->setInterval(idleLimitMs() < kIdleMs ? 1000 : kCheckMs);
	connect(pswrap_awake_timer, &QTimer::timeout, this, &StreamSession::PsWrapStayAwakeTick);
	pswrap_awake_timer->start();
}

// เรียกจาก SendFeedbackState ด้วย state รวมของ input จริง (ก่อนรวม awake_state) — gyro/accel ไม่นับ (จอยวางบนโต๊ะก็สั่นเอง)
void StreamSession::PsWrapNoteInput(const ChiakiControllerState &s)
{
	const ChiakiControllerState &p = pswrap_last_input;
	bool changed = s.buttons != p.buttons || s.l2_state != p.l2_state || s.r2_state != p.r2_state
		|| stickMoved(s.left_x, p.left_x) || stickMoved(s.left_y, p.left_y)
		|| stickMoved(s.right_x, p.right_x) || stickMoved(s.right_y, p.right_y);
	for (size_t i = 0; !changed && i < CHIAKI_CONTROLLER_TOUCHES_MAX; i++)
		changed = s.touches[i].id != p.touches[i].id || s.touches[i].x != p.touches[i].x || s.touches[i].y != p.touches[i].y;
	if (!changed)
		return;
	pswrap_last_input = s;
	pswrap_last_input_ms = nowMs();
}

void StreamSession::PsWrapStayAwakeTick()
{
	if (!pswrap_settings || !pswrap_settings->GetStayAwake())
		return;
	const qint64 now = nowMs();
	const qint64 idle = now - pswrap_last_input_ms;
	if (idle < idleLimitMs())
		return;
	pswrap_last_input_ms = now;   // ครั้งถัดไปอีก 10 นาที (ตัวขยับเองไม่ผ่าน PsWrapNoteInput)
	awake_state.left_x = int16_t(kNudge * pswrap_awake_dir);
	pswrap_awake_dir = -pswrap_awake_dir;
	CHIAKI_LOGI(log.GetChiakiLog(), "PSWRAP stay awake: nobody pressed anything for %lld s, nudging the left stick", (long long)(idle / 1000));
	SendFeedbackState();
	QTimer::singleShot(kPulseMs, this, [this] {
		chiaki_controller_state_set_idle(&awake_state);
		SendFeedbackState();
	});
}
