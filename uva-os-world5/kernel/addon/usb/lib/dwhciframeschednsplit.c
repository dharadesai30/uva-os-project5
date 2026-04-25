//
// dwhciframeschednsplit.c
//
// USPi - An USB driver for Raspberry Pi written in C
// Copyright (C) 2014  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <uspi/dwhciframeschednsplit.h>
#include <uspi/dwhciregister.h>
#include <uspi/assert.h>

#define FRAME_UNSET	(DWHCI_MAX_FRAME_NUMBER+1)

void DWHCIFrameSchedulerNoSplit (TDWHCIFrameSchedulerNoSplit *pThis, boolean bIsPeriodic)
{
	assert (pThis != 0);

	TDWHCIFrameScheduler *pBase = (TDWHCIFrameScheduler *) pThis;

	pBase->_DWHCIFrameScheduler = _DWHCIFrameSchedulerNoSplit;
	pBase->StartSplit = DWHCIFrameSchedulerNoSplitStartSplit;
	pBase->CompleteSplit = DWHCIFrameSchedulerNoSplitCompleteSplit;
	pBase->TransactionComplete = DWHCIFrameSchedulerNoSplitTransactionComplete;
	pBase->WaitForFrame = DWHCIFrameSchedulerNoSplitWaitForFrame;
	pBase->IsOddFrame = DWHCIFrameSchedulerNoSplitIsOddFrame;

	pThis->m_bIsPeriodic = bIsPeriodic;
	pThis->m_nNextFrame = FRAME_UNSET;

}

void _DWHCIFrameSchedulerNoSplit (TDWHCIFrameScheduler *pBase)
{
}

void DWHCIFrameSchedulerNoSplitStartSplit (TDWHCIFrameScheduler *pBase)
{
	assert (0);
}

boolean DWHCIFrameSchedulerNoSplitCompleteSplit (TDWHCIFrameScheduler *pBase)
{
	assert (0);
	return FALSE;
}

void DWHCIFrameSchedulerNoSplitTransactionComplete (TDWHCIFrameScheduler *pBase, u32 nStatus)
{
	assert (0);
}

void DWHCIFrameSchedulerNoSplitWaitForFrame (TDWHCIFrameScheduler *pBase)
{
	TDWHCIFrameSchedulerNoSplit *pThis = (TDWHCIFrameSchedulerNoSplit *) pBase;
	assert (pThis != 0);

	unsigned sec, msec, sec2, msec2;  /// xzl 
	current_time(&sec, &msec); 

	TDWHCIRegister FrameNumber;
	DWHCIRegister (&FrameNumber, DWHCI_HOST_FRM_NUM);

	// xzl: below - tried to wait for the next frame 
	// however, on qemu, the assumption that cpu runs much faster than io is broken. 
	// cpu won't read continuous frame numbers. 
	// given that DWHCI_MAX_FRAME_NUMBER is 3fff, there is also a chance to wrap around (?)
#if defined(PLAT_RPI3QEMU)
	unsigned cur_frame = DWHCI_HOST_FRM_NUM_NUMBER (DWHCIRegisterRead (&FrameNumber)) & DWHCI_MAX_FRAME_NUMBER;
	pThis->m_nNextFrame = (DWHCI_HOST_FRM_NUM_NUMBER (DWHCIRegisterRead (&FrameNumber))+1) & DWHCI_MAX_FRAME_NUMBER;
	if (!pThis->m_bIsPeriodic)
	{
		// as long as the frame # changes, wait is over
		while ((DWHCI_HOST_FRM_NUM_NUMBER (DWHCIRegisterRead (&FrameNumber)) & DWHCI_MAX_FRAME_NUMBER) == cur_frame)
			; // do nothing		
	}
#else	// actual hardware
	pThis->m_nNextFrame = (DWHCI_HOST_FRM_NUM_NUMBER (DWHCIRegisterRead (&FrameNumber))+1) & DWHCI_MAX_FRAME_NUMBER;
	if (!pThis->m_bIsPeriodic)
	{
		while ((DWHCI_HOST_FRM_NUM_NUMBER (DWHCIRegisterRead (&FrameNumber)) & DWHCI_MAX_FRAME_NUMBER) != pThis->m_nNextFrame)
		{
			// do nothing		
		}
	}
#endif
	current_time(&sec2, &msec2);
	if (sec2*1000+msec2-sec*1000-msec>500)
		LogWrite(">>>>>>>>>>>>>> DWHCIFrameSchedulerNoSplitWaitForFrame:", LOG_WARNING, 
			"warn: wait %d s - %d s", sec, sec2);

	_DWHCIRegister (&FrameNumber);
}

boolean DWHCIFrameSchedulerNoSplitIsOddFrame (TDWHCIFrameScheduler *pBase)
{
	TDWHCIFrameSchedulerNoSplit *pThis = (TDWHCIFrameSchedulerNoSplit *) pBase;
	assert (pThis != 0);

	return pThis->m_nNextFrame & 1 ? TRUE : FALSE;
}
