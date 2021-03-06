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
#include <libavutil/imgutils.h>
//media server
#include "mpeg-ts.h"
#include "mpeg-ts-proto.h"
}
#include <thread>
#include <map>

#include <srt.h>

#define usleep(x) Sleep(x / 1000)

//#define SAVEFILE

static unsigned char g_sendbuffer[1316];

void InitFFmpeg();
void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,void* ts);
int OpenCameraVideo(AVFormatContext* pFormatCtx, int *pVideoIndex);
int EncodeVideo(AVFormatContext *pFormatCtx, int videoIndex, string saveFile, void* ts);
int interrupt_cb(void* cb);

static void* ts_alloc(void* /*param*/, size_t bytes)
{
	static char s_buffer[188];
	//assert(bytes <= sizeof(s_buffer));
	return s_buffer;
}

static void ts_free(void* /*param*/, void* /*packet*/)
{
	return;
}

static void ts_write(void* param, const void* packet, size_t bytes)
{
	static int recvlen = 0;
	int paramvalue = *(int*)param;
	memcpy(g_sendbuffer+recvlen, packet, 188);
	recvlen += 188;
	if (recvlen == 1316) {
		printf("write 1316 ts packet\n");
#ifdef SAVEFILE
		fwrite(packet, bytes, 1, (FILE*)param);
#else
		int st = srt_sendmsg2(*((SRTSOCKET*)param), (const char*)g_sendbuffer, 1316, NULL);
		if (st == SRT_ERROR)
		{
			fprintf(stderr, "srt_sendmsg: %s\n", srt_getlasterror_str());
		}
#endif
		
		recvlen = 0;
	}
	//
}

static int ts_stream(void* ts, int codecid)
{
	static std::map<int, int> streams;
	std::map<int, int>::const_iterator it = streams.find(codecid);
	if (streams.end() != it)
		return it->second;

	int i = mpeg_ts_add_stream(ts, codecid, NULL, 0);
	streams[codecid] = i;
	return i;
}

int main()
{
	int videoIndex = -1;
	int ss, st;
	struct sockaddr_in sa;
	int yes = 1;
	string saveFile1 = "D:/fftestvideo.h264";
	string saveFile2 = "D:/fftestvideo2.ts";
	const char message[] = "This message should be sent to the other side";

	struct mpeg_ts_func_t h;
	h.alloc = ts_alloc;
	h.write = ts_write;
	h.free = ts_free;
#ifdef SAVEFILE
	FILE* fp;
	fopen_s(&fp, saveFile2.c_str(), "wb");
	void* ts = mpeg_ts_create(&h, fp);
#else
	void* ts = mpeg_ts_create(&h, &ss);
#endif
	
	InitFFmpeg();
	AVFormatContext *pFormatCtx = avformat_alloc_context();

	//init srt
	// use this function to initialize the UDT library
	srt_startup();
	srt_setloglevel(srt_logging::LogLevel::debug);
	//ss = srt_create_socket();
	ss=srt_socket(AF_INET, SOCK_DGRAM, 0);
	if (ss == SRT_ERROR)
	{
		fprintf(stderr, "srt_socket: %s\n", srt_getlasterror_str());
		return 1;
	}
	if (inet_pton(AF_INET, "192.168.8.117", &sa.sin_addr) != 1)
	{
		return 1;
	}
	sa.sin_family = AF_INET;
	sa.sin_port = htons(10000);
	printf("srt setsockflag\n");
	//srt_setsockflag(ss, SRTO_SENDER, &yes, sizeof yes);

	printf("srt connect\n");
	st = srt_connect(ss, (struct sockaddr*)&sa, sizeof sa);
	if (st == SRT_ERROR)
	{
		fprintf(stderr, "srt_connect: %s\n", srt_getlasterror_str());
		return 1;
	}

	//open camera
	if (!OpenCameraVideo(pFormatCtx, &videoIndex))
	{
		cout << "Couldn't open camera." << endl;
		return -1;
	}
	//std::thread EncodeThread(EncodeVideo, pFormatCtx, videoIndex, saveFile);
	//EncodeThread.join();
	//encode and write to file
	EncodeVideo(pFormatCtx,videoIndex,saveFile1,ts);

	//end
#ifdef SAVEFILE
	fclose(fp);
#else
	printf("srt close\n");
	srt_sendmsg2(ss, message, sizeof message, NULL);
	st = srt_close(ss);
	if (st == SRT_ERROR)
	{
		fprintf(stderr, "srt_close: %s\n", srt_getlasterror_str());
		return 1;
	}

	printf("srt cleanup\n");
	srt_cleanup();
#endif
	//don't wait	
	//int a;
	//cin >> a;
}

