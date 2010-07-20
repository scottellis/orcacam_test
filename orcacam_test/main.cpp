/*
  A little program for testing the Hamamatsu Orca-Flash camera
  attached to an Xcelera frame grabber and using the DCAM-API SDK.
*/

#include <windows.h>
#include "resource.h"

// DCAM-API
#include "dcamapi.h"
//#include "dcamprop.h"

// OpenCV
#include "cv.h"
#include "highgui.h"

void image_loop(HDCAM hdcam, IplImage *img);
void update_frame_count(int frame_count, unsigned long start, unsigned long timing);
void print_last_dcamerr(HDCAM hdcam, const char *lastcall);
HDCAM allocate_camera();
IplImage *allocate_image(HDCAM hdcam);
bool set_exposure_time(HDCAM hdcam, double exposure_sec);
bool get_exposure_time_from_user(HDCAM hdcam);
bool setup_camera(HDCAM hdcam);
bool read_one_frame(HDCAM hdcam, IplImage *img, unsigned long *elapsed);
void scale_image_to_16_bits(IplImage *img);
void scale_image_to_8_bits(IplImage *img, unsigned short *raw_data);

class genericInput {
public:
	char _title[32];
	char _prompt[32];
	double _old_value;
	double _new_value;
};

BOOL CALLBACK generic_input_dlgproc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

int g_data_lshift = 0;
int g_data_rshift = 0;
double g_exposure_sec = 0.0015;
unsigned short *g_raw_data;

int main(int argc, char **argv)
{
	HDCAM hdcam = 0;
	IplImage *img = NULL;

	timeBeginPeriod(1);

	hdcam = allocate_camera();

	if (!hdcam)
		goto done;

	if (!set_exposure_time(hdcam, g_exposure_sec))
		goto done;

	if (!setup_camera(hdcam))
		goto done;

	img = allocate_image(hdcam);
	
	if (!img)
		goto done;

	cvNamedWindow("HCImage", 0); 

	image_loop(hdcam, img);

done:
	if (g_raw_data)
		delete [] g_raw_data;

	if (img)
		cvReleaseImage(&img);

	if (hdcam)	
		dcam_close(hdcam);

	dcam_uninit(NULL, NULL);

	timeEndPeriod(1);

	return 0;
}

void image_loop(HDCAM hdcam, IplImage *img)
{
	int key, frame_count;
	unsigned long start, timing, elapsed, status;
	bool frame_allocated = false;
	bool done = false;

	frame_count = 0;
	start = GetTickCount();
	timing = 0;

	// 0 ms / frame
	if (!dcam_precapture(hdcam, DCAM_CAPTUREMODE_SEQUENCE)) {
		print_last_dcamerr(hdcam, "dcam_precapture");
		return;
	}

	// 12 ms / frame
	if (!dcam_allocframe(hdcam, 10)) {
		print_last_dcamerr(hdcam, "dcam_allocframe");
		return;
	}
	else {
		frame_allocated = true;
	}

	// 54 ms / frame
	if (!dcam_capture(hdcam)) {
		print_last_dcamerr(hdcam, "dcam_capture");
		done = true;
	}

	while (!done) {	
		if (!read_one_frame(hdcam, img, &elapsed))
			break;

		timing += elapsed;

		frame_count++;

		if ((frame_count & 0x0011) == 0x0011) {
			cvShowImage("HCImage", img);
			key = cvWaitKey(1);
			update_frame_count(frame_count, start, timing);
		
			switch (key & 0xff) {
			case 0x1b:	// escape
				done = true;
				break;

			case 'G':
			case 'g':
				MessageBox((HWND)cvGetWindowHandle("HCImage"), "todo", "Gain Setting", MB_OK);
				break;

			case 'S':
			case 's':
				get_exposure_time_from_user(hdcam);
				break;
			}
		}
	}
		
	// 1 == DCAM_STATUS_BUSY
	dcam_getstatus(hdcam, &status);

	if (DCAM_STATUS_BUSY == status) {
		// 116 ms / frame
		dcam_idle(hdcam);
	}

	if (frame_allocated)
		dcam_freeframe(hdcam);

	dcam_getstatus(hdcam, &status);

	if (DCAM_STATUS_BUSY == status)
		dcam_idle(hdcam);

}

