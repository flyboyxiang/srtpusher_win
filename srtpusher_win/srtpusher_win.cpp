// srtpusher_win.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include<string>

using namespace std;

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavutil/rational.h>
#include <libavutil/error.h>
}

void Init();
void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, FILE *outfile);

int interrupt_cb(void* cb) {
	return 0;
}
void show_dshow_device_option()
{
	AVFormatContext *pFormatCtx = avformat_alloc_context();
	AVDictionary* options = NULL;
	av_dict_set(&options, "list_options", "true", 0);
	AVInputFormat *iformat = av_find_input_format("dshow");
	printf("========Device Option Info======\n");
	avformat_open_input(&pFormatCtx, "video=Integrated Camera", iformat, &options);
	printf("================================\n");
}



int main()
{
	Init();
	//show_dshow_device_option();

	string fileInput = "video=Integrated Camera";//"video=Integrated Webcam";
	AVFormatContext *pFormatCtx = avformat_alloc_context();
	pFormatCtx->interrupt_callback.callback = interrupt_cb;
	AVInputFormat *ifmt = av_find_input_format("dshow");
	AVDictionary *format_opts = nullptr;
	av_dict_set_int(&format_opts, "rtbufsize", 18432000, 0);
	int ret = avformat_open_input(&pFormatCtx, fileInput.c_str(), ifmt, &format_opts);
	if (ret < 0)
	{
		return  ret;
	}
	ret = avformat_find_stream_info(pFormatCtx, nullptr);
	av_dump_format(pFormatCtx, 0, fileInput.c_str(), 0);
	if (ret >= 0)
	{
		cout << "open input stream successfully" << endl;
	}
	int videoIndex = -1;
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMediaType::AVMEDIA_TYPE_VIDEO)
		{
			videoIndex = i;
			break;
		}
	}
	if (videoIndex == -1)
	{
		cout << "Couldn't find a video stream." << endl;
		return -1;
	}

	//AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));
	//AVFrame *pFrame = av_frame_alloc();
	//while (1) {
	//	ret=av_read_frame(pFormatCtx, packet);
	//	if (ret >= 0) {
	//		if (packet->stream_index == videoIndex) {
	//			//调用H264进行编码
	//		}
	//	}
	//}

	AVCodec* pH264Codec = avcodec_find_encoder(AVCodecID::AV_CODEC_ID_H264);
	AVCodecContext* pH264CodecContext = NULL;
	AVFrame *frame;
	AVPacket *pkt;
	if (!pH264Codec) {
		cout << "Couldn't find the H264 encoder." << endl;
		return -1;
	}
	pH264CodecContext = avcodec_alloc_context3(pH264Codec);
	if (!pH264CodecContext) {
		cout << "Couldn't find the H264 encoder." << endl;
		return -1;
	}
	/* put sample parameters */
	pH264CodecContext->bit_rate = 400000;
	/* resolution must be a multiple of two */
	pH264CodecContext->width = 352;
	pH264CodecContext->height = 288;
	/* frames per second */
	AVRational r1, r2;
	r1.num = 1;
	r1.den = 25;
	r2.num = 25;
	r2.den = 1;

	pH264CodecContext->time_base = r1;
	pH264CodecContext->framerate = r2;
	/* emit one intra frame every ten frames
	* check frame pict_type before passing frame
	* to encoder, if frame->pict_type is AV_PICTURE_TYPE_I
	* then gop_size is ignored and the output of encoder
	* will always be I frame irrespective to gop_size
	*/
	pH264CodecContext->gop_size = 10;
	//No b frames for livestreaming
	pH264CodecContext->has_b_frames = 0;
	pH264CodecContext->max_b_frames = 0;
	pH264CodecContext->pix_fmt = AV_PIX_FMT_YUV420P;
	//for livestreaming reduce B frame and make realtime encoding
	av_opt_set(pH264CodecContext->priv_data, "preset", "superfast", 0);
	av_opt_set(pH264CodecContext->priv_data, "tune", "zerolatency", 0);

	/* open it */
	ret = avcodec_open2(pH264CodecContext, pH264Codec, NULL);
	if (ret < 0) {
		//fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
		exit(1);
	}
	frame = av_frame_alloc();
	FILE* f;
	fopen_s(&f, "D:/fftestvideo.h264", "wb");
	if (!f) {
		fprintf(stderr, "Could not open %s\n", "D:/fftestvideo");
		exit(1);
	}
	if (!frame) {
		fprintf(stderr, "Could not allocate video frame\n");
		exit(1);
	}
	frame->format = pH264CodecContext->pix_fmt;
	frame->width = pH264CodecContext->width;
	frame->height = pH264CodecContext->height;

	ret = av_frame_get_buffer(frame, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate the video frame data\n");
		exit(1);
	}
	pkt = av_packet_alloc();
	if (!pkt)
		exit(1);

	/* encode 1 second of video */
	for (int i = 0; i < 25; i++) {
		fflush(stdout);

		/* make sure the frame data is writable */
		ret = av_frame_make_writable(frame);
		if (ret < 0)
			exit(1);

		/* prepare a dummy image */
		/* Y */
		for (int y = 0; y < pH264CodecContext->height; y++) {
			for (int x = 0; x < pH264CodecContext->width; x++) {
				frame->data[0][y * frame->linesize[0] + x] = x + y + i * 3;
			}
		}

		/* Cb and Cr */
		for (int y = 0; y < pH264CodecContext->height / 2; y++) {
			for (int x = 0; x < pH264CodecContext->width / 2; x++) {
				frame->data[1][y * frame->linesize[1] + x] = 128 + y + i * 2;
				frame->data[2][y * frame->linesize[2] + x] = 64 + x + i * 5;
			}
		}

		frame->pts = i;
		//try to adapt bit_rate
		pH264CodecContext->bit_rate = 800000;
		/* encode the image */
		encode(pH264CodecContext, frame, pkt, f);
		//放到发送队列，组包TS 通过SRT发送
		av_packet_unref(pkt);
	}

	/* flush the encoder */
	encode(pH264CodecContext, NULL, pkt, f);
	//av_packet_unref(pkt);
	uint8_t endcode[] = { 0, 0, 1, 0xb7 };
	fwrite(endcode, 1, sizeof(endcode), f);
	fclose(f);

	avcodec_free_context(&pH264CodecContext);
	av_frame_free(&frame);
	av_packet_free(&pkt);

}

