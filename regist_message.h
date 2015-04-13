/*
 * regist_message.h
 *
 *  Created on: Mar 10, 2015
 *      Author: jimm
 */

#ifndef REGIST_MESSAGE_H_
#define REGIST_MESSAGE_H_

#include "../frame/frame.h"
#include "../include/msg_head.h"
#include "../include/control_head.h"
#include "../include/contacts_msg.h"
#include "logic/setuserinfo_handler.h"
#include "logic/getuserinfo_handler.h"

using namespace FRAME;

MSGMAP_BEGIN(msgmap)
ON_PROC_PCH_PMH_PMB_PU8_I32(MSGID_SETUSERINFO_REQ, ControlHead, MsgHeadCS, CSetUserInfoReq, CSetUserInfoHandler, CSetUserInfoHandler::SetUserInfo);
ON_PROC_PCH_PMH_PMB_PU8_I32(MSGID_GETUSERINFO_REQ, ControlHead, MsgHeadCS, CGetUserInfoReq, CGetUserInfoHandler, CGetUserInfoHandler::GetUserInfo);
MSGMAP_END(msgmap)

#endif /* REGIST_MESSAGE_H_ */