bool read_one_frame(HDCAM hdcam, IplImage *img, unsigned long *elapsed)
{
	unsigned long wait_flags;
	long newestframeindex, framecount, bytes_per_row;
	unsigned long bytes_per_frame;
	void *top;
	bool success = false;

	// 0 ms / frame
	if (!dcam_getdataframebytes(hdcam, &bytes_per_frame)) {
		print_last_dcamerr(hdcam, "dcam_getdataframebytes");
		goto read_done;
	}

	wait_flags = DCAM_EVENT_FRAMEEND;
	//wait_flags = DCAM_EVENT_CAPTUREEND;
		
	// 27 ms / frame
	*elapsed = timeGetTime();
	if (!dcam_wait(hdcam, &wait_flags, DCAM_WAIT_INFINITE, NULL)) {
		print_last_dcamerr(hdcam, "dcam_wait");
		goto read_done;
	}
	*elapsed = timeGetTime() - *elapsed;

	// 0.02 ms / frame
	dcam_gettransferinfo(hdcam, &newestframeindex, &framecount);

	// 2 ms / frame
	dcam_lockdata(hdcam, &top, &bytes_per_row, newestframeindex);
	memcpy(g_raw_data, top, bytes_per_frame);
	dcam_unlockdata(hdcam);
		
	// 4 ms / frame
	if (img->depth == IPL_DEPTH_8U) 
		scale_image_to_8_bits(img, g_raw_data);
	else if (img->depth == IPL_DEPTH_16U) 
		scale_image_to_16_bits(img);
	
	success = true;

read_done:

	return success;
}

void update_frame_count(int frame_count, unsigned long start, unsigned long timing)
{
	HDC hdc;
	RECT r;
	int len, oldbkmode;
	COLORREF oldcolor; 
	SIZE sz;
	unsigned long elapsed;
	char buff[32];

	HWND hWnd = (HWND) cvGetWindowHandle("HCImage");

	if (!hWnd)
		return;

	hdc = GetDC(hWnd);

	elapsed = GetTickCount() - start;

	len = sprintf_s(buff, sizeof(buff), "Frame %d", frame_count);
	GetClientRect(hWnd, &r);
	GetTextExtentPoint32(hdc, buff, len, &sz);

	oldcolor = SetTextColor(hdc, RGB(60, 180, 0));
	oldbkmode = SetBkMode(hdc, TRANSPARENT);
	
	TextOut(hdc, r.left + 10, r.bottom - (3 * (sz.cy + 10)), buff, len);

	if (elapsed > 0) {
		len = sprintf_s(buff, sizeof(buff), "Rate %0.2lf fps", 
						(1000.0 * (double)frame_count) / (double)elapsed);
		TextOut(hdc, r.left + 10, r.bottom - (2 * (sz.cy + 10)), buff, len);
	}

	len = sprintf_s(buff, sizeof(buff), "Timing %0.2lf ms / frame", 
					(double)timing / (double)frame_count);
	TextOut(hdc, r.left + 10, r.bottom - (sz.cy + 10), buff, len);
	

	SetBkMode(hdc, oldbkmode);
	SetTextColor(hdc, oldcolor);

	ReleaseDC(hWnd, hdc);
}

HDCAM allocate_camera()
{
	long num_devices;
	unsigned long ticks;
	HDCAM hdcam = 0;

	ticks = GetTickCount();

	if (!dcam_init(NULL, &num_devices, NULL)) {
#if defined (CONSOLE_ONLY)
		printf("dcam_init() failed\n");
#else
		MessageBox(NULL, "dcam_init() failed", "dcam_init", MB_OK);
#endif
		return 0;
	}

	printf("dcam_init() took %lu ms\n", GetTickCount() - ticks);

	if (num_devices < 1) {
#if defined (CONSOLE_ONLY)
		printf("dcam_init(): num_devices = %lu\n", num_devices);
#else
		MessageBox(NULL, "No cameras detected", "dcam_init", MB_OK);
#endif
		return 0;
	}

	ticks = GetTickCount();

	if (!dcam_open(&hdcam, 0, NULL)) {
		printf("dcam_open() failed\n");
		return 0;
	}
	
	printf("dcam_open() took %lu ms\n", GetTickCount() - ticks);

	return hdcam;
}

IplImage *allocate_image(HDCAM hdcam)
{
	SIZE size;
	CvSize sz;
	unsigned long bytes_per_frame;
	
	IplImage *img = NULL;

	if (!dcam_getdatasize(hdcam, &size)) {
		print_last_dcamerr(hdcam, "dcam_getdatasize()");
		return NULL;
	}
	
	if (size.cx == 0 || size.cy == 0)
		return NULL;

	if (!dcam_getdataframebytes(hdcam, &bytes_per_frame)) {
		print_last_dcamerr(hdcam, "dcam_getdataframebytes");
		return NULL;
	}

	sz.width = size.cx;
	sz.height = size.cy;

	g_raw_data = new unsigned short[sz.width * sz.height];

	if (!g_raw_data)
		return NULL;

	img = cvCreateImage(sz, IPL_DEPTH_8U, 1);

	return img;
}

