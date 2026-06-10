// Diagnostic: captures Raw Input HID reports (Valve + generic controllers) and
// reports, per device and report ID, how often each byte offset changes between
// consecutive reports. Offsets changing in ~100% of packets while the controller
// is untouched are noise (sequence numbers, timestamps, IMU) and must be ignored
// by idle detection.
#include <windows.h>
#include <hidusage.h>
#include <stdio.h>
#include <map>
#include <vector>
#include <string>

struct Stats {
	UINT sizeHid = 0;
	UINT count = 0;
	std::vector<BYTE> last;
	std::vector<BYTE> first;
	std::vector<UINT> changed;
};

static std::map<std::wstring, std::map<int, Stats>> g_stats;
static std::map<std::wstring, std::wstring> g_devinfo;
static int g_captureSeconds = 15;

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
	if (m == WM_INPUT)
	{
		UINT size = 0;
		GetRawInputData((HRAWINPUT)l, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
		if (!size)
			return 0;
		std::vector<BYTE> buf(size);
		if (GetRawInputData((HRAWINPUT)l, RID_INPUT, buf.data(), &size, sizeof(RAWINPUTHEADER)) != size)
			return 0;
		RAWINPUT* raw = (RAWINPUT*)buf.data();
		if (raw->header.dwType != RIM_TYPEHID)
			return 0;
		UINT ns = 0;
		GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICENAME, NULL, &ns);
		std::vector<WCHAR> name(ns + 1);
		if (GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICENAME, name.data(), &ns) == (UINT)-1)
			return 0;
		std::wstring path(name.data());
		if (!g_devinfo.count(path))
		{
			RID_DEVICE_INFO di = { 0 };
			UINT ds = sizeof(di);
			di.cbSize = sizeof(di);
			GetRawInputDeviceInfoW(raw->header.hDevice, RIDI_DEVICEINFO, &di, &ds);
			wchar_t tmp[256];
			swprintf(tmp, 256, L"VID=%04X PID=%04X UsagePage=%04X Usage=%02X",
				di.hid.dwVendorId, di.hid.dwProductId, di.hid.usUsagePage, di.hid.usUsage);
			g_devinfo[path] = tmp;
		}
		const RAWHID& hid = raw->data.hid;
		for (DWORD n = 0; n < hid.dwCount; ++n)
		{
			const BYTE* r = hid.bRawData + n * hid.dwSizeHid;
			Stats& s = g_stats[path][r[0]];
			s.sizeHid = hid.dwSizeHid;
			s.count++;
			if (s.changed.size() < hid.dwSizeHid)
				s.changed.assign(hid.dwSizeHid, 0);
			if (s.first.empty())
				s.first.assign(r, r + hid.dwSizeHid);
			if (s.last.size() == hid.dwSizeHid)
				for (UINT i = 0; i < hid.dwSizeHid; ++i)
					if (s.last[i] != r[i])
						s.changed[i]++;
			s.last.assign(r, r + hid.dwSizeHid);
		}
		return 0;
	}
	if (m == WM_TIMER)
	{
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcW(h, m, w, l);
}

static void hexdump(const wchar_t* label, const std::vector<BYTE>& d)
{
	wprintf(L"  %s:", label);
	for (size_t i = 0; i < d.size(); ++i)
		wprintf(L" %02X", d[i]);
	wprintf(L"\n");
}

