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


void print_last_dcamerr(HDCAM hdcam, const char *lastcall);
HDCAM allocate_camera();
IplImage *allocate_image(HDCAM hdcam);
bool set_exposure_time(HDCAM hdcam, double exposure_sec);
bool get_exposure_time_from_user(HDCAM hdcam);
bool setup_camera(HDCAM hdcam);
bool read_one_frame(HDCAM hdcam, IplImage *img);
void scale_image_to_16_bits(IplImage *img);

class genericInput {
public:
	char _title[32];
	char _prompt[32];
	double _old_value;
	double _new_value;
};

BOOL CALLBACK generic_input_dlgproc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

int g_data_shift = 0;
double g_exposure_sec = 0.0006;

int main(int argc, char **argv)
{
	int key;
	bool done;
	HDCAM hdcam = 0;
	IplImage *img = NULL;

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

	cvNamedWindow("HCImage", 0); //CV_WINDOW_AUTOSIZE);

	done = false;

	while (!done) {
		if (!read_one_frame(hdcam, img))
			break;

		cvShowImage("HCImage", img);
		
		key = cvWaitKey(10);

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


done:
	if (img)
		cvReleaseImage(&img);

	if (hdcam)
		dcam_close(hdcam);

	dcam_uninit(NULL, NULL);

	return 0;
}

HDCAM allocate_camera()
{
	long num_devices;
	unsigned long ticks;
	HDCAM hdcam = 0;

	ticks = GetTickCount();

	if (!dcam_init(NULL, &num_devices, NULL)) {
		printf("dcam_init() failed\n");
		return 0;
	}

	printf("dcam_init() took %lu ms\n", GetTickCount() - ticks);

	if (num_devices < 1) {
		printf("dcam_init(): num_devices = %lu\n", num_devices);
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
	unsigned long bytes_per_frame, pixel_depth;
	
	IplImage *img = NULL;

	if (!dcam_getdatasize(hdcam, &size)) {
		print_last_dcamerr(hdcam, "dcam_getdatasize()");
		return NULL;
	}
	
	if (size.cx == 0 || size.cy == 0)
		return NULL;

	if (!dcam_getdataframebytes(hdcam, &bytes_per_frame)) {
		print_last_dcamerr(hdcam, "dcam_getdataframebytes");
		return false;
	}

	sz.width = size.cx;
	sz.height = size.cy;

	pixel_depth = bytes_per_frame / (sz.width * sz.height);

	if (pixel_depth == 2) {
		img = cvCreateImage(sz, IPL_DEPTH_16U, 1);
	}
	else if (pixel_depth == 1) {
		img = cvCreateImage(sz, IPL_DEPTH_8U, 1);
	}
	else {
		printf("Unhandled pixel_depth %ld\n", pixel_depth);
	}
	
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
	//DCAM_DATATYPE datatype;
	int32 min, max;
	//unsigned long before_framebytes, after_framebytes;

	/*
	if (!dcam_getdataframebytes(hdcam, &before_framebytes)) {
		print_last_dcamerr(hdcam, "dcam_getdataframebytes");
		return false;
	}
	*/

	/*
	if (!dcam_getdatatype(hdcam, &datatype)) {
		print_last_dcamerr(hdcam, "dcam_getdatatype");
		return false;
	}

	if (!dcam_setdatatype(hdcam, DCAM_DATATYPE_UINT8)) {
		print_last_dcamerr(hdcam, "dcam_setdatatype");
		return false;
	}
	
	if (!dcam_getdatarange(hdcam, &max, &min)) {
		print_last_dcamerr(hdcam, "dcam_getdatarange");
		return false;
	}
	*/

	if (!dcam_setbinning(hdcam, 2)) {
		print_last_dcamerr(hdcam, "dcam_setbinning");
		return false;
	}
	/*
	if (!dcam_getdataframebytes(hdcam, &after_framebytes)) {
		print_last_dcamerr(hdcam, "dcam_getdataframebytes");
		return false;
	}
	*/

	if (!dcam_getdatarange(hdcam, &max, &min)) {
		print_last_dcamerr(hdcam, "dcam_getdatarange");
		return false;
	}

	if (max > 0) {
		max++;
		g_data_shift = 0;

		while (max < 65536) {
			g_data_shift++;
			max <<= 1;
		}
	}

	return true;
}

void print_last_dcamerr(HDCAM hdcam, const char *lastcall)
{
	char msg[256];

	memset(msg, 0, sizeof(msg));

	long err = dcam_getlasterror(hdcam, msg, sizeof(msg));

	if (lastcall && *lastcall) 
		printf("DCAM failure: %s returned 0x%08X\n", lastcall, err);
	else
		printf("DCAM error 0x%08X\n", err);

	if (*msg)
		printf("\t%s\n", msg);
}

bool read_one_frame(HDCAM hdcam, IplImage *img)
{
	unsigned long wait_flags;
	long index, total, bytes_per_row, bytes_per_frame;
	void *top;
	bool success = false;
	bool frame_allocated = false;

	if (!dcam_precapture(hdcam, DCAM_CAPTUREMODE_SNAP)) {
		print_last_dcamerr(hdcam, "dcam_precapture");
		goto read_done;
	}

	if (!dcam_allocframe(hdcam, 1)) {
		print_last_dcamerr(hdcam, "dcam_allocframe");
		goto read_done;
	}
	else {
		frame_allocated = true;
	}

	if (!dcam_capture(hdcam)) {
		print_last_dcamerr(hdcam, "dcam_capture");
		goto read_done;
	}

	wait_flags = DCAM_EVENT_FRAMEEND;

	if (!dcam_wait(hdcam, &wait_flags, DCAM_WAIT_INFINITE, NULL)) {
		print_last_dcamerr(hdcam, "dcam_wait");
		goto read_done;
	}

	dcam_gettransferinfo(hdcam, &index, &total);

	dcam_lockdata(hdcam, &top, &bytes_per_row, 0);
	
	bytes_per_frame = img->width * img->height;

	if (img->depth == IPL_DEPTH_16U)
		bytes_per_frame *= 2;

	memcpy(img->imageData, top, bytes_per_frame);
	dcam_unlockdata(hdcam);
	
	dcam_idle(hdcam);

	if (img->depth == IPL_DEPTH_16U) 
		scale_image_to_16_bits(img);

	success = true;

read_done:

	if (frame_allocated)
		dcam_freeframe(hdcam);

	return success;
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
		*p <<= g_data_shift;
	}
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