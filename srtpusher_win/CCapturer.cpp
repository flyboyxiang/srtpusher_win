#include "stdafx.h"
#include "CCapturer.h"

std::mutex g_locker;

CCapturer::CCapturer()
{
	m_pEncodeListener = NULL;
	m_pFormatCtx_Video =NULL;
	m_pFormatCtx_Audio = NULL;
	m_pH264Codec = NULL;
	m_videoIndex = -1;
	m_audioIndex = -1;
	m_bStop = true;
	m_pFormatCtx_Video = avformat_alloc_context();
}


CCapturer::~CCapturer()
{
	if (m_pFormatCtx_Video) {
		avformat_close_input(&m_pFormatCtx_Video);
	}
	if (m_pFormatCtx_Audio) {
		avformat_close_input(&m_pFormatCtx_Audio);
	}
	avcodec_free_context(&m_pH264CodecContext);
	av_frame_free(&m_pVideoFrame);
	av_packet_free(&m_pVideoPkt);
}

void CCapturer::SetEncodeListener(EncodeListener* pListener)
{
	this->m_pEncodeListener = pListener;
}

void CCapturer::InitFFmpeg()
{
	avdevice_register_all();
	//deprecated no need to call av_register_all
}

int CCapturer::OpenCameraVideo(int frame_rate, int bit_rate)
{
	string videoFileInput = "video=Integrated Camera";//"video=Integrated Webcam";
	string audioFileInput = "audio=麦克风阵列(Realtek High Definition Audio)";
	AVInputFormat *ifmt = av_find_input_format("dshow");
	AVDictionary *format_opts = nullptr;
	//av_dict_set_int(&format_opts, "rtbufsize", 18432000, 0);
	av_dict_set(&format_opts, "video_size", "640x480", 0);
	av_dict_set(&format_opts, "pixel_format", "bgr24", 0);

	int ret = avformat_open_input(&m_pFormatCtx_Video, videoFileInput.c_str(), ifmt, &format_opts);
	if (ret < 0)
	{
		return  ret;
	}
	ret = avformat_find_stream_info(m_pFormatCtx_Video, nullptr);
	av_dump_format(m_pFormatCtx_Video, 0, videoFileInput.c_str(), 0);
	if (ret < 0)
	{
		//failed
		return -1;
	}
	for (int i = 0; i < m_pFormatCtx_Video->nb_streams; i++)
	{
		if (m_pFormatCtx_Video->streams[i]->codecpar->codec_type == AVMediaType::AVMEDIA_TYPE_VIDEO)
		{
			m_videoIndex = i;
			break;
		}
	}
	if (m_videoIndex == -1)
	{
		//Couldn't find a video stream
		return -2;
	}
	//bacause AV_CODEC_ID_H264 means libx264,while libx264 only support yuv420p pix format and libx264rgb can support bgr24 pix format
	m_pH264Codec = avcodec_find_encoder_by_name("libx264rgb");//avcodec_find_encoder(AVCodecID::AV_CODEC_ID_H264);
	if (!m_pH264Codec) {
		//Couldn't find the H264 encoder
		return -1;
	}
	m_pH264CodecContext = avcodec_alloc_context3(m_pH264Codec);
	/* put sample parameters */
	m_pH264CodecContext->bit_rate = bit_rate;
	/* resolution must be a multiple of two */
	m_pH264CodecContext->width = m_pFormatCtx_Video->streams[m_videoIndex]->codecpar->width;
	m_pH264CodecContext->height = m_pFormatCtx_Video->streams[m_videoIndex]->codecpar->height;
	/* frames per second */
	AVRational r1, r2;
	r1.num = 1;
	r1.den = frame_rate;
	r2.num = frame_rate;
	r2.den = 1;
	m_pH264CodecContext->time_base = r1;
	m_pH264CodecContext->framerate = r2;
	//emit one intra frame every ten frames
	m_pH264CodecContext->gop_size = 10;
	//No b frames for livestreaming
	m_pH264CodecContext->has_b_frames = 0;
	m_pH264CodecContext->max_b_frames = 0;
	m_pH264CodecContext->pix_fmt = AV_PIX_FMT_BGR24;//(AVPixelFormat)pFrame->format;//AV_PIX_FMT_YUV420P;
													//for livestreaming reduce B frame and make realtime encoding
	av_opt_set(m_pH264CodecContext->priv_data, "preset", "superfast", 0);
	av_opt_set(m_pH264CodecContext->priv_data, "tune", "zerolatency", 0);

	/* open it */
	ret = avcodec_open2(m_pH264CodecContext, m_pH264Codec, NULL);
	if (ret < 0) {
		//fprintf(stderr, "Could not open codec: %s\n", av_err2str(ret));
		exit(1);
	}
}