int wmain(int argc, wchar_t** argv)
{
	if (argc > 1)
		g_captureSeconds = _wtoi(argv[1]);

	WNDCLASSW wc = { 0 };
	wc.lpfnWndProc = WndProc;
	wc.hInstance = GetModuleHandleW(NULL);
	wc.lpszClassName = L"HidDumpWnd";
	RegisterClassW(&wc);
	HWND hwnd = CreateWindowW(L"HidDumpWnd", L"", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
	if (!hwnd)
	{
		wprintf(L"CreateWindow failed\n");
		return 1;
	}

	std::vector<RAWINPUTDEVICE> devices;
	std::map<DWORD, bool> seen;
	auto add = [&](USHORT up, USHORT u) {
		DWORD key = ((DWORD)up << 16) | u;
		if (seen.count(key))
			return;
		seen[key] = true;
		RAWINPUTDEVICE d = { 0 };
		d.usUsagePage = up;
		d.usUsage = u;
		d.dwFlags = RIDEV_INPUTSINK;
		d.hwndTarget = hwnd;
		devices.push_back(d);
	};
	add(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_JOYSTICK);
	add(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_GAMEPAD);
	add(HID_USAGE_PAGE_GENERIC, HID_USAGE_GENERIC_MULTI_AXIS_CONTROLLER);

	UINT deviceCount = 0;
	wprintf(L"== Valve HID devices present ==\n");
	if (GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) == 0 && deviceCount > 0)
	{
		std::vector<RAWINPUTDEVICELIST> rawDevices(deviceCount);
		if (GetRawInputDeviceList(rawDevices.data(), &deviceCount, sizeof(RAWINPUTDEVICELIST)) != (UINT)-1)
		{
			for (UINT i = 0; i < deviceCount; ++i)
			{
				if (rawDevices[i].dwType != RIM_TYPEHID)
					continue;
				RID_DEVICE_INFO di = { 0 };
				UINT ds = sizeof(di);
				di.cbSize = sizeof(di);
				if (GetRawInputDeviceInfo(rawDevices[i].hDevice, RIDI_DEVICEINFO, &di, &ds) == (UINT)-1)
					continue;
				if (di.dwType != RIM_TYPEHID || di.hid.dwVendorId != 0x28DE)
					continue;
				UINT ns = 0;
				GetRawInputDeviceInfoW(rawDevices[i].hDevice, RIDI_DEVICENAME, NULL, &ns);
				std::vector<WCHAR> nm(ns + 1);
				GetRawInputDeviceInfoW(rawDevices[i].hDevice, RIDI_DEVICENAME, nm.data(), &ns);
				wprintf(L"  VID=%04X PID=%04X UsagePage=%04X Usage=%02X  %s\n",
					di.hid.dwVendorId, di.hid.dwProductId, di.hid.usUsagePage, di.hid.usUsage, nm.data());
				add(di.hid.usUsagePage, di.hid.usUsage);
			}
		}
	}

	if (!RegisterRawInputDevices(devices.data(), (UINT)devices.size(), sizeof(devices[0])))
	{
		wprintf(L"RegisterRawInputDevices failed (%lu)\n", GetLastError());
		return 1;
	}
	wprintf(L"== Capturing for %d seconds - DO NOT touch the controller ==\n", g_captureSeconds);
	fflush(stdout);
	SetTimer(hwnd, 1, g_captureSeconds * 1000, NULL);

	MSG msg;
	while (GetMessageW(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}

	wprintf(L"\n== Results ==\n");
	for (auto& dev : g_stats)
	{
		wprintf(L"\nDevice: %s\n  %s\n", dev.first.c_str(), g_devinfo[dev.first].c_str());
		for (auto& rep : dev.second)
		{
			Stats& s = rep.second;
			wprintf(L" ReportID=0x%02X  reports=%u  sizeHid=%u\n", rep.first, s.count, s.sizeHid);
			hexdump(L"first", s.first);
			hexdump(L"last ", s.last);
			wprintf(L"  changed offsets (offset:changes/%u):", s.count > 0 ? s.count - 1 : 0);
			bool any = false;
			for (UINT i = 0; i < s.changed.size(); ++i)
			{
				if (s.changed[i] > 0)
				{
					wprintf(L" %u:%u", i, s.changed[i]);
					any = true;
				}
			}
			wprintf(any ? L"\n" : L" none\n");
		}
	}
	if (g_stats.empty())
		wprintf(L"No HID reports received.\n");
	return 0;
}
