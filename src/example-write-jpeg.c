#include "libuvc/libuvc.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <jpeglib.h>

#define WIDTH   1280
#define HEIGHT  720
#define QUALITY 90
#define	FPS	8 

typedef struct 
{
	int count;
	unsigned char* jpeg_buf; 
	int bSaveRaw;
	int bSaveJpeg;

	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
} UVCCB_PARAM;


typedef struct {
	enum uvc_frame_format fmt;
	char* desc;
} UVC_FRAME_FMT_DESC;


/***

**/
static UVC_FRAME_FMT_DESC desc_table[] = {{

  UVC_FRAME_FORMAT_UNKNOWN ,	"Unknown" },{
  UVC_FRAME_FORMAT_UNCOMPRESSED,	"Uncompressed" },{
  UVC_FRAME_FORMAT_COMPRESSED,	"Compressed" },{
  /** YUYV/YUV2/YUV422: YUV encoding with one luminance value per pixel and
   * one UV (chrominance) pair for every two pixels.
   */
  UVC_FRAME_FORMAT_YUYV,	"YUYV" },{
  UVC_FRAME_FORMAT_UYVY,	"UYVY" },{
  /** 24-bit RGB */
  UVC_FRAME_FORMAT_RGB,	"RGB" },{
  UVC_FRAME_FORMAT_BGR,	"BGR" },{
  /** Motion-JPEG (or JPEG) encoded images */
  UVC_FRAME_FORMAT_MJPEG,	"MJPEG" },{
  /** Greyscale images */
  UVC_FRAME_FORMAT_GRAY8,	"GRAYB" },{
  UVC_FRAME_FORMAT_GRAY16,	"GRAY16" },{
  /* Raw colour mosaic images */
  UVC_FRAME_FORMAT_BY8,	"BY8" },{
  UVC_FRAME_FORMAT_BA81,	"BAB1" },{
  UVC_FRAME_FORMAT_SGRBG8,	"SGRBG8" },{
  UVC_FRAME_FORMAT_SGBRG8,	"SGBRG8" },{
  UVC_FRAME_FORMAT_SRGGB8,	"SRGGB8" },{
  UVC_FRAME_FORMAT_SBGGR8,	"SBGGR8" },{
  /** Number of formats understood */
  UVC_FRAME_FORMAT_COUNT, NULL
}};


/****************************************************************
	
*****************************************************************/

static char* uvc_frame_fmt_2_text(enum uvc_frame_format fmt) 
{
	char* text = desc_table[0].desc;

	int ii = 0;
	while(desc_table[ii].fmt != UVC_FRAME_FORMAT_COUNT) 
	{
		if (fmt == desc_table[ii].fmt ) {
			return desc_table[ii].desc;
			break;
		}
		ii++;
	}
	return text;
}


//////////////////////////////////////////////////////////////
/* This callback function runs once per frame. Use it to perform any
 * quick processing you need, or have it put the frame into your application's
 * input queue. If this function takes too long, you'll start losing frames. */