int OpenCameraVideo(AVFormatContext* pFormatCtx,int *pVideoIndex)
{
	string fileInput = "video=Integrated Camera";//"video=Integrated Webcam";

	pFormatCtx->interrupt_callback.callback = interrupt_cb;
	AVInputFormat *ifmt = av_find_input_format("dshow");
	AVDictionary *format_opts = nullptr;
	//av_dict_set_int(&format_opts, "rtbufsize", 18432000, 0);
	av_dict_set(&format_opts, "video_size", "640x480", 0);
	av_dict_set(&format_opts, "pixel_format", "bgr24", 0);
	//av_dict_set(&format_opts, "framerate", "25", 0);

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
	for (int i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMediaType::AVMEDIA_TYPE_VIDEO)
		{
			*pVideoIndex = i;
			break;
		}
	}
	if (*pVideoIndex == -1)
	{
		cout << "Couldn't find a video stream." << endl;
		return -1;
	}
}


int EncodeVideo(AVFormatContext *pFormatCtx, int videoIndex,string saveFile,void* ts)
{
	//bacause AV_CODEC_ID_H264 means libx264,while libx264 only support yuv420p pix format and libx264rgb can support bgr24 pix format
	AVCodec* pH264Codec = avcodec_find_encoder_by_name("libx264rgb");//avcodec_find_encoder(AVCodecID::AV_CODEC_ID_H264);
	if (!pH264Codec) {
		cout << "Couldn't find the H264 encoder." << endl;
		return -1;
	}
	AVCodecContext* pH264CodecContext = avcodec_alloc_context3(pH264Codec);
	/* put sample parameters */
	pH264CodecContext->bit_rate = 400000;
	/* resolution must be a multiple of two */
	pH264CodecContext->width = pFormatCtx->streams[videoIndex]->codecpar->width;
	pH264CodecContext->height = pFormatCtx->streams[videoIndex]->codecpar->height;
	/* frames per second */
	int frame_rate = 30;
	AVRational r1, r2;
	r1.num = 1;
	r1.den = frame_rate;
	r2.num = frame_rate;
	r2.den = 1;
	pH264CodecContext->time_base = r1;
	pH264CodecContext->framerate = r2;
	//emit one intra frame every ten frames
	pH264CodecContext->gop_size = 10;
	//No b frames for livestreaming
	pH264CodecContext->has_b_frames = 0;
	pH264CodecContext->max_b_frames = 0;
	pH264CodecContext->pix_fmt = AV_PIX_FMT_BGR24;//(AVPixelFormat)pFrame->format;//AV_PIX_FMT_YUV420P;
												  //for livestreaming reduce B frame and make realtime encoding
	av_opt_set(pH264CodecContext->priv_data, "preset", "superfast", 0);
	av_opt_set(pH264CodecContext->priv_data, "tune", "zerolatency", 0);

	/* open it */
	int ret = avcodec_open2(pH264CodecContext, pH264Codec, NULL);
	if (ret < 0) {
		//fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
		exit(1);
	}
	//encode
	AVPacket *pkt = av_packet_alloc();
	if (!pkt)
		exit(1);
	AVFrame *pFrame = av_frame_alloc();

	pFrame->format = pFormatCtx->streams[videoIndex]->codecpar->format;
	pFrame->width = pFormatCtx->streams[videoIndex]->codecpar->width;
	pFrame->height = pFormatCtx->streams[videoIndex]->codecpar->height;

	ret = av_frame_get_buffer(pFrame, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate the video frame data\n");
		exit(1);
	}
	//output file
	//FILE* f;
	//fopen_s(&f, saveFile.c_str(), "wb");
	//if (!f) {
	//	fprintf(stderr, "Could not open %s\n", "D:/fftestvideo");
	//	exit(1);
	//}
	
	/* encode 1 second of video */
	int stream_id = ts_stream(ts, PSI_STREAM_H264);
	for (int i = 0; i < 36000; i++) {
		//AVPacket *packet = (AVPacket *)av_malloc(sizeof(AVPacket));
		av_read_frame(pFormatCtx, pkt);
		//判断packet是否为视频帧
		if (pkt->stream_index == videoIndex) {
			//先确定这个packet的codec是啥玩意，然后看看是否需要解码,试了解码不行avopencodec2的时候不行
			//看来不用解码，直接就可以上马
			int size = pkt->size;//equal to 640*480*3 means in bytes
			ret = av_frame_make_writable(pFrame);
			if (ret < 0)
				exit(1);
			//pFrame->data[0] = pkt->data;
			//pFrame->linesize[0] = pFrame->width*3;

			//av_image_fill_linesizes(pFrame->linesize, AV_PIX_FMT_BGR24, 848);
			//av_image_fill_pointers(pFrame->data, AV_PIX_FMT_BGR24, 480, pkt->data, pFrame->linesize);

			pFrame->data[0] = pkt->data + 3 * (pFrame->height - 1) * ((pFrame->width + 3)&~3);
			pFrame->linesize[0] = 3 * ((pFrame->width + 3)&~3) * (-1);
			//调用H264进行编码
			AVRational timeb;
			pFrame->pts = i * 90000 / frame_rate;//AV_TIME_BASE* av_q2d(pH264CodecContext->time_base);
			//AVRational bq,cq;
			//cq.num = 1;
			//cq.den = 90000;
			//bq.num = 1;
			//bq.den = AV_TIME_BASE;
			////int64_t now = av_gettime();
			////把时间戳从一个时基调整到另外一个时基
			//pFrame->pts=av_rescale_q(pFrame->pts, bq, cq);
			
			/* encode the image */
			encode(pH264CodecContext, pFrame, pkt, ts);
			
			//放到发送队列，组包TS 通过SRT发送
			av_packet_unref(pkt);
		}
	}

	/* flush the encoder */
	encode(pH264CodecContext, NULL, pkt,ts);
	//mpeg_ts_write(ts, stream_id, pkt->flags & AV_PKT_FLAG_KEY, pkt->pts, pkt->dts, (void*)pkt->data, pkt->size);
	uint8_t endcode[] = { 0, 0, 1, 0xb7 };
	//fwrite(endcode, 1, sizeof(endcode), f);
	//fclose(f);

	avcodec_free_context(&pH264CodecContext);
	av_frame_free(&pFrame);
	av_packet_free(&pkt);
	mpeg_ts_destroy(ts);
}

void encode(AVCodecContext *enc_ctx, AVFrame *frame, AVPacket *pkt,void* ts)
{
	int ret;

	/* send the frame to the encoder */
	//if (frame)
	//	printf("Send frame %lld\n", frame->pts);

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
		//write to ts stream
		mpeg_ts_write(ts, ts_stream(ts, PSI_STREAM_H264), pkt->flags & AV_PKT_FLAG_KEY, pkt->pts, pkt->dts, (void*)pkt->data, pkt->size);
		//write to file
		//printf("Write packet %lld (size=%5d)\n", pkt->pts, pkt->size);
		//fwrite(pkt->data, 1, pkt->size, outfile);
		av_packet_unref(pkt);
	}
}

void InitFFmpeg()
{
	avdevice_register_all();
	//deprecated no need to call av_register_all
	//av_register_all();
	//avfilter_register_all(); 
}

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





