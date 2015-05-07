/*
 * getuin_handler.cpp
 *
 *  Created on: May 6, 2015
 *      Author: jimm
 */

#include "../../common/common_object.h"
#include "../../frame/frame_impl.h"
#include "../../frame/redis_session.h"
#include "../../include/control_head.h"
#include "../../include/contacts_msg.h"
#include "../../include/msg_head.h"
#include <string>

using namespace std;
using namespace FRAME;

class CGetUinHandler : public CBaseObject
{
	struct UserSession
	{
		UserSession()
		{
			m_nUin = 0;
		}
		ControlHead			m_stCtlHead;
		MsgHeadCS			m_stMsgHeadCS;
		CGetUinReq			m_stGetUinReq;
		string				m_strAccountName;
		uint32_t			m_nUin;
	};

public:

	virtual int32_t Init()
	{
		return 0;
	}
	virtual int32_t Uninit()
	{
		return 0;
	}
	virtual int32_t GetSize()
	{
		return 0;
	}

	int32_t GetUin(ICtlHead *pCtlHead, IMsgHead *pMsgHead, IMsgBody *pMsgBody, uint8_t *pBuf, int32_t nBufSize);

	int32_t OnSessionGetAccountName(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionGetUserUin(int32_t nResult, void *pReply, void *pSession);

	int32_t OnSessionGetVersion(int32_t nResult, void *pReply, void *pSession);

	int32_t OnRedisSessionTimeout(void *pTimerData);
};


