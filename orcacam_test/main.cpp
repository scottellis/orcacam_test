/*
  A little program for testing the Hamamatsu Orca-Flash camera
  attached to an Xcelera frame grabber and using the DCAM-API SDK.
*/

#include <windows.h>

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
bool read_one_frame(HDCAM hdcam, IplImage *img);
void scale_image_to_16_bits(IplImage *img, int actual_bits);

int main(int argc, char **argv)
{
	HDCAM hdcam = 0;
	IplImage *img = NULL;

	hdcam = allocate_camera();

	if (!hdcam)
		goto done;
				
	img = allocate_image(hdcam);
	
	if (!img)
		goto done;

	if (!set_exposure_time(hdcam, 0.0020))
		goto done;

	cvNamedWindow("HCImage", 0); //CV_WINDOW_AUTOSIZE);

	do {
		if (!read_one_frame(hdcam, img))
			break;

		cvShowImage("HCImage", img);
		
	} while (cvWaitKey(10) < 0);


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
	IplImage *img = NULL;

	if (!dcam_getdatasize(hdcam, &size)) {
		print_last_dcamerr(hdcam, "dcam_getdatasize()");
		return NULL;
	}
	
	if (size.cx == 0 || size.cy == 0)
		return NULL;

	sz.width = size.cx;
	sz.height = size.cy;

	img = cvCreateImage(sz, IPL_DEPTH_16U, 1);
	
	return img;
}

bool set_exposure_time(HDCAM hdcam, double exposure_sec)
{
	if (!dcam_setexposuretime(hdcam, exposure_sec)) {
		print_last_dcamerr(hdcam, "dcam_setexposuretime");
		return false;
	}

	return true;
}

void print_last_dcamerr(HDCAM hdcam, const char *lastcall)
{
	char msg[256];

	memset(msg, 0, sizeof(msg));

	long err = dcam_getlasterror(hdcam, msg, sizeof(msg));

	printf("failure: %s returned 0x%08X\n");

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
	
	bytes_per_frame = img->width * img->height * 2;

	memcpy(img->imageData, top, bytes_per_frame);
	dcam_unlockdata(hdcam);
	
	dcam_idle(hdcam);

	scale_image_to_16_bits(img, 12);

	success = true;

read_done:

	if (frame_allocated)
		dcam_freeframe(hdcam);

	return success;
}

/*
  TODO: Figure out num_bits dynamically. Could be 12 or 14.
*/
void scale_image_to_16_bits(IplImage *img, int actual_bits)
{
	int i, n;
	unsigned short *p;
	int shift = 16 - actual_bits;

	n = img->width * img->height;

	p = (unsigned short *)img->imageData;

	for (i = 0; i < n; i++, p++) {
		*p <<= shift;
	}
}