void cb(uvc_frame_t *frame, void *ptr) 
{
  uvc_frame_t *bgr;
  uvc_error_t ret;

	char filename[128] = {0,};

	UVCCB_PARAM* pParam = (UVCCB_PARAM *)ptr;
  	pParam->count = pParam->count + 1;

	if ((pParam->count)  % FPS == 0) {
		  printf("%s: %d, %dx%d, %d Kb\n", 
					uvc_frame_fmt_2_text(frame->frame_format),
					pParam->count, 
					frame->width, 
					frame->height, 
					(int)(frame->data_bytes+1023)/1024
					);

		size_t alloc_size = frame->width * frame->height * 3;
		uvc_frame_t* newimage = uvc_allocate_frame(alloc_size);
		if (newimage) 
		{
			char filename[128] = {0,};
			if (UVC_SUCCESS == uvc_any2rgb(frame, newimage)) {

				if (pParam->bSaveRaw) {
					sprintf(filename, "frame_%04d.raw", pParam->count );
					FILE* fp_raw = fopen(filename, "wb+");
					if (fp_raw) {
						printf("Writing RAW (%d bytes)\n", newimage->data_bytes );
						fwrite( newimage->data, newimage->data_bytes, 1, fp_raw );
						fclose(fp_raw);
					}
				}

				if (pParam->bSaveJpeg) {
					sprintf(filename, "frame_%04d.jpg", pParam->count );
					FILE* fp = fopen(filename, "wb+");
					if (fp) {

						struct jpeg_compress_struct cinfo;
						struct jpeg_error_mgr jerr;
	
						JSAMPROW row_pointer[1];	/* pointer to JSAMPLE row[s] */
						int row_stride;		/* physical row width in image buffer */	
	
						cinfo.err = jpeg_std_error(&jerr);  // errors get written to stderr

						jpeg_create_compress (&cinfo);   
						cinfo.image_width = WIDTH; 
						cinfo.image_height = HEIGHT; 
						cinfo.input_components = 3; 
						cinfo.in_color_space = JCS_RGB;	

						jpeg_set_defaults(&cinfo);
						jpeg_set_quality(&cinfo, QUALITY, TRUE);
						jpeg_stdio_dest(&cinfo, fp);

						jpeg_start_compress(&cinfo, TRUE);

						row_stride = frame->width * 3;	/* JSAMPLEs per row in image_buffer */

							while (cinfo.next_scanline < cinfo.image_height) {
							/* jpeg_write_scanlines expects an array of pointers to scanlines.
							* Here the array is only one element long, but you could pass
							* more than one scanline at a time if that's more convenient.
							*/
							row_pointer[0] = & newimage->data[cinfo.next_scanline * row_stride];
							(void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
						}
						fclose(fp);
					}
				}
			}
			uvc_free_frame(newimage);
		}
	}
 }
 
 
 //=============================================================================================

int main(int argc, char **argv) 
{
  uvc_context_t *ctx;
  uvc_device_t *dev;
  uvc_device_handle_t *devh;
  uvc_stream_ctrl_t ctrl;
  uvc_error_t res;

  /* Initialize a UVC service context. Libuvc will set up its own libusb
   * context. Replace NULL with a libusb_context pointer to run libuvc
   * from an existing libusb context. */
  res = uvc_init(&ctx, NULL);

  if (res < 0) {
    uvc_perror(res, "uvc_init");
    return res;
  }

  puts("UVC initialized");


  UVCCB_PARAM*  pParam = (UVCCB_PARAM *) malloc(sizeof(UVCCB_PARAM));
  if (!pParam) {
    uvc_perror(res, "param init failed");
  	return 0;
  }
  puts("PARAM initialized");

	pParam->count = 0;
	pParam->jpeg_buf = NULL;	//(unsigned char *) malloc(640 * 480 *  3); 

	pParam->bSaveRaw = FALSE;
	pParam->bSaveJpeg = TRUE;

	pParam->cinfo.err = jpeg_std_error(&pParam->jerr);
	jpeg_create_compress(&pParam->cinfo);


  /* Locates the first attached UVC device, stores in dev */
  res = uvc_find_device(
      ctx, &dev,
      0, 0, NULL); /* filter devices: vendor_id, product_id, "serial_num" */

  if (res < 0) {
    uvc_perror(res, "uvc_find_device"); /* no devices found */
  } else {
    puts("Device found");

    /* Try to open the device: requires exclusive access */
    res = uvc_open(dev, &devh);

    if (res < 0) {
      uvc_perror(res, "uvc_open"); /* unable to open device */
    } else {
      puts("Device opened");

      /* Print out a message containing all the information that libuvc
       * knows about the device */
     uvc_print_diag(devh, stderr);

      /* Try to negotiate a 640x480 30 fps YUYV stream profile */
      res = uvc_get_stream_ctrl_format_size(
          devh, &ctrl, /* result stored in ctrl */
          UVC_FRAME_FORMAT_YUYV, /* YUV 422, aka YUV 4:2:2. try _COMPRESSED */
          WIDTH, HEIGHT, FPS /* width, height, fps */
      );

      /* Print out the result */
      uvc_print_stream_ctrl(&ctrl, stderr);

      if (res < 0) {
        uvc_perror(res, "get_mode"); /* device doesn't provide a matching stream */
      } else {
        /* Start the video stream. The library will call user function cb:
         *   cb(frame, (void*) 12345)
         */
        res = uvc_start_streaming(devh, &ctrl, cb, pParam, 0);

        if (res < 0) {
          uvc_perror(res, "start_streaming"); /* unable to start stream */
        } else {
          puts("Streaming...");

		uvc_set_ae_mode(devh, 1); /* e.g., turn on auto exposure */

          sleep(10); /* stream for 10 seconds */

          /* End the stream. Blocks until last callback is serviced */
		uvc_stop_streaming(devh);
		
          puts("Done streaming.");
        }
      }

      /* Release our handle on the device */
      uvc_close(devh);
      puts("Device closed");
    }

    /* Release the device descriptor */
    uvc_unref_device(dev);
  }


	if (pParam) {
		free(pParam);
	}
	
  /* Close the UVC context. This closes and cleans up any existing device handles,
   * and it closes the libusb context if one was not provided. */
  uvc_exit(ctx);
  puts("UVC exited");

  return 0;
}

