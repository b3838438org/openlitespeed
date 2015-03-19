/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013 - 2015  LiteSpeed Technologies, Inc.                 *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/
#include <thread/workcrew.h>

#include <edio/eventnotifier.h>
#include <http/httplog.h>
#include <lsr/ls_lfqueue.h>

#ifndef LS_WORKCREW_LF
#include <thread/pthreadworkqueue.h>
#endif

#include <new>

WorkCrew::WorkCrew(EventNotifier *en)
        : m_pNotifier(en)
        , m_crew()
        , m_pProcess(NULL)
{
#ifdef LS_WORKCREW_LF
    m_pJobQueue = ls_lfqueue_new();
#else
    m_pJobQueue = new PThreadWorkQueue();
#endif
}


WorkCrew::~WorkCrew()
{
    stopProcessing();
#ifdef LS_WORKCREW_LF
    ls_lfqueue_delete(m_pJobQueue);
#else
    delete m_pJobQueue;
#endif
}


int WorkCrew::increaseTo(int numMembers)
{
    int i;
    m_crew.guarantee(NULL, numMembers);
    for (i = m_crew.getSize(); i < numMembers; ++i)
    {
        Worker *worker = new(m_crew.getNew()) Worker(wcWorkerFn);
        if (worker->run(this))
            return LS_FAIL;
#ifdef LS_WORKCREW_DEBUG
        printf("Worker %lx, %lu started\n", (unsigned long)worker,
               worker->getId());
#endif
    }
    return 0;
}


int WorkCrew::decreaseTo(int numMembers)
{
    void *retVal;
    int i;
    int iSize = m_crew.getSize();

    for (i = numMembers; i < iSize; ++i)
        m_crew.getObj(i)->setStop();

    for (i = numMembers; i < iSize; ++i)
        m_crew.getObj(i)->join(&retVal);

    m_crew.setSize(numMembers);
#ifdef LS_WORKCREW_DEBUG
    printf("%d workers left\n", m_crew.getSize());
#endif
    return 0;
}


ls_lfnodei_t *WorkCrew::getJob()
{
#ifdef LS_WORKCREW_LF
    struct timespec timeout;
    timeout.tv_sec = 0;
    timeout.tv_nsec = 250000000;
    return ls_lfqueue_timedget(m_pJobQueue, &timeout);
#else
    int size = 1;
    ls_lfnodei_t *pWork;
    pWork = NULL;
    if (m_pJobQueue->get(&pWork, size, 250) != 0)
        return NULL;
    return pWork;
#endif
}


int WorkCrew::startJobProcessor(int numWorkers, ls_lfqueue_t *pFinishedQueue,
                          WorkCrewProcessFn processor)
{
    assert(processor && pFinishedQueue);
    m_pProcess = processor;
    m_pFinishedQueue = pFinishedQueue;
    if (D_ENABLED(DL_MORE))
        LOG_D(("WorkCrew::startJobProcessor(), Starting Processor."));
#ifndef LS_WORKCREW_LF
    m_pJobQueue->start();
#endif
    return resize(numWorkers);
}


void WorkCrew::stopProcessing()
{
    decreaseTo(0);
    if (D_ENABLED(DL_MORE))
        LOG_D(("WorkCrew::stopProcessing(), Stopping Processor."));
#ifndef LS_WORKCREW_LF
    m_pJobQueue->shutdown();
#endif
    m_pFinishedQueue = NULL;
}


void *WorkCrew::getAndProcessJob()
{
    void *ret;
    ls_lfnodei_t *item = getJob();
    if (!item)
        return NULL;
    if (D_ENABLED(DL_MORE))
        LOG_D(("WorkCrew::getAndProcessJob(), Got Job."));
    if ((ret = m_pProcess(item)) != NULL)
    {
        if (D_ENABLED(DL_MORE))
            LOG_D(("WorkCrew::getAndProcessJob(), Job Failed,"
                   " returned: %d", ret));
        return ret;
    }
    if (D_ENABLED(DL_MORE))
        LOG_D(("WorkCrew::getAndProcessJob(), Job Completed."));
    putFinishedItem(item);
    return NULL;
}


int WorkCrew::putFinishedItem(ls_lfnodei_t *item)
{
    int ret;
    ret = ls_lfqueue_put(m_pFinishedQueue, item);
    if (m_pNotifier)
    {
        if (D_ENABLED(DL_MORE))
            LOG_D(("WorkCrew::putFinishedItem(), Notifying Notifier."));
        m_pNotifier->notify();
    }
    return ret;
}


int WorkCrew::resize(int numMembers)
{
    if (numMembers < 0)
        return LS_FAIL;
    else if (numMembers < LS_WORKCREW_MINWORKER)
        numMembers = LS_WORKCREW_MINWORKER;
    else if (numMembers > LS_WORKCREW_MAXWORKER)
        numMembers = LS_WORKCREW_MAXWORKER;
    if (numMembers == m_crew.getSize())
        return 0;
    if (D_ENABLED(DL_MORE))
        LOG_D(("WorkCrew::resize(), Updating Crew Size to %d.", numMembers));
    return (numMembers > m_crew.getSize() ? increaseTo(numMembers) :
            decreaseTo(numMembers));
}


int WorkCrew::addJob(ls_lfnodei_t *item)
{
#ifdef LS_WORKCREW_LF
    return ls_lfqueue_put(m_pJobQueue, item);
#else
    return m_pJobQueue->append(&item, 1);
#endif
}