void CCapturer::StartCapture() 
{
	//启动视频读取和编码线程
	m_bStop = false;
	m_pVideoThread = make_shared<thread>(CCapturer::EncodeVideoThread, this);
}

int CCapturer::EncodeVideoThread(void* data)
{
	CCapturer* pCapturer = (CCapturer*)data;
	EncodeListener* pListener = pCapturer->m_pEncodeListener;
	//encode
	AVFormatContext* pFormatCtx = pCapturer->m_pFormatCtx_Video;
	AVCodecContext* pCodecCtx = pCapturer->m_pH264CodecContext;
	int videoIndex = pCapturer->m_videoIndex;
	int frame_rate = pCapturer->m_pH264CodecContext->framerate.num;
	AVPacket *pVideoPkt = av_packet_alloc();
	AVFrame *pVideoFrame = av_frame_alloc();

	pVideoFrame->format = pFormatCtx->streams[videoIndex]->codecpar->format;
	pVideoFrame->width = pFormatCtx->streams[videoIndex]->codecpar->width;
	pVideoFrame->height = pFormatCtx->streams[videoIndex]->codecpar->height;

	int ret = av_frame_get_buffer(pVideoFrame, 32);
	if (ret < 0) {
		fprintf(stderr, "Could not allocate the video frame data\n");
		exit(1);
	}

	/* encode 1 second of video */
	int frame_index = 0;
	while(!pCapturer->m_bStop) {
		av_read_frame(pFormatCtx, pVideoPkt);
		//判断packet是否为视频帧
		if (pVideoPkt->stream_index == videoIndex) {
			int size = pVideoPkt->size;//equal to 640*480*3 means in bytes
			ret = av_frame_make_writable(pVideoFrame);
			if (ret < 0)
				exit(1);
			pVideoFrame->data[0] = pVideoPkt->data + 3 * (pVideoFrame->height - 1) * ((pVideoFrame->width + 3)&~3);
			pVideoFrame->linesize[0] = 3 * ((pVideoFrame->width + 3)&~3) * (-1);
			//调用H264进行编码
			AVRational timeb;
			pVideoFrame->pts = frame_index * 90000 / frame_rate;//AV_TIME_BASE* av_q2d(pH264CodecContext->time_base);
		    /* encode the image */
			CCapturer::Encode(pVideoPkt,pVideoFrame, data);
			//放到发送队列，组包TS 通过SRT发送
			av_packet_unref(pVideoPkt);
			//帧序号递增
			frame_index++;
		}
	}

	/* flush the encoder */
	CCapturer::Encode(pVideoPkt, NULL, data);
}

void CCapturer::Encode(AVPacket* pPacket,AVFrame *pFrame, void *data)
{
	CCapturer* pCapturer = (CCapturer*)data;
	AVCodecContext* pCodecCtx = pCapturer->m_pH264CodecContext;

	/* send the frame to the encoder */
	int ret = avcodec_send_frame(pCodecCtx, pFrame);
	if (ret < 0) {
		fprintf(stderr, "Error sending a frame for encoding\n");
		exit(1);
	}

	while (ret >= 0) {
		ret = avcodec_receive_packet(pCodecCtx, pPacket);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
			return;
		else if (ret < 0) {
			fprintf(stderr, "Error during encoding\n");
			exit(1);
		}
		//just lock
		std::lock_guard<std::mutex> locker(g_locker);
		VIDEOBUFFER buffer;
		buffer.flags = pPacket->flags & AV_PKT_FLAG_KEY;
		buffer.pts = pPacket->pts;
		buffer.dts = pPacket->dts;
		buffer.len = pPacket->size;
		buffer.data = new uint8_t[buffer.len];
		pCapturer->m_vbuffer_queue.push(buffer);
		//pListener->OnVideoEncodedBuffer(pPacket->flags & AV_PKT_FLAG_KEY, pPacket->pts, pPacket->dts, (void*)pPacket->data, pPacket->size);
		//just unlock
		av_packet_unref(pPacket);
	}
}

