// PS-WRAP Link: จอยของเพื่อนเข้า StreamSession — แยกไฟล์จาก streamsession.cpp เพื่อลด conflict ตอน merge upstream
#include "streamsession.h"

void StreamSession::PsWrapSetLinkState(const ChiakiControllerState &state)
{
	link_state = state;
	SendFeedbackState();
}