bool set_exposure_time(HDCAM hdcam, double exposure_sec)
{
	if (!dcam_setexposuretime(hdcam, exposure_sec)) {
		print_last_dcamerr(hdcam, "dcam_setexposuretime");
		return false;
	}

	g_exposure_sec = exposure_sec;

	return true;
}

bool get_exposure_time_from_user(HDCAM hdcam)
{
	genericInput gi;

	strncpy_s(gi._title, sizeof(gi._title), "Shutter Input", _TRUNCATE);
	strncpy_s(gi._prompt, sizeof(gi._prompt), "Shutter (sec)", _TRUNCATE);
	gi._old_value = g_exposure_sec;
	gi._new_value = g_exposure_sec;

	if (DialogBoxParam(GetModuleHandle(NULL), 
						MAKEINTRESOURCE(IDD_DLG_GENERIC_INPUT),
						(HWND) cvGetWindowHandle("HCImage"),
						generic_input_dlgproc,
						(LPARAM) &gi)) 
		return set_exposure_time(hdcam, gi._new_value);			

	return false;
}

bool setup_camera(HDCAM hdcam)
{
	int32 min, max;

	if (!dcam_getdatarange(hdcam, &max, &min)) {
		print_last_dcamerr(hdcam, "dcam_getdatarange");
		return false;
	}

	if (max > 0) {
		g_data_rshift = 0;

		while (max > 255) {
			g_data_rshift++;
			max >>= 1;
		}
	}

	return true;
}

void print_last_dcamerr(HDCAM hdcam, const char *lastcall)
{
	char msg[256];

	memset(msg, 0, sizeof(msg));

	long err = dcam_getlasterror(hdcam, msg, sizeof(msg));

#if defined (CONSOLE_ONLY)
	if (lastcall && *lastcall) 
		printf("DCAM failure: %s returned 0x%08X\n", lastcall, err);
	else
		printf("DCAM error 0x%08X\n", err);

	if (*msg)
		printf("\t%s\n", msg);
#else
	char buff[512];

	if (lastcall && *lastcall) 
		sprintf_s(buff, sizeof(buff), "%s : Error code 0x%08X", lastcall, err);
	else
		sprintf_s(buff, sizeof(buff), "Error code 0x%08X", err);

	if (*msg) {
		strncat_s(buff, sizeof(buff), "\n\n", _TRUNCATE);
		strncat_s(buff, sizeof(buff), msg, _TRUNCATE);
	}

	MessageBox(NULL, buff, "DCAM Error", MB_OK);
#endif
}

/*
  TODO: Figure out num_bits dynamically. Could be 12 or 14.
*/
void scale_image_to_16_bits(IplImage *img)
{
	int i, n;
	unsigned short *p;
	
	n = img->width * img->height;

	p = (unsigned short *)img->imageData;

	for (i = 0; i < n; i++, p++) {
		*p <<= g_data_lshift;
	}
}

void scale_image_to_8_bits(IplImage *img, unsigned short *raw_data)
{
	int n = img->width * img->height;
		
	for (int i = 0; i < n; i++)
		img->imageData[i] = (unsigned char) (raw_data[i] >> g_data_rshift);
}

BOOL CALLBACK generic_input_dlgproc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
	genericInput *gi;
	char buff[32];

	switch (msg) {
	case WM_INITDIALOG:
		gi = (genericInput *)lParam;

		if (!gi)
			return TRUE;

		SetWindowLong(hDlg, GWL_USERDATA, lParam);
		
		SetWindowText(hDlg, gi->_title);
		SetDlgItemText(hDlg, IDC_PROMPT, gi->_prompt);
		sprintf_s(buff, sizeof(buff), "%0.4lf", gi->_old_value);
		SetDlgItemText(hDlg, IDC_VALUE, buff);
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam)) {
		case IDOK:
			memset(buff, 0, sizeof(buff));
			if (GetDlgItemText(hDlg, IDC_VALUE, buff, sizeof(buff) - 1)) {
				gi = (genericInput *) GetWindowLong(hDlg, GWL_USERDATA);			
				gi->_new_value = atof(buff);

				if (abs(gi->_old_value - gi->_new_value) > 0.0001)
					EndDialog(hDlg, 1);
				else 
					EndDialog(hDlg, 0);
			}
			else {
				MessageBox(hDlg, "Need a value", "Input Error", MB_OK);
			}

			break;

		case IDCANCEL:
			EndDialog(hDlg, 0);
			break;
		}

	}

	return FALSE;
}