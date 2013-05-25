/**
 * Copyright © 2013 Kirill Gavrilov <kirill@sview.ru>
 *
 * Distributed under the Boost Software License, Version 1.0.
 * See accompanying file license-boost.txt or copy at
 * http://www.boost.org/LICENSE_1_0.txt
 */

#include <StStrings/StMsgQueue.h>

StMsgQueue::StMsgQueue() {
    //
}

StMsgQueue::~StMsgQueue() {
    //
}

void StMsgQueue::popAll() {
    myMutex.lock();
    while(!myQueue.empty()) {
        const StMsg& aMsg = myQueue.front();
        myQueue.pop_front();
        if(aMsg.Type == StLogger::ST_ERROR) {
            stError(*aMsg.Text);
        } else {
            stInfo(*aMsg.Text);
        }
    }
    myMutex.unlock();
}

bool StMsgQueue::pop(StMsg& theMessage) {
    myMutex.lock();
    if(myQueue.empty()) {
        myMutex.unlock();
        return false;
    }

    theMessage = myQueue.front();
    myQueue.pop_front();
    myMutex.unlock();
    return true;
}

void StMsgQueue::doPush(const StMsg& theMessage) {
    myMutex.lock();
    myQueue.push_back(theMessage);
    myMutex.unlock();
}

void StMsgQueue::pushInfo(const StHandle<StString>& theMessage) {
    StMsg aMsg;
    aMsg.Type = StLogger::ST_INFO;
    aMsg.Text = theMessage;
    //ST_DEBUG_LOG(*theMessage);
    doPush(aMsg);
}

void StMsgQueue::pushError(const StHandle<StString>& theMessage) {
    StMsg aMsg;
    aMsg.Type = StLogger::ST_ERROR;
    aMsg.Text = theMessage;
    //ST_ERROR_LOG(*theMessage);
    doPush(aMsg);
}

void StMsgQueue::doPushInfo(const StString& theMessage) {
    StMsg aMsg;
    aMsg.Type = StLogger::ST_INFO;
    aMsg.Text = new StString(theMessage);
    //ST_DEBUG_LOG(*theMessage);
    doPush(aMsg);
}

void StMsgQueue::doPushError(const StString& theMessage) {
    StMsg aMsg;
    aMsg.Type = StLogger::ST_ERROR;
    aMsg.Text = new StString(theMessage);
    //ST_ERROR_LOG(*theMessage);
    doPush(aMsg);
}
