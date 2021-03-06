#include "MainWindow.hpp"

#include <string>

#include <CommCtrl.h>
#include <process.h>
#include <ShObjIdl.h>
#include <Uxtheme.h>
#include <Windows.h>

#include <bass.h>

#include "../resource/resource.h"

#include "OSUtil.hpp"

using std::string;

namespace inetr {
	void MainWindow::radioOpenURL(string url) {
		void* *args = new void*[2];

		string *str = new string(url);
		*args = this;
		*(args + 1) = str;

		currentStreamURL = url;

		_beginthread(staticRadioOpenURLThread, 0,
			reinterpret_cast<void*>(args));
	}

	void MainWindow::radioOpenURLThread(string url) {
		radioStatus_currentMetadata = "";

		KillTimer(window, bufferTimerId);
		KillTimer(window, metaTimerId);

		if (currentStream != 0) {
			BASS_ChannelStop(currentStream);
			BASS_StreamFree(currentStream);
		}

		radioStatus = INETR_RS_Connecting;
		updateStatusLabel();

		HSTREAM tempStream = BASS_StreamCreateURL(url.c_str(), 0, 0, nullptr,
			0);

		if (currentStreamURL != url || radioStatus != INETR_RS_Connecting) {
			BASS_StreamFree(tempStream);
			return;
		}

		currentStream = tempStream;

		if (currentStream != 0) {
			SetTimer(window, bufferTimerId, 50, nullptr);
		} else {
			radioStatus = INETR_RS_ConnectionError;
			updateStatusLabel();
		}
	}

	void MainWindow::radioStop() {
		if (currentStream != 0) {
			BASS_ChannelStop(currentStream);
			BASS_StreamFree(currentStream);
		}

		ShowWindow(stationImg, SW_HIDE);
		radioStatus = INETR_RS_Idle;
		updateStatusLabel();

		KillTimer(window, bufferTimerId);
		KillTimer(window, metaTimerId);

		currentStation = nullptr;
	}

	float MainWindow::radioGetVolume() const {
		return radioMuted ? 0.0f : userConfig.RadioVolume;
	}

	void MainWindow::radioSetVolume(float volume) {
		radioSetMuted(false);

		userConfig.RadioVolume = volume;
		if (currentStream)
			BASS_ChannelSetAttribute(currentStream, BASS_ATTRIB_VOL,
			radioGetVolume());

		SendMessage(volumePbar, PBM_SETPOS, (WPARAM)(volume * 100.0f),
			(LPARAM)0);

		ShowWindow(volumePbar, SW_SHOW);
		SetTimer(window, hideVolBarTimerId, 1000, nullptr);
	}

	void MainWindow::radioSetMuted(bool muted) {
		radioMuted = muted;
		if (currentStream)
			BASS_ChannelSetAttribute(currentStream, BASS_ATTRIB_VOL,
			radioGetVolume());

		if (OSUtil::IsVistaOrLater()) {
			SendMessage(volumePbar, PBM_SETSTATE, muted ?
				(isColorblindModeEnabled ? PBST_PAUSED : PBST_ERROR) :
				PBST_NORMAL, (LPARAM)0);
		} else if (IsAppThemed() == FALSE) {
			SendMessage(volumePbar, PBM_SETBARCOLOR,
				(WPARAM)0, (LPARAM)(muted ? RGB(255, 0, 0) : CLR_DEFAULT));
		}

		ShowWindow(volumePbar, SW_SHOW);
		if (muted)
			KillTimer(window, hideVolBarTimerId);
		else
			SetTimer(window, hideVolBarTimerId, 1000, nullptr);

		updateStatusLabel();

		if (OSUtil::IsWin7OrLater()) {
			ITaskbarList3 *taskbarList;
			if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr,
				CLSCTX_INPROC_SERVER, __uuidof(taskbarList),
				reinterpret_cast<void**>(&taskbarList)))) {

				HICON icon = LoadIcon(instance,
					MAKEINTRESOURCE(muted ? IDI_ICON_SOUND : IDI_ICON_MUTE));

				THUMBBUTTON thumbButtons[1];

				thumbButtons[0].dwMask = THB_ICON;
				thumbButtons[0].iId = thumbBarMuteBtnId;
				thumbButtons[0].hIcon = icon;

				taskbarList->ThumbBarUpdateButtons(window, 1, thumbButtons);

				DeleteObject((HGDIOBJ)icon);

				taskbarList->SetProgressValue(window, muted ? 100UL : 0UL,
					100UL);
				taskbarList->SetProgressState(window, muted ? TBPF_ERROR :
					TBPF_NOPROGRESS);

				taskbarList->Release();
			}
		}
	}
}