void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt, FILE *outfile)
{
	int ret;

	/* send the frame to the encoder */
	if (frame)
		printf("Send frame %lld\n", frame->pts);

	ret = avcodec_send_frame(enc_ctx, frame);
	if (ret < 0) {
		fprintf(stderr, "Error sending a frame for encoding\n");
		exit(1);
	}

	while (ret >= 0) {
		ret = avcodec_receive_packet(enc_ctx, pkt);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return;
		else if (ret < 0) {
			fprintf(stderr, "Error during encoding\n");
			exit(1);
		}

		printf("Write packet %lld (size=%5d)\n", pkt->pts, pkt->size);
		fwrite(pkt->data, 1, pkt->size, outfile);
		av_packet_unref(pkt);
	}
}

void Init()
{
	avdevice_register_all();
	//deprecated no need to call av_register_all
	//av_register_all();
	//avfilter_register_all(); 
}


int DecodePacket(AVFormatContext* pFormatCtx, AVCodecContext* pCodecCtx, int videoIndex) {
	int ret = -1;
	AVStream *stream = pFormatCtx->streams[videoIndex];
	AVCodec *dec = avcodec_find_decoder(stream->codecpar->codec_id);
	if (!dec) {
		return -1;
	}
	pCodecCtx = avcodec_alloc_context3(dec);
	if (!pCodecCtx) {
		return AVERROR(ENOMEM);
	}
	ret = avcodec_parameters_to_context(pCodecCtx, stream->codecpar);
	if (ret < 0) {
		return ret;
	}
	pCodecCtx->framerate = av_guess_frame_rate(pFormatCtx, stream, NULL);
	/* Open decoder */
	ret = avcodec_open2(pCodecCtx, dec, NULL);
	if (ret < 0) {
		return ret;
	}
	int screen_w = pCodecCtx->width;
	int screen_h = pCodecCtx->height;
}


