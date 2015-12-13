/*
   This file is part of memview, a real-time memory trace visualization
   application.

   Copyright (C) 2013 Andrew Clinton

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
   02111-1307, USA.

   The GNU General Public License is contained in the file COPYING.
*/

#include <QThreadPool>
#include "MemoryState.h"
#include "StopWatch.h"
#include "Color.h"
#include "GLImage.h"
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>

MemoryState::MemoryState(int ignorebits)
    : myTime(2)
    , mySampling(false)
    , myIgnoreBits(ignorebits)
    , myBottomBits(SYSmax(theAllBits-ignorebits, thePageBits))
    , myHead(myBottomBits, 0, 0)
{
    myBottomMask = (1ull << myBottomBits)-1;
    myTopMask = ~myBottomMask;
}

MemoryState::~MemoryState()
{
}

void
MemoryState::incrementTime(StackTraceMap *stacks)
{
    QMutexLocker        lock(&myWriteLock);

    myTime++;

    bool half = myTime == theHalfLife;
    bool full = myTime == theFullLife;
    if (half || full)
    {
        // The time wrapped
        for (DisplayIterator it(begin()); !it.atEnd(); it.advance())
        {
            DisplayPage page(it.page());
            for (uint64 i = 0; i < page.size(); i++)
            {
                uint32        state = page.state(i).time();
                if (state && ((state >= theHalfLife) ^ full))
                    page.state(i).setTime(theStale);
            }
        }

        if (stacks)
        {
            StackTraceMapWriter writer(*stacks);
            uint64              start, end;

            writer.getTotalInterval(start, end);
            writer.apply(start, end, StackInfoUpdater(full));
        }

        if (full)
            myTime = 2;
    }
}

void
MemoryState::appendAddressInfo(
        QString &message, uint64 addr,
        const MMapMap &map)
{
    if (!addr)
        return;

    QString       tmp;
    uint64        paddr = addr << myIgnoreBits;

    MMapMapReader reader(map);
    auto          it = reader.find(paddr);
    MMapInfo      mmapinfo{"Address", 0, false};

    if (it != reader.end())
        mmapinfo = it.value();

    tmp.sprintf("\t%s: 0x%.12llx", mmapinfo.myStr.c_str(), paddr);

    message.append(tmp);

    uint64      off;
    auto        page = getPage(addr, off);
    if (!page.exists())
        return;

    State        entry = page.state(off);

    if (!entry.uval)
        return;

    const char  *typestr = 0;

    int type = entry.type();
    switch (type & ~MV_TypeFree)
    {
        case MV_TypeRead: typestr = "Read"; break;
        case MV_TypeWrite: typestr = "Written"; break;
        case MV_TypeInstr: typestr = "Instruction"; break;
        case MV_TypeAlloc: typestr = "Allocated"; break;
    }

    if (typestr)
    {
        if (!mmapinfo.myMapped)
            typestr = "Unmapped";
        else if (type & MV_TypeFree)
            typestr = "Deallocated";

        tmp.sprintf("\t(Thread %d %s)", entry.thread(), typestr);
        message.append(tmp);
    }
}

class Downsample : public QRunnable {
public:
    Downsample(MemoryState &dst, int shift, bool fast)
        : myDst(dst)
        , myShift(shift)
        , myFast(fast)
    {
    }

    void push(const MemoryState::DisplayPage src)
    {
        mySrc.push_back(src);
    }
    size_t size() const { return mySrc.size(); }

    virtual void run()
    {
        for (auto it = mySrc.begin(); it != mySrc.end(); ++it)
            myDst.downsamplePage(*it, myShift, myFast);
    }

private:
    MemoryState &myDst;
    std::vector<MemoryState::DisplayPage> mySrc;
    int     myShift;
    bool    myFast;
};

void
MemoryState::downsample(const MemoryState &state)
{
    const int shift = myIgnoreBits - state.myIgnoreBits;

    // Copy time first for the display to work correctly
    myTime = state.myTime;

    Downsample *task = 0;
    uint64      bunch_size = 16;
    for (DisplayIterator it(const_cast<MemoryState &>(state).begin());
            !it.atEnd(); it.advance())
    {
        // Split up source pages into tasks.  This isn't strictly
        // thread-safe when 1 << shift is greater than bunch_size, but the
        // errors aren't usually visible.
        if (!task)
            task = new Downsample(*this, shift, false);
        task->push(it.page());
        if (task->size() >= bunch_size)
        {
            QThreadPool::globalInstance()->start(task);
            task = 0;
        }
    }
    if (task)
        QThreadPool::globalInstance()->start(task);

    QThreadPool::globalInstance()->waitForDone();

    mySampling = false;
}

void
MemoryState::downsamplePage(const DisplayPage &page, int shift, bool fast)
{
    const   uint64 scale = 1ull << shift;
    const   uint64 stride = fast ? 1 : scale;
    uint64  myaddr = page.addr() >> shift;

    uint64  mytop;
    splitAddr(myaddr, mytop);

    StateArray        &state = findOrCreateState(mytop);
    state.setExists(myaddr);

    for (uint64 i = 0; i < page.size(); i += scale)
    {
        uint    &mystate = state[myaddr].uval;
        const State   *arr = page.stateArray();
        uint64   n = SYSmin(i + stride, page.size());
        for (uint64 j = i; j < n; j++)
        {
            mystate = SYSmax(mystate, arr[j].uval);
        }
        myaddr++;
    }
}
